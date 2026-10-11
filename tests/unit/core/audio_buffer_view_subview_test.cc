#include <gtest/gtest.h>

#include <cstdint>

#include "lavanda/core/audio_buffer.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kFrames = 8;
constexpr std::uint32_t kChannels = 2;

Sample Tag(std::uint32_t frame, std::uint32_t channel) {
  return static_cast<Sample>(frame * 10 + channel);
}

class AudioBufferViewSubViewTest : public ::testing::Test {
 protected:
  AudioBufferViewSubViewTest() : buffer_(kFrames, kChannels) {
    for (std::uint32_t f = 0; f < kFrames; ++f) {
      for (std::uint32_t c = 0; c < kChannels; ++c) {
        buffer_(f, c) = Tag(f, c);
      }
    }
  }

  AudioBuffer buffer_;
};

TEST_F(AudioBufferViewSubViewTest, ExposesTheRequestedFrameRange) {
  AudioBufferView sub = buffer_.View().SubView(3, 4);

  ASSERT_EQ(sub.frame_count(), 4u);
  ASSERT_EQ(sub.channel_count(), kChannels);
  EXPECT_FLOAT_EQ(sub(0, 0), Tag(3, 0));
  EXPECT_FLOAT_EQ(sub(0, 1), Tag(3, 1));
  EXPECT_FLOAT_EQ(sub(3, 1), Tag(6, 1));
}

TEST_F(AudioBufferViewSubViewTest, WritesGoThroughToTheUnderlyingBuffer) {
  AudioBufferView sub = buffer_.View().SubView(3, 2);

  sub(0, 0) = -1.0f;
  sub(1, 1) = -2.0f;

  EXPECT_FLOAT_EQ(buffer_(3, 0), -1.0f);
  EXPECT_FLOAT_EQ(buffer_(4, 1), -2.0f);
  EXPECT_FLOAT_EQ(buffer_(2, 0), Tag(2, 0));
  EXPECT_FLOAT_EQ(buffer_(5, 0), Tag(5, 0));
}

TEST_F(AudioBufferViewSubViewTest, ClampsCountToTheViewBounds) {
  AudioBufferView sub = buffer_.View().SubView(6, 100);

  EXPECT_EQ(sub.frame_count(), 2u);
  EXPECT_FLOAT_EQ(sub(1, 0), Tag(7, 0));
}

TEST_F(AudioBufferViewSubViewTest, OffsetAtOrPastTheEndIsEmpty) {
  EXPECT_TRUE(buffer_.View().SubView(kFrames, 1).empty());
  EXPECT_TRUE(buffer_.View().SubView(kFrames + 5, 1).empty());
}

TEST_F(AudioBufferViewSubViewTest, ZeroCountIsEmpty) {
  EXPECT_TRUE(buffer_.View().SubView(2, 0).empty());
}

TEST_F(AudioBufferViewSubViewTest, DefaultViewYieldsEmptySubView) {
  EXPECT_TRUE(AudioBufferView().SubView(0, 4).empty());
}

TEST_F(AudioBufferViewSubViewTest, AdjacentSubViewsTileWithoutGapOrOverlap) {
  AudioBufferView view = buffer_.View();
  AudioBufferView first = view.SubView(0, 3);
  AudioBufferView second = view.SubView(3, 5);

  EXPECT_EQ(first.frame_count() + second.frame_count(), kFrames);
  EXPECT_FLOAT_EQ(first(2, 0), Tag(2, 0));
  EXPECT_FLOAT_EQ(second(0, 0), Tag(3, 0));
  EXPECT_FLOAT_EQ(second(4, 1), Tag(7, 1));
}

TEST_F(AudioBufferViewSubViewTest, SubViewOfSubViewComposesOffsets) {
  AudioBufferView nested = buffer_.View().SubView(2, 5).SubView(1, 2);

  ASSERT_EQ(nested.frame_count(), 2u);
  EXPECT_FLOAT_EQ(nested(0, 0), Tag(3, 0));
  EXPECT_FLOAT_EQ(nested(1, 1), Tag(4, 1));
}

}  // namespace
}  // namespace lavanda
