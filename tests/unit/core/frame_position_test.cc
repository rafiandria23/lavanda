#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "lavanda/core/time.h"

namespace lavanda {
namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

FramePosition Pos(std::uint64_t v) { return FramePosition{v}; }
FrameCount Count(std::uint64_t v) { return FrameCount{v}; }

TEST(FramePositionTypeTest, ToIntegerRoundTrips) {
  EXPECT_EQ(ToInteger(Pos(0)), 0u);
  EXPECT_EQ(ToInteger(Pos(48000)), 48000u);
  EXPECT_EQ(ToInteger(kMaxFramePosition), kMax);
}

TEST(FramePositionTypeTest, OrdersByValue) {
  EXPECT_TRUE(Pos(1) < Pos(2));
  EXPECT_TRUE(Pos(2) > Pos(1));
  EXPECT_TRUE(Pos(5) == Pos(5));
  EXPECT_TRUE(Pos(5) != Pos(6));
}

TEST(FramePositionTypeTest, AddSaturatingAddsNormally) {
  EXPECT_EQ(ToInteger(AddSaturating(Pos(1000), Count(256))), 1256u);
  EXPECT_EQ(ToInteger(AddSaturating(Pos(1000), Count(0))), 1000u);
}

TEST(FramePositionTypeTest, AddSaturatingClampsInsteadOfWrapping) {
  EXPECT_EQ(ToInteger(AddSaturating(Pos(kMax), Count(1))), kMax);
  EXPECT_EQ(ToInteger(AddSaturating(Pos(kMax - 5), Count(5))), kMax);
  EXPECT_EQ(ToInteger(AddSaturating(Pos(kMax - 5), Count(6))), kMax);
  EXPECT_EQ(ToInteger(AddSaturating(Pos(10), Count(kMax))), kMax);
}

TEST(FramePositionTypeTest, FramesBetweenMeasuresForwardDistance) {
  EXPECT_EQ(ToInteger(FramesBetween(Pos(100), Pos(356))), 256u);
  EXPECT_EQ(ToInteger(FramesBetween(Pos(100), Pos(100))), 0u);
}

TEST(FramePositionTypeTest, FramesBetweenIsZeroWhenTargetIsNotAfter) {
  EXPECT_EQ(ToInteger(FramesBetween(Pos(356), Pos(100))), 0u);
}

}  // namespace
}  // namespace lavanda
