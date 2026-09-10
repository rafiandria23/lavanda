#ifndef LAVANDA_RUNTIME_MIXER_MATH_H_
#define LAVANDA_RUNTIME_MIXER_MATH_H_

#include "lavanda/core/audio_buffer.h"

namespace lavanda {

struct StereoGains {
  float left = 1.0f;
  float right = 1.0f;
};

StereoGains EqualPowerPan(float pan, float gain) noexcept;

void AccumulateMonoToStereo(AudioBufferView destination, AudioBufferView source,
                            StereoGains gains) noexcept;

void AccumulateInto(AudioBufferView destination, AudioBufferView source,
                    float gain) noexcept;

void ApplyGain(AudioBufferView buffer, float gain) noexcept;

}  // namespace lavanda

#endif
