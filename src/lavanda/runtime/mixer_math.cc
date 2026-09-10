#include "lavanda/runtime/mixer_math.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lavanda {

StereoGains EqualPowerPan(float pan, float gain) noexcept {
  const float clamped_pan = std::clamp(pan, -1.0f, 1.0f);
  const float angle = (clamped_pan + 1.0f) * (std::numbers::pi_v<float> / 4.0f);

  StereoGains gains;

  gains.left = std::cos(angle) * gain;
  gains.right = std::sin(angle) * gain;

  return gains;
}

void AccumulateMonoToStereo(AudioBufferView destination, AudioBufferView source,
                            StereoGains gains) noexcept {
  if (destination.empty() || source.empty()) {
    return;
  }

  const std::uint32_t frame_count = destination.frame_count();

  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    const float sample = source(frame, 0);

    destination(frame, 0) += sample * gains.left;
    destination(frame, 1) += sample * gains.right;
  }
}

void AccumulateInto(AudioBufferView destination, AudioBufferView source,
                    float gain) noexcept {
  if (destination.empty() || source.empty()) {
    return;
  }

  const std::uint32_t frame_count = destination.frame_count();
  const std::uint32_t channel_count = destination.channel_count();

  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
      destination(frame, channel) += source(frame, channel) * gain;
    }
  }
}

void ApplyGain(AudioBufferView buffer, float gain) noexcept {
  if (buffer.empty()) {
    return;
  }

  const std::uint32_t frame_count = buffer.frame_count();
  const std::uint32_t channel_count = buffer.channel_count();

  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
      buffer(frame, channel) *= gain;
    }
  }
}

}  // namespace lavanda
