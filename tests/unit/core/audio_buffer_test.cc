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

}  // namespace
}  // namespace lavanda
