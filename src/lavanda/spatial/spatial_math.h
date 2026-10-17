#ifndef LAVANDA_SPATIAL_SPATIAL_MATH_H_
#define LAVANDA_SPATIAL_SPATIAL_MATH_H_

#include <cstdint>

#include "lavanda/runtime/mixer_math.h"
#include "lavanda/spatial/spatial_config.h"
#include "lavanda/spatial/spatial_state.h"
#include "lavanda/spatial/vec3.h"

namespace lavanda {

inline constexpr float kMinSpatialDistance = 1.0e-6f;

inline constexpr float kMinDopplerRatio = 0.5f;
inline constexpr float kMaxDopplerRatio = 2.0f;

inline constexpr float kMaxDopplerRadialFraction = 0.9f;

struct SpatialGeometry {
  float distance = 0.0f;
  float pan = 0.0f;
  Vec3 direction;
};

struct SpatialTarget {
  StereoGains gains;
  float playback_ratio = 1.0f;
};

SpatialGeometry ComputeGeometry(const ListenerState& listener,
                                const EmitterState& emitter) noexcept;

float DistanceAttenuation(float distance, const SpatialConfig& config) noexcept;

float DopplerRatio(const SpatialGeometry& geometry,
                   const ListenerState& listener, const EmitterState& emitter,
                   const SpatialConfig& config) noexcept;

std::uint32_t SmoothingFrames(const SpatialConfig& config,
                              double sample_rate_hz) noexcept;

SpatialTarget ComputeSpatialTarget(const ListenerState& listener,
                                   const EmitterState& emitter,
                                   const SpatialConfig& config) noexcept;

}  // namespace lavanda

#endif
