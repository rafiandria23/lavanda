#include "lavanda/runtime/audio_runtime.h"

#include <gtest/gtest.h>

#include <memory>

#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

TEST(AudioRuntimeTest, StartSucceedsWithWorkingDevice) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  EXPECT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.is_running());
}

TEST(AudioRuntimeTest, StartFailsWithNullDevice) {
  AudioRuntime runtime(nullptr);
  Status status = runtime.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kDeviceUnavailable);
}

TEST(AudioRuntimeTest, StartPropagatesDeviceOpenFailure) {
  AudioRuntime runtime(
      std::make_unique<test_support::FakeAudioDevice>(/*fail_open=*/true));
  Status status = runtime.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kPlatformError);
  EXPECT_FALSE(runtime.is_running());
}

TEST(AudioRuntimeTest, StartingAlreadyRunningRuntimeReturnsAlreadyOpen) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  Status status = runtime.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kAlreadyOpen);
}

TEST(AudioRuntimeTest, StopThenStartResumes) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Stop().ok());
  EXPECT_FALSE(runtime.is_running());
  EXPECT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.is_running());
}

TEST(AudioRuntimeTest, StopIsIdempotent) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.Stop().ok());
  EXPECT_TRUE(runtime.Stop().ok());
}

TEST(AudioRuntimeTest, ShutdownThenStartReopensSuccessfully) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
  EXPECT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.is_running());
}

TEST(AudioRuntimeTest, ShutdownIsIdempotent) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeTest, SubmitSucceedsWhenQueueHasRoom) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());
}

TEST(AudioRuntimeTest, SubmitFailsWithQueueFullWhenSaturated) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>(),
                       /*command_queue_capacity=*/2);

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 440.0f})
          .ok());
  EXPECT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 880.0f})
          .ok());
  Status status =
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 220.0f});
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kQueueFull);
}

TEST(AudioRuntimeTest, StatsStartAtZero) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  EXPECT_EQ(runtime.stats().render_count, 0u);
}

TEST(AudioRuntimeTest, PumpedRenderIncrementsStats) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  EXPECT_EQ(runtime.stats().render_count, 1u);
  EXPECT_EQ(runtime.stats().last_callback_frame_count, 64u);
}

TEST(AudioRuntimeTest, CommandsApplyBeforeTheRenderTheyArriveIn) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 880.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetGain, .value = 1.0f}).ok());

  AudioBuffer buffer(64, 1);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) any_nonzero = true;
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeTest, StoppedRuntimeRefusesPumpedRender) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());
  ASSERT_TRUE(runtime.Stop().ok());

  AudioBuffer buffer(64, 1);

  EXPECT_FALSE(device->PumpRender(buffer.View()));
}

TEST(AudioRuntimeTest, MultipleCommandsInOneRenderBlockAllApply) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 100.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 200.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 300.0f})
          .ok());
  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());

  AudioBuffer first(8, 1);

  ASSERT_TRUE(device->PumpRender(first.View()));

  AudioBuffer second(8, 1);

  ASSERT_TRUE(device->PumpRender(second.View()));

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 8; ++f) {
    if (second(f, 0) != 0.0f) any_nonzero = true;
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(AudioRuntimeTest, ZeroLengthRenderBlockDoesNotCrash) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioBuffer empty_buffer(0, 2);

  EXPECT_TRUE(device->PumpRender(empty_buffer.View()));
}

TEST(AudioRuntimeTest, ShutdownWithPendingCommandsDoesNotCrash) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  AudioRuntime runtime(std::move(owned_device), /*command_queue_capacity=*/4);

  ASSERT_TRUE(runtime.Start().ok());

  EXPECT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 100.0f})
          .ok());
  EXPECT_TRUE(
      runtime.Submit({.type = CommandType::kSetGain, .value = 0.3f}).ok());
  EXPECT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());

  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

}  // namespace
}  // namespace lavanda
