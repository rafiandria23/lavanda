#include "lavanda/spatial/quaternion.h"

#include <gtest/gtest.h>

#include <limits>
#include <numbers>

namespace lavanda {
namespace {

constexpr float kTolerance = 1.0e-5f;
constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;

constexpr Vec3 kRight{1.0f, 0.0f, 0.0f};
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kForward{0.0f, 0.0f, -1.0f};

void ExpectVec3Near(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST(QuaternionTest, DefaultIsIdentity) {
  const Quaternion q;

  EXPECT_EQ(q.w, 1.0f);
  EXPECT_EQ(q.x, 0.0f);
  EXPECT_EQ(q.y, 0.0f);
  EXPECT_EQ(q.z, 0.0f);
}

TEST(QuaternionTest, IdentityLeavesVectorsUnchanged) {
  ExpectVec3Near(Rotate(Quaternion{}, Vec3{1.0f, -2.0f, 3.0f}),
                 Vec3{1.0f, -2.0f, 3.0f});
}

TEST(QuaternionTest, YawNinetyMapsRightToMinusZ) {
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, kHalfPi);

  ExpectVec3Near(Rotate(yaw, kRight), Vec3{0.0f, 0.0f, -1.0f});
}

TEST(QuaternionTest, YawNinetyTurnsTheListenerToFaceMinusX) {
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, kHalfPi);

  ExpectVec3Near(Rotate(yaw, kForward), Vec3{-1.0f, 0.0f, 0.0f});
}

TEST(QuaternionTest, PitchNinetyTurnsForwardToUp) {
  const Quaternion pitch = QuaternionFromAxisAngle(kRight, kHalfPi);

  ExpectVec3Near(Rotate(pitch, kForward), Vec3{0.0f, 1.0f, 0.0f});
}

TEST(QuaternionTest, InverseRotateUndoesRotate) {
  const Quaternion q = QuaternionFromAxisAngle(Vec3{1.0f, 2.0f, 3.0f}, 0.7f);
  const Vec3 v{0.3f, -1.2f, 2.5f};

  ExpectVec3Near(InverseRotate(q, Rotate(q, v)), v);
}

TEST(QuaternionTest, RotationPreservesLength) {
  const Quaternion q = QuaternionFromAxisAngle(Vec3{-2.0f, 1.0f, 0.5f}, 2.1f);
  const Vec3 v{0.3f, -1.2f, 2.5f};

  EXPECT_NEAR(Length(Rotate(q, v)), Length(v), kTolerance);
}

TEST(QuaternionTest, ConjugateNegatesTheVectorPart) {
  const Quaternion c = Conjugate(Quaternion{0.5f, 0.1f, -0.2f, 0.3f});

  EXPECT_FLOAT_EQ(c.w, 0.5f);
  EXPECT_FLOAT_EQ(c.x, -0.1f);
  EXPECT_FLOAT_EQ(c.y, 0.2f);
  EXPECT_FLOAT_EQ(c.z, -0.3f);
}

TEST(QuaternionTest, NormalizedProducesUnitLength) {
  const Quaternion n = Normalized(Quaternion{2.0f, 1.0f, -2.0f, 4.0f});

  EXPECT_NEAR(Norm(n), 1.0f, kTolerance);
  EXPECT_NEAR(n.w, 0.4f, kTolerance);
}

TEST(QuaternionTest, NormalizedFallsBackToIdentityForZeroOrNonFinite) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  for (const Quaternion& bad :
       {Quaternion{0.0f, 0.0f, 0.0f, 0.0f}, Quaternion{nan, 0.0f, 0.0f, 0.0f},
        Quaternion{inf, 0.0f, 0.0f, 0.0f}}) {
    const Quaternion n = Normalized(bad);

    EXPECT_EQ(n.w, 1.0f);
    EXPECT_EQ(n.x, 0.0f);
    EXPECT_EQ(n.y, 0.0f);
    EXPECT_EQ(n.z, 0.0f);
  }
}

TEST(QuaternionTest, AxisAngleNormalizesTheAxis) {
  const Quaternion unit = QuaternionFromAxisAngle(kUp, 1.0f);
  const Quaternion scaled =
      QuaternionFromAxisAngle(Vec3{0.0f, 2.0f, 0.0f}, 1.0f);

  EXPECT_NEAR(scaled.w, unit.w, kTolerance);
  EXPECT_NEAR(scaled.y, unit.y, kTolerance);
  EXPECT_NEAR(Norm(scaled), 1.0f, kTolerance);
}

TEST(QuaternionTest, AxisAngleWithZeroAxisIsIdentity) {
  const Quaternion q = QuaternionFromAxisAngle(Vec3{}, 1.0f);

  EXPECT_EQ(q.w, 1.0f);
  EXPECT_EQ(q.y, 0.0f);
}

TEST(QuaternionTest, InverseRotateGivesListenerLocalCoordinates) {
  // A listener turned 90 degrees to the left now faces world -X.
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, kHalfPi);

  // A source at world -X is straight ahead of that listener.
  ExpectVec3Near(InverseRotate(yaw, Vec3{-5.0f, 0.0f, 0.0f}),
                 Vec3{0.0f, 0.0f, -5.0f});

  // A source at world -Z (ahead of an unturned listener) is now on its right.
  ExpectVec3Near(InverseRotate(yaw, Vec3{0.0f, 0.0f, -5.0f}),
                 Vec3{5.0f, 0.0f, 0.0f});
}

}  // namespace
}  // namespace lavanda
