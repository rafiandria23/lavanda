#ifndef LAVANDA_RUNTIME_MIXER_MATH_H_
#define LAVANDA_RUNTIME_MIXER_MATH_H_

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/gain_ramp.h"

namespace lavanda {

struct StereoGains {
  float left = 1.0f;
  float right = 1.0f;
};

StereoGains EqualPowerPan(float pan, float gain) noexcept;

void AccumulateMonoToStereo(AudioBufferView destination, AudioBufferView source,
                            StereoGains gains) noexcept;
void AccumulateMonoToStereoRamped(AudioBufferView destination,
                                  AudioBufferView source,
                                  StereoGains unit_gains, const GainRamp& ramp,
                                  std::uint64_t first_frame) noexcept;

void AccumulateInto(AudioBufferView destination, AudioBufferView source,
                    float gain) noexcept;
void AccumulateIntoRamped(AudioBufferView destination, AudioBufferView source,
                          const GainRamp& ramp,
                          std::uint64_t first_frame) noexcept;

void ApplyGain(AudioBufferView buffer, float gain) noexcept;
void ApplyGainRamped(AudioBufferView buffer, const GainRamp& ramp,
                     std::uint64_t first_frame) noexcept;

}  // namespace lavanda

#endif
