#include <gtest/gtest.h>

#include <memory>

#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

TEST(AudioRuntimeMixingTest, CreateVoiceSucceedsAndStartsStopped) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice = runtime.CreateVoice();

  ASSERT_TRUE(voice.ok());
  EXPECT_TRUE(voice.value().is_valid());
}

TEST(AudioRuntimeMixingTest, SingleVoiceProducesNonSilentStereoOutput) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(1.0f).ok());
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f || buffer(f, 1) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeMixingTest, HardLeftPanProducesSilenceOnRightChannel) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(1.0f).ok());
  ASSERT_TRUE(voice.SetPan(-1.0f).ok());
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  for (std::uint32_t f = 0; f < 64; ++f) {
    EXPECT_NEAR(buffer(f, 1), 0.0f, 1e-5f);
  }
}

TEST(AudioRuntimeMixingTest, VoiceRoutedThroughUserBusReachesOutput) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(1.0f).ok());
  ASSERT_TRUE(voice.SetBus(bus).ok());
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeMixingTest, BusGainZeroSilencesRoutedVoice) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();

  ASSERT_TRUE(bus.SetGain(0.0f).ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(1.0f).ok());
  ASSERT_TRUE(voice.SetBus(bus).ok());
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  for (std::uint32_t f = 0; f < 64; ++f) {
    EXPECT_NEAR(buffer(f, 0), 0.0f, 1e-5f);
    EXPECT_NEAR(buffer(f, 1), 0.0f, 1e-5f);
  }
}

TEST(AudioRuntimeMixingTest, DestroyedVoiceStaleHandleCannotAffectNewVoice) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();

  RuntimeConfig config;
  config.max_voices = 1;

  AudioRuntime runtime(std::move(owned_device), config);

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_a_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_a_or.ok());

  Voice voice_a = voice_a_or.value();
  VoiceId stale_id = voice_a.id();  // captured BEFORE Destroy() wipes it

  ASSERT_TRUE(voice_a.Destroy().ok());
  EXPECT_FALSE(voice_a.is_valid())
      << "Destroy() must invalidate the handle immediately on success";
  EXPECT_FALSE(voice_a.Stop().ok())
      << "an invalidated Voice handle must refuse to submit anything at "
         "all, not just be silently ignored by the audio thread";

  StatusOr<Voice> voice_b_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_b_or.ok());

  Voice voice_b = voice_b_or.value();

  ASSERT_EQ(voice_b.id().index, stale_id.index)
      << "capacity is 1, so B must have reused A's slot for this test to "
         "actually exercise generation-based rejection";
  ASSERT_NE(voice_b.id().generation, stale_id.generation);
  ASSERT_TRUE(voice_b.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice_b.SetGain(1.0f).ok());
  ASSERT_TRUE(voice_b.Start().ok());

  EXPECT_TRUE(
      runtime.Submit({.type = CommandType::kStopVoice, .voice_id = stale_id})
          .ok())
      << "Submit() itself succeeds -- rejection happens on the audio "
         "thread, not here";

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero)
      << "B must still be playing -- A's stale StopVoice must have been "
         "ignored by the audio thread";
}

TEST(AudioRuntimeMixingTest, ShutdownWhileVoicesActiveDoesNotCrash) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());
  ASSERT_TRUE(voice_or.value().SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice_or.value().Start().ok());

  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

}  // namespace
}  // namespace lavanda
