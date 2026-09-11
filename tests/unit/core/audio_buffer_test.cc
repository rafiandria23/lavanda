#include "lavanda/core/audio_buffer.h"

#include <gtest/gtest.h>

#include "test_support/deterministic_signal.h"

namespace lavanda {
namespace {

TEST(AudioBufferTest, ConstructedBufferIsZeroed) {
  AudioBuffer buffer(4, 2);

  for (std::uint32_t f = 0; f < 4; ++f) {
    for (std::uint32_t c = 0; c < 2; ++c) {
      EXPECT_EQ(buffer(f, c), 0.0f);
    }
  }
}

TEST(AudioBufferTest, ReadWriteRoundTrips) {
  AudioBuffer buffer(2, 2);

  buffer(0, 0) = 1.0f;
  buffer(1, 1) = -0.5f;

  EXPECT_FLOAT_EQ(buffer(0, 0), 1.0f);
  EXPECT_FLOAT_EQ(buffer(1, 1), -0.5f);
}

TEST(AudioBufferTest, ClearZeroesExistingContent) {
  AudioBuffer buffer(2, 2);

  test_support::FillWithRamp(buffer.View());

  buffer.Clear();

  EXPECT_EQ(buffer(1, 1), 0.0f);
}

TEST(AudioBufferViewTest, InterleavesFrameMajorChannelMinor) {
  AudioBuffer buffer(2, 2);

  test_support::FillWithRamp(buffer.View());

  EXPECT_FLOAT_EQ(buffer(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(buffer(0, 1), 1.0f);
  EXPECT_FLOAT_EQ(buffer(1, 0), 2.0f);
  EXPECT_FLOAT_EQ(buffer(1, 1), 3.0f);
}

TEST(AudioBufferViewTest, ViewSharesStorageWithOwningBuffer) {
  AudioBuffer buffer(1, 1);

  buffer.View()(0, 0) = 42.0f;

  EXPECT_FLOAT_EQ(buffer(0, 0), 42.0f);
}

TEST(AudioBufferViewTest, DefaultConstructedViewIsEmpty) {
  AudioBufferView view;

  EXPECT_TRUE(view.empty());
}

TEST(AudioBufferTest, PartialViewCoversOnlyRequestedPrefix) {
  AudioBuffer buffer(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) buffer(f, 0) = static_cast<float>(f);

  AudioBufferView partial = buffer.View(3);

  EXPECT_EQ(partial.frame_count(), 3u);

  for (std::uint32_t f = 0; f < 3; ++f) {
    EXPECT_FLOAT_EQ(partial(f, 0), static_cast<float>(f));
  }
}

TEST(AudioBufferTest, PartialViewSharesStorageWithFullBuffer) {
  AudioBuffer buffer(4, 1);
  buffer.View(2)(0, 0) = 9.0f;

  EXPECT_FLOAT_EQ(buffer(0, 0), 9.0f);
}

TEST(AudioBufferTest, PartialViewRequestLargerThanBufferIsClamped) {
  AudioBuffer buffer(4, 1);
  AudioBufferView view = buffer.View(999);

  EXPECT_EQ(view.frame_count(), 4u);
}

}  // namespace
}  // namespace lavanda
