#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/runtime_config.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

TEST(AudioRuntimeConcurrencyTest,
     RapidVoiceCreateDestroyUnderConcurrentRendering) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();

  RuntimeConfig config;

  config.max_voices = 4;
  config.command_queue_capacity = 16;

  AudioRuntime runtime(std::move(owned_device), config);

  ASSERT_TRUE(runtime.Start().ok());

  std::atomic<bool> keep_rendering{true};
  std::thread render_thread([&]() {
    AudioBuffer buffer(64, 2);

    while (keep_rendering.load(std::memory_order_relaxed)) {
      device->PumpRender(buffer.View());
      std::this_thread::yield();
    }
  });

  constexpr int kIterations = 3000;

  for (int i = 0; i < kIterations; ++i) {
    StatusOr<Voice> voice_or = runtime.CreateVoice();

    while (!voice_or.ok()) {
      std::this_thread::yield();
      voice_or = runtime.CreateVoice();
    }

    Voice voice = voice_or.value();
    Status set_status =
        voice.SetFrequency(220.0f + static_cast<float>(i % 400));

    while (!set_status.ok()) {
      std::this_thread::yield();
      set_status = voice.SetFrequency(220.0f + static_cast<float>(i % 400));
    }

    Status start_status = voice.Start();

    while (!start_status.ok()) {
      std::this_thread::yield();
      start_status = voice.Start();
    }

    Status destroy_status = voice.Destroy();

    while (!destroy_status.ok()) {
      std::this_thread::yield();
      destroy_status = voice.Destroy();
    }
  }

  keep_rendering.store(false, std::memory_order_relaxed);
  render_thread.join();

  EXPECT_GT(runtime.stats().render_count, 0u);

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeConcurrencyTest,
     RapidGainPanChangesOnPersistentVoicesUnderConcurrentRendering) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  constexpr int kVoiceCount = 4;
  std::vector<Voice> voices;

  for (int i = 0; i < kVoiceCount; ++i) {
    StatusOr<Voice> voice_or = runtime.CreateVoice();

    ASSERT_TRUE(voice_or.ok());
    ASSERT_TRUE(voice_or.value().SetFrequency(220.0f).ok());
    ASSERT_TRUE(voice_or.value().Start().ok());

    voices.push_back(voice_or.value());
  }

  std::atomic<bool> keep_rendering{true};
  std::thread render_thread([&]() {
    AudioBuffer buffer(64, 2);

    while (keep_rendering.load(std::memory_order_relaxed)) {
      device->PumpRender(buffer.View());
      std::this_thread::yield();
    }
  });

  constexpr int kIterations = 8000;

  for (int i = 0; i < kIterations; ++i) {
    Voice& voice = voices[static_cast<std::size_t>(i) % voices.size()];

    static_cast<void>(voice.SetGain(0.1f + 0.05f * static_cast<float>(i % 18)));
    static_cast<void>(voice.SetPan(-1.0f + 0.1f * static_cast<float>(i % 20)));
  }

  for (Voice& voice : voices) {
    Status status = voice.SetGain(1.0f);

    while (!status.ok()) {
      std::this_thread::yield();
      status = voice.SetGain(1.0f);
    }

    status = voice.SetPan(0.0f);

    while (!status.ok()) {
      std::this_thread::yield();
      status = voice.SetPan(0.0f);
    }
  }

  keep_rendering.store(false, std::memory_order_relaxed);
  render_thread.join();

  AudioBuffer final_buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(final_buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (final_buffer(f, 0) != 0.0f) {
      any_nonzero = true;
    }
  }

  EXPECT_TRUE(any_nonzero);

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeConcurrencyTest,
     QueueSaturationRecoversOnceAudioThreadResumesConsuming) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();

  RuntimeConfig config;
  config.command_queue_capacity = 4;

  AudioRuntime runtime(std::move(owned_device), config);

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  bool saw_queue_full = false;

  for (int i = 0; i < 100 && !saw_queue_full; ++i) {
    Status status = voice.SetFrequency(100.0f + static_cast<float>(i));

    if (!status.ok()) {
      EXPECT_EQ(status.code(), ErrorCode::kQueueFull);

      saw_queue_full = true;
    }
  }

  EXPECT_TRUE(saw_queue_full)
      << "expected the tiny queue to saturate with nothing consuming it";

  std::atomic<bool> keep_rendering{true};
  std::thread render_thread([&]() {
    AudioBuffer buffer(64, 2);
    while (keep_rendering.load(std::memory_order_relaxed)) {
      device->PumpRender(buffer.View());
      std::this_thread::yield();
    }
  });

  Status recovered_status = voice.SetFrequency(440.0f);

  while (!recovered_status.ok()) {
    std::this_thread::yield();
    recovered_status = voice.SetFrequency(440.0f);
  }

  EXPECT_TRUE(recovered_status.ok());

  keep_rendering.store(false, std::memory_order_relaxed);
  render_thread.join();

  EXPECT_TRUE(runtime.Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
