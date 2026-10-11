#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kChannels = 2;

class RuntimeFramePositionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device));
  }

  void Pump(std::uint32_t frames) {
    AudioBuffer buffer(frames, kChannels);

    ASSERT_TRUE(device_->PumpRender(buffer.View()));
  }

  std::uint64_t Position() const {
    return ToInteger(runtime_->frame_position());
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

TEST_F(RuntimeFramePositionTest, IsZeroBeforeAnyCallback) {
  EXPECT_EQ(Position(), 0u);

  ASSERT_TRUE(runtime_->Start().ok());

  EXPECT_EQ(Position(), 0u);
}

TEST_F(RuntimeFramePositionTest, AdvancesByTheFullFrameCountOfEachCallback) {
  ASSERT_TRUE(runtime_->Start().ok());

  Pump(256);

  EXPECT_EQ(Position(), 256u);

  Pump(512);

  EXPECT_EQ(Position(), 768u);

  Pump(1);

  EXPECT_EQ(Position(), 769u);

  Pump(37);

  EXPECT_EQ(Position(), 806u);
}

TEST_F(RuntimeFramePositionTest, DoesNotAdvanceWhileStoppedAndIsNotReset) {
  ASSERT_TRUE(runtime_->Start().ok());

  Pump(100);

  ASSERT_TRUE(runtime_->Stop().ok());

  AudioBuffer buffer(64, kChannels);

  EXPECT_FALSE(device_->PumpRender(buffer.View()));
  EXPECT_EQ(Position(), 100u);

  ASSERT_TRUE(runtime_->Start().ok());

  Pump(50);

  EXPECT_EQ(Position(), 150u);
}

TEST_F(RuntimeFramePositionTest, DependsOnFramesRenderedNotWallClockTime) {
  ASSERT_TRUE(runtime_->Start().ok());

  Pump(256);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  Pump(256);

  EXPECT_EQ(Position(), 512u);
}

}  // namespace
}  // namespace lavanda
