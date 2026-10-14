#include "lavanda/spatial/vec3.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

constexpr float kTolerance = 1.0e-6f;

TEST(Vec3Test, DefaultsToZero) {
  const Vec3 v;

  EXPECT_EQ(v.x, 0.0f);
  EXPECT_EQ(v.y, 0.0f);
  EXPECT_EQ(v.z, 0.0f);
}

TEST(Vec3Test, AddsAndSubtracts) {
  const Vec3 a{1.0f, 2.0f, 3.0f};
  const Vec3 b{0.5f, -1.0f, 4.0f};

  const Vec3 sum = a + b;
  const Vec3 difference = a - b;

  EXPECT_FLOAT_EQ(sum.x, 1.5f);
  EXPECT_FLOAT_EQ(sum.y, 1.0f);
  EXPECT_FLOAT_EQ(sum.z, 7.0f);
  EXPECT_FLOAT_EQ(difference.x, 0.5f);
  EXPECT_FLOAT_EQ(difference.y, 3.0f);
  EXPECT_FLOAT_EQ(difference.z, -1.0f);
}

TEST(Vec3Test, ScalesByAScalarOnEitherSide) {
  const Vec3 v{1.0f, -2.0f, 3.0f};

  const Vec3 left = 2.0f * v;
  const Vec3 right = v * 2.0f;

  EXPECT_FLOAT_EQ(left.x, 2.0f);
  EXPECT_FLOAT_EQ(left.y, -4.0f);
  EXPECT_FLOAT_EQ(left.z, 6.0f);
  EXPECT_FLOAT_EQ(right.x, left.x);
  EXPECT_FLOAT_EQ(right.y, left.y);
  EXPECT_FLOAT_EQ(right.z, left.z);
}

TEST(Vec3Test, DotProduct) {
  EXPECT_FLOAT_EQ(Dot(Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, -5.0f, 6.0f}), 12.0f);
  EXPECT_FLOAT_EQ(Dot(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}), 0.0f);
}

TEST(Vec3Test, CrossProductIsRightHanded) {
  const Vec3 x{1.0f, 0.0f, 0.0f};
  const Vec3 y{0.0f, 1.0f, 0.0f};

  const Vec3 z = Cross(x, y);

  EXPECT_NEAR(z.x, 0.0f, kTolerance);
  EXPECT_NEAR(z.y, 0.0f, kTolerance);
  EXPECT_NEAR(z.z, 1.0f, kTolerance);

  const Vec3 minus_z = Cross(y, x);

  EXPECT_NEAR(minus_z.z, -1.0f, kTolerance);
}

TEST(Vec3Test, LengthMatchesPythagoreanQuadruple) {
  EXPECT_NEAR(Length(Vec3{3.0f, 4.0f, 12.0f}), 13.0f, kTolerance);
}

TEST(Vec3Test, LengthSquaredAvoidsTheSquareRoot) {
  EXPECT_FLOAT_EQ(LengthSquared(Vec3{3.0f, 4.0f, 12.0f}), 169.0f);
}

}  // namespace
}  // namespace lavanda
