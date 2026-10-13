#include "lavanda/runtime/float_conversion.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kUint32Max = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kBound = 64;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kFloatMax = std::numeric_limits<float>::max();
constexpr float kTwoPow32 = 4294967296.0f;
constexpr float kLargestFloatBelowTwoPow32 = 4294967040.0f;

// ---- tests -----------------------------------------------------------------

TEST(ClampFloatToCountTest, NaNZeroAndNegativesBecomeZero) {
  const float values[] = {0.0f,   -0.0f,      -0.5f, -1.0f,
                          -1e20f, -kFloatMax, -kInf, kNaN};

  for (float value : values) {
    EXPECT_EQ(ClampFloatToCount(value, kBound), 0u) << value;
    EXPECT_EQ(ClampFloatToCount(value, kUint32Max), 0u) << value;
  }
}

TEST(ClampFloatToCountTest, FractionsTruncateTowardZero) {
  EXPECT_EQ(ClampFloatToCount(0.9f, kBound), 0u);
  EXPECT_EQ(ClampFloatToCount(1.0f, kBound), 1u);
  EXPECT_EQ(ClampFloatToCount(1.9f, kBound), 1u);
  EXPECT_EQ(ClampFloatToCount(20.5f, kBound), 20u);
}

TEST(ClampFloatToCountTest, InRangeValuesPassThrough) {
  EXPECT_EQ(ClampFloatToCount(20.0f, kBound), 20u);
  EXPECT_EQ(ClampFloatToCount(63.0f, kBound), 63u);
  EXPECT_EQ(ClampFloatToCount(64.0f, kBound), 64u);
}

TEST(ClampFloatToCountTest, ValuesAboveTheBoundSaturateToIt) {
  const float values[] = {65.0f,     1e9f,  kLargestFloatBelowTwoPow32,
                          kTwoPow32, 1e20f, kFloatMax,
                          kInf};

  for (float value : values) {
    EXPECT_EQ(ClampFloatToCount(value, kBound), kBound) << value;
  }
}

TEST(ClampFloatToCountTest, FullRangeBoundIsExactAndNeverOverflows) {
  EXPECT_EQ(ClampFloatToCount(kLargestFloatBelowTwoPow32, kUint32Max),
            4294967040u);
  EXPECT_EQ(ClampFloatToCount(kTwoPow32, kUint32Max), kUint32Max);
  EXPECT_EQ(ClampFloatToCount(1e20f, kUint32Max), kUint32Max);
  EXPECT_EQ(ClampFloatToCount(kFloatMax, kUint32Max), kUint32Max);
  EXPECT_EQ(ClampFloatToCount(kInf, kUint32Max), kUint32Max);
}

TEST(ClampFloatToCountTest, ZeroBoundAlwaysYieldsZero) {
  EXPECT_EQ(ClampFloatToCount(5.0f, 0), 0u);
  EXPECT_EQ(ClampFloatToCount(kInf, 0), 0u);
  EXPECT_EQ(ClampFloatToCount(kNaN, 0), 0u);
}

}  // namespace
}  // namespace lavanda
