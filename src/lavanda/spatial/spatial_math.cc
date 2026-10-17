#include "lavanda/spatial/spatial_math.h"

#include <algorithm>

#include "lavanda/spatial/quaternion.h"

namespace lavanda {

SpatialGeometry ComputeGeometry(const ListenerState& listener,
                                const EmitterState& emitter) noexcept {
  const Vec3 offset = emitter.position - listener.position;
  const float distance = Length(offset);

  SpatialGeometry geometry;

  if (!(distance >= kMinSpatialDistance)) {
    return geometry;
  }

  geometry.distance = distance;
  geometry.direction = offset * (1.0f / distance);

  const Vec3 local = InverseRotate(Normalized(listener.orientation), offset);

  geometry.pan = std::clamp(local.x / distance, -1.0f, 1.0f);

  return geometry;
}

float DistanceAttenuation(float distance,
                          const SpatialConfig& config) noexcept {
  const float reference = config.reference_distance;
  const float clamped =
      std::clamp(distance, reference, config.maximum_distance);

  return reference /
         (reference + config.rolloff_factor * (clamped - reference));
}

float DopplerRatio(const SpatialGeometry& geometry,
                   const ListenerState& listener, const EmitterState& emitter,
                   const SpatialConfig& config) noexcept {
  if (!(geometry.distance > 0.0f) || config.doppler_factor <= 0.0f) {
    return 1.0f;
  }

  const float c = config.speed_of_sound_mps;
  const float limit = kMaxDopplerRadialFraction * c;

  const float listener_toward = std::clamp(
      config.doppler_factor * Dot(listener.velocity, geometry.direction),
      -limit, limit);
  const float emitter_away = std::clamp(
      config.doppler_factor * Dot(emitter.velocity, geometry.direction), -limit,
      limit);
  const float ratio = (c + listener_toward) / (c + emitter_away);

  return std::clamp(ratio, kMinDopplerRatio, kMaxDopplerRatio);
}

std::uint32_t SmoothingFrames(const SpatialConfig& config,
                              double sample_rate_hz) noexcept {
  const double frames =
      static_cast<double>(config.smoothing_seconds) * sample_rate_hz;

  if (!(frames > 0.0) || !std::isfinite(frames)) {
    return 0;
  }

  constexpr long long kMaxFrames = std::numeric_limits<std::uint32_t>::max();

  return static_cast<std::uint32_t>(
      std::clamp<long long>(std::llround(frames), 0, kMaxFrames));
}

SpatialTarget ComputeSpatialTarget(const ListenerState& listener,
                                   const EmitterState& emitter,
                                   const SpatialConfig& config) noexcept {
  const SpatialGeometry geometry = ComputeGeometry(listener, emitter);

  SpatialTarget target;
  target.gains = EqualPowerPan(geometry.pan,
                               DistanceAttenuation(geometry.distance, config));
  target.playback_ratio = DopplerRatio(geometry, listener, emitter, config);

  return target;
}

}  // namespace lavanda
