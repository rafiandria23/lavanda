#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

#include "lavanda/core/status.h"
#include "lavanda/spatial/spatial_config.h"
#include "lavanda/spatial/spatial_state.h"

namespace lavanda {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

constexpr std::array<float, 3> kNonFinite = {kNaN, kInf, -kInf};

void ExpectInvalid(const Status& status) {
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

TEST(SpatialValidationTest, DefaultStatesAreValid) {
  EXPECT_TRUE(ValidateEmitterState(EmitterState{}).ok());
  EXPECT_TRUE(ValidateListenerState(ListenerState{}).ok());
}

TEST(SpatialValidationTest, RejectsNonFinitePositions) {
  for (float bad : kNonFinite) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      EmitterState emitter;
      ListenerState listener;
      std::array<float*, 3> emitter_axes = {
          &emitter.position.x, &emitter.position.y, &emitter.position.z};
      std::array<float*, 3> listener_axes = {
          &listener.position.x, &listener.position.y, &listener.position.z};

      *emitter_axes[axis] = bad;
      *listener_axes[axis] = bad;

      ExpectInvalid(ValidateEmitterState(emitter));
      ExpectInvalid(ValidateListenerState(listener));
    }
  }
}

TEST(SpatialValidationTest, RejectsNonFiniteVelocities) {
  for (float bad : kNonFinite) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      EmitterState emitter;
      ListenerState listener;
      std::array<float*, 3> emitter_axes = {
          &emitter.velocity.x, &emitter.velocity.y, &emitter.velocity.z};
      std::array<float*, 3> listener_axes = {
          &listener.velocity.x, &listener.velocity.y, &listener.velocity.z};

      *emitter_axes[axis] = bad;
      *listener_axes[axis] = bad;

      ExpectInvalid(ValidateEmitterState(emitter));
      ExpectInvalid(ValidateListenerState(listener));
    }
  }
}

TEST(SpatialValidationTest, AcceptsCoordinatesAtTheBoundAndRejectsBeyond) {
  EmitterState at_bound;
  at_bound.position = {kMaxSpatialCoordinate, -kMaxSpatialCoordinate, 0.0f};

  EXPECT_TRUE(ValidateEmitterState(at_bound).ok());

  EmitterState beyond = at_bound;
  beyond.position.x = kMaxSpatialCoordinate * 1.01f;

  ExpectInvalid(ValidateEmitterState(beyond));

  ListenerState listener;
  listener.position.z = -kMaxSpatialCoordinate * 1.01f;

  ExpectInvalid(ValidateListenerState(listener));
}

TEST(SpatialValidationTest, RejectsSpeedsBeyondTheBound) {
  EmitterState emitter;
  emitter.velocity.y = kMaxSpatialSpeed;

  EXPECT_TRUE(ValidateEmitterState(emitter).ok());

  emitter.velocity.y = kMaxSpatialSpeed * 1.01f;

  ExpectInvalid(ValidateEmitterState(emitter));
}

TEST(SpatialValidationTest, ListenerRejectsNonFiniteOrZeroOrientation) {
  ListenerState zero;
  zero.orientation = Quaternion{0.0f, 0.0f, 0.0f, 0.0f};

  ExpectInvalid(ValidateListenerState(zero));

  ListenerState tiny;
  tiny.orientation = Quaternion{kMinQuaternionNorm * 0.5f, 0.0f, 0.0f, 0.0f};

  ExpectInvalid(ValidateListenerState(tiny));

  for (float bad : kNonFinite) {
    ListenerState listener;
    listener.orientation.y = bad;

    ExpectInvalid(ValidateListenerState(listener));
  }
}

TEST(SpatialValidationTest, ListenerAcceptsNonUnitOrientation) {
  ListenerState listener;
  listener.orientation = Quaternion{2.0f, 0.0f, 0.0f, 0.0f};

  EXPECT_TRUE(ValidateListenerState(listener).ok());
}

TEST(SpatialValidationTest, DefaultConfigIsValid) {
  EXPECT_TRUE(ValidateSpatialConfig(SpatialConfig{}).ok());
}

TEST(SpatialValidationTest, ConfigRejectsBadSpeedOfSound) {
  for (float bad : {0.0f, -343.0f, kNaN, kInf}) {
    SpatialConfig config;
    config.speed_of_sound_mps = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }
}

TEST(SpatialValidationTest, ConfigRejectsBadDopplerFactor) {
  for (float bad : {-0.1f, kMaxDopplerFactor * 1.01f, kNaN, kInf}) {
    SpatialConfig config;
    config.doppler_factor = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }
}

TEST(SpatialValidationTest, ConfigRejectsBadDistances) {
  for (float bad : {0.0f, -1.0f, kNaN, kInf}) {
    SpatialConfig config;
    config.reference_distance = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }

  SpatialConfig below;
  below.reference_distance = 5.0f;
  below.maximum_distance = 4.0f;

  ExpectInvalid(ValidateSpatialConfig(below));

  for (float bad : {kNaN, kInf}) {
    SpatialConfig config;
    config.maximum_distance = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }
}

TEST(SpatialValidationTest, ConfigRejectsBadRolloff) {
  for (float bad : {-0.1f, kMaxRolloffFactor * 1.01f, kNaN, kInf}) {
    SpatialConfig config;
    config.rolloff_factor = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }
}

TEST(SpatialValidationTest, ConfigRejectsBadSmoothing) {
  for (float bad : {-0.001f, kMaxSmoothingSeconds * 1.01f, kNaN, kInf}) {
    SpatialConfig config;
    config.smoothing_seconds = bad;

    ExpectInvalid(ValidateSpatialConfig(config));
  }
}

TEST(SpatialValidationTest, ConfigAcceptsBoundaryValues) {
  SpatialConfig config;

  config.doppler_factor = 0.0f;
  config.rolloff_factor = 0.0f;
  config.smoothing_seconds = 0.0f;
  config.maximum_distance = config.reference_distance;

  EXPECT_TRUE(ValidateSpatialConfig(config).ok());

  config.doppler_factor = kMaxDopplerFactor;
  config.rolloff_factor = kMaxRolloffFactor;
  config.smoothing_seconds = kMaxSmoothingSeconds;

  EXPECT_TRUE(ValidateSpatialConfig(config).ok());
}

}  // namespace
}  // namespace lavanda
