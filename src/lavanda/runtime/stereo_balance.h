#ifndef LAVANDA_RUNTIME_STEREO_BALANCE_H_
#define LAVANDA_RUNTIME_STEREO_BALANCE_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "lavanda/core/audio_buffer.h"

namespace lavanda {

struct BalanceGains {
  float left = 0.0f;
  float right = 0.0f;
};

inline BalanceGains StereoBalanceGains(float pan, float gain) noexcept {
  const float clamped = std::clamp(pan, -1.0f, 1.0f);
  constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;

  const float left =
      clamped > 0.0f ? std::max(0.0f, std::cos(clamped * kHalfPi)) : 1.0f;
  const float right =
      clamped < 0.0f ? std::max(0.0f, std::cos(-clamped * kHalfPi)) : 1.0f;

  return BalanceGains{gain * left, gain * right};
}

inline void AccumulateStereoWithBalance(AudioBufferView destination,
                                        AudioBufferView source,
                                        BalanceGains gains) noexcept {
  if (destination.channel_count() != 2 || source.channel_count() != 2) {
    return;
  }

  const std::uint32_t frames =
      std::min(destination.frame_count(), source.frame_count());

  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    destination(frame, 0) += source(frame, 0) * gains.left;
    destination(frame, 1) += source(frame, 1) * gains.right;
  }
}

}  // namespace lavanda

#endif
