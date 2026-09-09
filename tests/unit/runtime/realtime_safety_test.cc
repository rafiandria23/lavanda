#include <gtest/gtest.h>

#include <memory>

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
    EXPECT_EQ(guard.allocation_count(), 0u)
        << "render path allocated " << guard.allocation_count()
        << " time(s) -- see RenderState::Render, CommandQueue::TryPop, "
           "AudioClock::Advance, RenderDiagnostics::RecordRender";
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

}  // namespace
}  // namespace lavanda
