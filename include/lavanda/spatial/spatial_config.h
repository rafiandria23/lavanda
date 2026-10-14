#ifndef LAVANDA_SPATIAL_SPATIAL_CONFIG_H_
#define LAVANDA_SPATIAL_SPATIAL_CONFIG_H_

#include "lavanda/core/status.h"

namespace lavanda {

inline constexpr float kMaxDopplerFactor = 10.0f;
inline constexpr float kMaxRolloffFactor = 100.0f;
inline constexpr float kMaxSmoothingSeconds = 1.0f;

// Runtime-wide, immutable spatialization settings.
struct SpatialConfig {
  float speed_of_sound_mps = 343.0f;
  float doppler_factor = 1.0f;  // 0 disables Doppler, 1 is the standard model
  float reference_distance =
      1.0f;  // metres; attenuation is 1.0 at or inside it
  float maximum_distance = 100.0f;   // metres; attenuation stops falling here
  float rolloff_factor = 1.0f;       // 0 disables distance attenuation
  float smoothing_seconds = 0.010f;  // 0 applies spatial changes as steps
};

Status ValidateSpatialConfig(const SpatialConfig& config);

}  // namespace lavanda

#endif
