#ifndef LAVANDA_SPATIAL_SPATIAL_OSCILLATOR_H_
#define LAVANDA_SPATIAL_SPATIAL_OSCILLATOR_H_

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/gain_ramp.h"

namespace lavanda {

void RenderDopplerSine(AudioBufferView output, double& phase,
                       float base_frequency_hz, const GainRamp& playback_ratio,
                       std::uint64_t first_frame,
                       double sample_rate_hz) noexcept;

}  // namespace lavanda

#endif
