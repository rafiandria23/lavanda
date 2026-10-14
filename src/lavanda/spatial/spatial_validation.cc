#include <cmath>

#include "lavanda/spatial/spatial_config.h"
#include "lavanda/spatial/spatial_state.h"

namespace lavanda {
namespace {

Status Invalid(const char* message) {
  return Status(ErrorCode::kInvalidArgument, message);
}

bool IsBounded(float value, float limit) noexcept {
  return std::isfinite(value) && std::fabs(value) <= limit;
}

bool IsBounded(const Vec3& v, float limit) noexcept {
  return IsBounded(v.x, limit) && IsBounded(v.y, limit) &&
         IsBounded(v.z, limit);
}

}  // namespace

Status ValidateEmitterState(const EmitterState& state) {
  if (!IsBounded(state.position, kMaxSpatialCoordinate)) {
    return Invalid("emitter position must be finite and within +/-1e6 m");
  }

  if (!IsBounded(state.velocity, kMaxSpatialSpeed)) {
    return Invalid("emitter velocity must be finite and within +/-1e5 m/s");
  }

  return Status::Ok();
}

Status ValidateListenerState(const ListenerState& state) {
  if (!IsBounded(state.position, kMaxSpatialCoordinate)) {
    return Invalid("listener position must be finite and within +/-1e6 m");
  }

  if (!IsBounded(state.velocity, kMaxSpatialSpeed)) {
    return Invalid("listener velocity must be finite and within +/-1e5 m/s");
  }

  const Quaternion& q = state.orientation;

  if (!IsBounded(q.w, kMaxQuaternionComponent) ||
      !IsBounded(q.x, kMaxQuaternionComponent) ||
      !IsBounded(q.y, kMaxQuaternionComponent) ||
      !IsBounded(q.z, kMaxQuaternionComponent)) {
    return Invalid("listener orientation components must be finite");
  }

  if (Norm(q) < kMinQuaternionNorm) {
    return Invalid("listener orientation must not be (near) zero");
  }

  return Status::Ok();
}

Status ValidateSpatialConfig(const SpatialConfig& config) {
  if (!std::isfinite(config.speed_of_sound_mps) ||
      config.speed_of_sound_mps <= 0.0f) {
    return Invalid("speed of sound must be finite and positive");
  }

  if (!std::isfinite(config.doppler_factor) || config.doppler_factor < 0.0f ||
      config.doppler_factor > kMaxDopplerFactor) {
    return Invalid("doppler factor must be finite and within [0, 10]");
  }

  if (!std::isfinite(config.reference_distance) ||
      config.reference_distance <= 0.0f) {
    return Invalid("reference distance must be finite and positive");
  }

  if (!std::isfinite(config.maximum_distance) ||
      config.maximum_distance < config.reference_distance) {
    return Invalid(
        "maximum distance must be finite and not below the reference "
        "distance");
  }

  if (!std::isfinite(config.rolloff_factor) || config.rolloff_factor < 0.0f ||
      config.rolloff_factor > kMaxRolloffFactor) {
    return Invalid("rolloff factor must be finite and within [0, 100]");
  }

  if (!std::isfinite(config.smoothing_seconds) ||
      config.smoothing_seconds < 0.0f ||
      config.smoothing_seconds > kMaxSmoothingSeconds) {
    return Invalid("smoothing must be finite and within [0, 1] seconds");
  }

  return Status::Ok();
}

}  // namespace lavanda
