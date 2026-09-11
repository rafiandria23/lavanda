#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "lavanda/runtime/audio_runtime.h"
#include "test_support/allocation_guard.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

TEST(RealtimeSafetyTest, RenderPathPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 440.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetGain, .value = 0.5f}).ok());

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest, DrainingManyQueuedCommandsPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device), /*command_queue_capacity=*/32);

  ASSERT_TRUE(runtime.Start().ok());

  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(runtime
                    .Submit({.type = CommandType::kSetFrequency,
                             .value = static_cast<float>(i)})
                    .ok());
  }

  AudioBuffer buffer(64, 1);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest, PopulatedMixerRenderPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();
  std::vector<Voice> voices;

  for (int i = 0; i < 4; ++i) {
    StatusOr<Voice> voice_or = runtime.CreateVoice();

    ASSERT_TRUE(voice_or.ok());

    Voice voice = voice_or.value();

    ASSERT_TRUE(
        voice.SetFrequency(220.0f + static_cast<float>(i) * 110.0f).ok());
    ASSERT_TRUE(voice.SetGain(0.5f).ok());
    ASSERT_TRUE(voice.SetPan(-0.5f + static_cast<float>(i) * 0.3f).ok());

    if (i % 2 == 0) {
      ASSERT_TRUE(voice.SetBus(bus).ok());
    }

    ASSERT_TRUE(voice.Start().ok());

    voices.push_back(voice);
  }

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u)
        << "populated mixer render allocated " << guard.allocation_count()
        << " time(s) -- see VoicePool::RenderVoiceSource, "
           "AudioRuntime::Impl::RenderVoicesAndBuses, mixer_math.cc";
  }
}

TEST(RealtimeSafetyTest, DrainingVoiceAndBusCommandsPerformsNoHeapAllocation) {
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
  ASSERT_TRUE(voice.SetGain(0.8f).ok());
  ASSERT_TRUE(voice.SetPan(0.25f).ok());
  ASSERT_TRUE(voice.SetBus(bus).ok());
  ASSERT_TRUE(voice.Start().ok());
  ASSERT_TRUE(bus.SetGain(0.6f).ok());

  AudioBuffer buffer(64, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest,
     RepeatedRenderBlocksWithActiveVoicesStayAllocationFree) {
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

  AudioBuffer buffer(128, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

}  // namespace
}  // namespace lavanda
