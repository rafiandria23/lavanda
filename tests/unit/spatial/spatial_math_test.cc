#include "lavanda/spatial/spatial_math.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/spatial_config.h"
#include "lavanda/spatial/spatial_state.h"
#include "lavanda/spatial/vec3.h"

namespace lavanda {
namespace {

constexpr float kTolerance = 1.0e-5f;
constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
constexpr float kCentreGain = 0.70710678f;  // cos(pi/4)

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kBackAxis{0.0f, 0.0f, 1.0f};  // roll axis (listener's -forward)

ListenerState ListenerAt(const Vec3& position,
                         const Quaternion& orientation = Quaternion{},
                         const Vec3& velocity = Vec3{}) {
  return ListenerState{position, orientation, velocity};
}

EmitterState EmitterAt(const Vec3& position, const Vec3& velocity = Vec3{}) {
  return EmitterState{position, velocity};
}

SpatialGeometry GeometryOf(const Vec3& source,
                           const Quaternion& orientation = Quaternion{}) {
  return ComputeGeometry(ListenerAt(Vec3{}, orientation), EmitterAt(source));
}

float RatioFor(const ListenerState& listener, const EmitterState& emitter,
               const SpatialConfig& config = SpatialConfig{}) {
  return DopplerRatio(ComputeGeometry(listener, emitter), listener, emitter,
                      config);
}

// --- attenuation -------------------------------------------------------------

TEST(SpatialMathTest, AttenuationIsUnityInsideReferenceDistance) {
  const SpatialConfig config;

  EXPECT_FLOAT_EQ(DistanceAttenuation(0.0f, config), 1.0f);
  EXPECT_FLOAT_EQ(DistanceAttenuation(0.5f, config), 1.0f);
  EXPECT_FLOAT_EQ(DistanceAttenuation(1.0f, config), 1.0f);
}

TEST(SpatialMathTest, AttenuationFollowsInverseLawBeyondReference) {
  const SpatialConfig config;

  EXPECT_NEAR(DistanceAttenuation(2.0f, config), 0.5f, kTolerance);
  EXPECT_NEAR(DistanceAttenuation(4.0f, config), 0.25f, kTolerance);
  EXPECT_NEAR(DistanceAttenuation(10.0f, config), 0.1f, kTolerance);
}

TEST(SpatialMathTest, AttenuationScalesWithRolloffAndReference) {
  SpatialConfig steep;
  steep.rolloff_factor = 2.0f;

  EXPECT_NEAR(DistanceAttenuation(2.0f, steep), 1.0f / 3.0f, kTolerance);

  SpatialConfig shallow;
  shallow.rolloff_factor = 0.5f;

  EXPECT_NEAR(DistanceAttenuation(3.0f, shallow), 0.5f, kTolerance);

  SpatialConfig wide;
  wide.reference_distance = 2.0f;

  EXPECT_NEAR(DistanceAttenuation(4.0f, wide), 0.5f, kTolerance);
  EXPECT_FLOAT_EQ(DistanceAttenuation(1.5f, wide), 1.0f);
}

TEST(SpatialMathTest, AttenuationHoldsItsFloorBeyondMaximumDistance) {
  const SpatialConfig config;  // reference 1, maximum 100, rolloff 1
  const float at_max = DistanceAttenuation(100.0f, config);

  EXPECT_NEAR(at_max, 0.01f, kTolerance);
  EXPECT_FLOAT_EQ(DistanceAttenuation(1000.0f, config), at_max);
  EXPECT_FLOAT_EQ(DistanceAttenuation(1.0e7f, config), at_max);
}

TEST(SpatialMathTest, AttenuationNeverIncreasesWithDistance) {
  const SpatialConfig config;
  float previous = DistanceAttenuation(0.0f, config);

  for (float distance = 0.1f; distance < 200.0f; distance += 0.37f) {
    const float gain = DistanceAttenuation(distance, config);

    EXPECT_LE(gain, previous) << "distance " << distance;
    EXPECT_GT(gain, 0.0f);

    previous = gain;
  }
}

TEST(SpatialMathTest, ZeroRolloffDisablesAttenuation) {
  SpatialConfig config;
  config.rolloff_factor = 0.0f;

  EXPECT_FLOAT_EQ(DistanceAttenuation(50.0f, config), 1.0f);
}

// --- geometry ----------------------------------------------------------------

TEST(SpatialMathTest, CoincidentPositionsAreNeutral) {
  const ListenerState listener =
      ListenerAt(Vec3{2.0f, 3.0f, 4.0f}, Quaternion{}, Vec3{5.0f, 0.0f, 0.0f});
  const EmitterState emitter =
      EmitterAt(Vec3{2.0f, 3.0f, 4.0f}, Vec3{-20.0f, 0.0f, 0.0f});

  const SpatialGeometry geometry = ComputeGeometry(listener, emitter);

  EXPECT_EQ(geometry.distance, 0.0f);
  EXPECT_EQ(geometry.pan, 0.0f);
  EXPECT_EQ(geometry.direction.x, 0.0f);
  EXPECT_EQ(geometry.direction.y, 0.0f);
  EXPECT_EQ(geometry.direction.z, 0.0f);

  const SpatialTarget target =
      ComputeSpatialTarget(listener, emitter, SpatialConfig{});

  EXPECT_NEAR(target.gains.left, kCentreGain, kTolerance);
  EXPECT_NEAR(target.gains.right, kCentreGain, kTolerance);
  EXPECT_FLOAT_EQ(target.playback_ratio, 1.0f);
}

TEST(SpatialMathTest, NearlyCoincidentPositionsAreNeutral) {
  const SpatialGeometry geometry = GeometryOf(Vec3{1.0e-7f, 0.0f, 0.0f});

  EXPECT_EQ(geometry.distance, 0.0f);
  EXPECT_EQ(geometry.pan, 0.0f);
}

struct PanCase {
  Vec3 source;
  float distance;
  float pan;
};

constexpr std::array<PanCase, 9> kPanCases = {{
    {Vec3{0.0f, 0.0f, -5.0f}, 5.0f, 0.0f},    // ahead
    {Vec3{0.0f, 0.0f, 5.0f}, 5.0f, 0.0f},     // behind
    {Vec3{0.0f, 5.0f, 0.0f}, 5.0f, 0.0f},     // above
    {Vec3{0.0f, -5.0f, 0.0f}, 5.0f, 0.0f},    // below
    {Vec3{5.0f, 0.0f, 0.0f}, 5.0f, 1.0f},     // right
    {Vec3{-5.0f, 0.0f, 0.0f}, 5.0f, -1.0f},   // left
    {Vec3{3.0f, 0.0f, -4.0f}, 5.0f, 0.6f},    // ahead-right
    {Vec3{-3.0f, 0.0f, -4.0f}, 5.0f, -0.6f},  // ahead-left
    {Vec3{3.0f, 4.0f, 0.0f}, 5.0f, 0.6f}  // elevation does not change pan law
}};

TEST(SpatialMathTest, PanFollowsListenerLocalDirection) {
  for (std::size_t index = 0; index < kPanCases.size(); ++index) {
    SCOPED_TRACE(index);

    const PanCase& c = kPanCases[index];
    const SpatialGeometry geometry = GeometryOf(c.source);

    EXPECT_NEAR(geometry.distance, c.distance, kTolerance);
    EXPECT_NEAR(geometry.pan, c.pan, kTolerance);
  }
}

TEST(SpatialMathTest, DirectionIsAUnitVectorFromListenerToEmitterInWorldSpace) {
  for (std::size_t index = 0; index < kPanCases.size(); ++index) {
    SCOPED_TRACE(index);

    const PanCase& c = kPanCases[index];
    const SpatialGeometry geometry = GeometryOf(c.source);

    EXPECT_NEAR(Length(geometry.direction), 1.0f, kTolerance);
    EXPECT_NEAR(geometry.direction.x, c.source.x / 5.0f, kTolerance);
    EXPECT_NEAR(geometry.direction.y, c.source.y / 5.0f, kTolerance);
    EXPECT_NEAR(geometry.direction.z, c.source.z / 5.0f, kTolerance);
  }
}

TEST(SpatialMathTest, YawedListenerReinterpretsLeftAndRight) {
  // Facing world -X.
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, kHalfPi);

  EXPECT_NEAR(GeometryOf(Vec3{-5.0f, 0.0f, 0.0f}, yaw).pan, 0.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{0.0f, 0.0f, -5.0f}, yaw).pan, 1.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{0.0f, 0.0f, 5.0f}, yaw).pan, -1.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{5.0f, 0.0f, 0.0f}, yaw).pan, 0.0f, kTolerance);
}

TEST(SpatialMathTest, RolledListenerMovesTheRightAxis) {
  // Rolled so the listener's right ear points to world +Y.
  const Quaternion roll = QuaternionFromAxisAngle(kBackAxis, kHalfPi);

  EXPECT_NEAR(GeometryOf(Vec3{0.0f, 5.0f, 0.0f}, roll).pan, 1.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{0.0f, -5.0f, 0.0f}, roll).pan, -1.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{5.0f, 0.0f, 0.0f}, roll).pan, 0.0f, kTolerance);
  EXPECT_NEAR(GeometryOf(Vec3{0.0f, 0.0f, -5.0f}, roll).pan, 0.0f, kTolerance);
}

TEST(SpatialMathTest, NonUnitOrientationBehavesLikeUnit) {
  const Quaternion unit = QuaternionFromAxisAngle(kUp, kHalfPi);
  const Quaternion scaled{unit.w * 3.0f, unit.x * 3.0f, unit.y * 3.0f,
                          unit.z * 3.0f};
  const Vec3 source{0.0f, 0.0f, -5.0f};

  EXPECT_NEAR(GeometryOf(source, scaled).pan, GeometryOf(source, unit).pan,
              kTolerance);
}

TEST(SpatialMathTest, ZeroOrientationFallsBackToIdentity) {
  const Quaternion zero{0.0f, 0.0f, 0.0f, 0.0f};

  EXPECT_NEAR(GeometryOf(Vec3{5.0f, 0.0f, 0.0f}, zero).pan, 1.0f, kTolerance);
}

TEST(SpatialMathTest, GeometryIsTranslationInvariant) {
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, 0.8f);
  const Vec3 shift{100.0f, -50.0f, 30.0f};
  const Vec3 source{3.0f, 4.0f, -12.0f};

  const SpatialGeometry base =
      ComputeGeometry(ListenerAt(Vec3{}, yaw), EmitterAt(source));
  const SpatialGeometry moved =
      ComputeGeometry(ListenerAt(shift, yaw), EmitterAt(source + shift));

  EXPECT_NEAR(moved.distance, base.distance, kTolerance);
  EXPECT_NEAR(moved.pan, base.pan, kTolerance);
  EXPECT_NEAR(moved.direction.x, base.direction.x, kTolerance);
  EXPECT_NEAR(moved.direction.y, base.direction.y, kTolerance);
  EXPECT_NEAR(moved.direction.z, base.direction.z, kTolerance);
}

// --- target ------------------------------------------------------------------

SpatialTarget TargetFor(const Vec3& source) {
  return ComputeSpatialTarget(ListenerAt(Vec3{}), EmitterAt(source),
                              SpatialConfig{});
}

TEST(SpatialMathTest, TargetPansHardRightAndHardLeft) {
  const SpatialTarget right = TargetFor(Vec3{5.0f, 0.0f, 0.0f});

  EXPECT_NEAR(right.gains.left, 0.0f, 1.0e-6f);
  EXPECT_NEAR(right.gains.right, 0.2f, 1.0e-6f);

  const SpatialTarget left = TargetFor(Vec3{-5.0f, 0.0f, 0.0f});

  EXPECT_NEAR(left.gains.left, 0.2f, 1.0e-6f);
  EXPECT_NEAR(left.gains.right, 0.0f, 1.0e-6f);
}

TEST(SpatialMathTest, TargetCentresSourcesAheadBehindAboveAndBelow) {
  const float expected = 0.2f * kCentreGain;

  for (const Vec3& source : {Vec3{0.0f, 0.0f, -5.0f}, Vec3{0.0f, 0.0f, 5.0f},
                             Vec3{0.0f, 5.0f, 0.0f}, Vec3{0.0f, -5.0f, 0.0f}}) {
    const SpatialTarget target = TargetFor(source);

    EXPECT_NEAR(target.gains.left, expected, kTolerance);
    EXPECT_NEAR(target.gains.right, expected, kTolerance);
  }
}

TEST(SpatialMathTest, TargetIsEqualPower) {
  struct Case {
    Vec3 source;
    float attenuation;
  };

  for (const Case& c :
       {Case{Vec3{3.0f, 0.0f, -4.0f}, 0.2f}, Case{Vec3{5.0f, 0.0f, 0.0f}, 0.2f},
        Case{Vec3{-3.0f, 4.0f, 0.0f}, 0.2f},
        Case{Vec3{12.0f, 0.0f, -16.0f}, 0.05f}}) {
    const SpatialTarget target = TargetFor(c.source);
    const float power = target.gains.left * target.gains.left +
                        target.gains.right * target.gains.right;

    EXPECT_NEAR(power, c.attenuation * c.attenuation, 1.0e-6f);
  }
}

TEST(SpatialMathTest, TargetMirrorsLeftAndRight) {
  const SpatialTarget a = TargetFor(Vec3{3.0f, 2.0f, -4.0f});
  const SpatialTarget b = TargetFor(Vec3{-3.0f, 2.0f, -4.0f});

  EXPECT_NEAR(a.gains.left, b.gains.right, 1.0e-6f);
  EXPECT_NEAR(a.gains.right, b.gains.left, 1.0e-6f);
}

TEST(SpatialMathTest, TargetCarriesPanAttenuationAndDopplerTogether) {
  const SpatialTarget target = ComputeSpatialTarget(
      ListenerAt(Vec3{}),
      EmitterAt(Vec3{0.0f, 0.0f, -5.0f}, Vec3{0.0f, 0.0f, -34.3f}),
      SpatialConfig{});

  EXPECT_NEAR(target.gains.left, 0.2f * kCentreGain, kTolerance);
  EXPECT_NEAR(target.gains.right, 0.2f * kCentreGain, kTolerance);
  EXPECT_NEAR(target.playback_ratio, 0.9090909f, kTolerance);
}

// --- doppler -----------------------------------------------------------------

constexpr Vec3 kAhead10{0.0f, 0.0f, -10.0f};

TEST(SpatialMathTest, DopplerIsNeutralWithoutRadialMotion) {
  EXPECT_FLOAT_EQ(RatioFor(ListenerAt(Vec3{}), EmitterAt(kAhead10)), 1.0f);

  // Purely perpendicular motion has no radial component.
  EXPECT_FLOAT_EQ(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 50.0f, 0.0f}),
               EmitterAt(kAhead10, Vec3{30.0f, 0.0f, 0.0f})),
      1.0f);
}

TEST(SpatialMathTest, DopplerLowersPitchForARecedingEmitter) {
  // Emitter ahead (-Z) moving further along -Z.
  EXPECT_NEAR(RatioFor(ListenerAt(Vec3{}),
                       EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -34.3f})),
              0.9090909f, kTolerance);

  // Same situation mirrored behind the listener.
  EXPECT_NEAR(RatioFor(ListenerAt(Vec3{}), EmitterAt(Vec3{0.0f, 0.0f, 10.0f},
                                                     Vec3{0.0f, 0.0f, 34.3f})),
              0.9090909f, kTolerance);

  // Off-axis: velocity along the line of sight of a 6-8-10 triangle.
  EXPECT_NEAR(RatioFor(ListenerAt(Vec3{}),
                       EmitterAt(Vec3{6.0f, 0.0f, -8.0f},
                                 Vec3{0.6f * 34.3f, 0.0f, -0.8f * 34.3f})),
              0.9090909f, kTolerance);
}

TEST(SpatialMathTest, DopplerRaisesPitchForAnApproachingEmitter) {
  EXPECT_NEAR(RatioFor(ListenerAt(Vec3{}),
                       EmitterAt(kAhead10, Vec3{0.0f, 0.0f, 34.3f})),
              1.1111111f, kTolerance);
}

TEST(SpatialMathTest, DopplerFollowsListenerMotion) {
  // Listener moving towards the emitter hears a higher pitch.
  EXPECT_NEAR(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 0.0f, -34.3f}),
               EmitterAt(kAhead10)),
      1.1f, kTolerance);

  // Listener moving away hears a lower pitch.
  EXPECT_NEAR(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 0.0f, 34.3f}),
               EmitterAt(kAhead10)),
      0.9f, kTolerance);
}

TEST(SpatialMathTest, DopplerCombinesBothMotions) {
  // Closing on each other.
  EXPECT_NEAR(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 0.0f, -34.3f}),
               EmitterAt(kAhead10, Vec3{0.0f, 0.0f, 34.3f})),
      1.2222222f, kTolerance);

  // Moving together at the same velocity: no relative motion, no shift.
  EXPECT_FLOAT_EQ(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 0.0f, -34.3f}),
               EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -34.3f})),
      1.0f);
}

TEST(SpatialMathTest, DopplerFactorScalesTheShift) {
  const ListenerState listener = ListenerAt(Vec3{});
  const EmitterState emitter = EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -34.3f});

  SpatialConfig off;
  off.doppler_factor = 0.0f;

  EXPECT_FLOAT_EQ(RatioFor(listener, emitter, off), 1.0f);

  SpatialConfig doubled;
  doubled.doppler_factor = 2.0f;

  EXPECT_NEAR(RatioFor(listener, emitter, doubled), 0.8333333f, kTolerance);
}

TEST(SpatialMathTest, DopplerUsesTheConfiguredSpeedOfSound) {
  SpatialConfig slow;
  slow.speed_of_sound_mps = 100.0f;

  EXPECT_NEAR(RatioFor(ListenerAt(Vec3{}),
                       EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -10.0f}), slow),
              0.9090909f, kTolerance);
}

TEST(SpatialMathTest, DopplerIgnoresListenerOrientation) {
  const EmitterState emitter = EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -34.3f});
  const Quaternion yaw = QuaternionFromAxisAngle(kUp, 1.1f);

  EXPECT_FLOAT_EQ(RatioFor(ListenerAt(Vec3{}), emitter),
                  RatioFor(ListenerAt(Vec3{}, yaw), emitter));
}

TEST(SpatialMathTest, DopplerClampsASupersonicApproach) {
  // Radial term clamps to -0.9c, the raw ratio would be 10; the ceiling is 2.
  EXPECT_FLOAT_EQ(RatioFor(ListenerAt(Vec3{}),
                           EmitterAt(kAhead10, Vec3{0.0f, 0.0f, 1000.0f})),
                  kMaxDopplerRatio);
}

TEST(SpatialMathTest, DopplerClampsAtTheFloor) {
  // Listener and emitter both running away at supersonic speed.
  EXPECT_FLOAT_EQ(
      RatioFor(ListenerAt(Vec3{}, Quaternion{}, Vec3{0.0f, 0.0f, 1000.0f}),
               EmitterAt(kAhead10, Vec3{0.0f, 0.0f, -1000.0f})),
      kMinDopplerRatio);
}

// --- sweep -------------------------------------------------------------------

TEST(SpatialMathTest, SweepOfExtremeInputsStaysFiniteAndBounded) {
  constexpr std::array<float, 6> kCoords = {0.0f,  1.0e-7f, 1.0f,
                                            -5.0f, 1.0e6f,  -1.0e6f};
  const std::array<Vec3, 2> listener_positions = {
      Vec3{}, Vec3{1.0e6f, -1.0e6f, 1.0e6f}};
  const std::array<Quaternion, 3> orientations = {
      Quaternion{}, QuaternionFromAxisAngle(Vec3{1.0f, 2.0f, 3.0f}, 1.0f),
      Quaternion{3.0f, 0.0f, 0.0f, 0.0f}};
  const std::array<Vec3, 2> velocities = {Vec3{},
                                          Vec3{1.0e5f, -1.0e5f, 1.0e5f}};

  SpatialConfig extreme;

  extreme.doppler_factor = kMaxDopplerFactor;
  extreme.rolloff_factor = kMaxRolloffFactor;
  extreme.reference_distance = 0.01f;
  extreme.maximum_distance = 0.01f;

  const std::array<SpatialConfig, 2> configs = {SpatialConfig{}, extreme};

  int failures = 0;
  int calls = 0;

  for (float x : kCoords) {
    for (float y : kCoords) {
      for (float z : kCoords) {
        for (const Vec3& listener_position : listener_positions) {
          for (const Quaternion& orientation : orientations) {
            for (const Vec3& listener_velocity : velocities) {
              for (const Vec3& emitter_velocity : velocities) {
                for (const SpatialConfig& config : configs) {
                  const ListenerState listener = ListenerAt(
                      listener_position, orientation, listener_velocity);
                  const EmitterState emitter =
                      EmitterAt(Vec3{x, y, z}, emitter_velocity);

                  const SpatialTarget target =
                      ComputeSpatialTarget(listener, emitter, config);
                  const SpatialGeometry geometry =
                      ComputeGeometry(listener, emitter);

                  const bool ok = std::isfinite(target.gains.left) &&
                                  std::isfinite(target.gains.right) &&
                                  std::isfinite(target.playback_ratio) &&
                                  std::isfinite(geometry.pan) &&
                                  std::isfinite(geometry.distance) &&
                                  target.gains.left >= -1.0e-6f &&
                                  target.gains.left <= 1.0f + 1.0e-6f &&
                                  target.gains.right >= -1.0e-6f &&
                                  target.gains.right <= 1.0f + 1.0e-6f &&
                                  target.playback_ratio >= kMinDopplerRatio &&
                                  target.playback_ratio <= kMaxDopplerRatio &&
                                  geometry.pan >= -1.0f && geometry.pan <= 1.0f;

                  if (!ok) {
                    ++failures;
                  }

                  ++calls;
                }
              }
            }
          }
        }
      }
    }
  }

  EXPECT_EQ(calls, 216 * 2 * 3 * 2 * 2 * 2);
  EXPECT_EQ(failures, 0);
}

}  // namespace
}  // namespace lavanda
