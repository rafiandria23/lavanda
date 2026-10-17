#include "lavanda/spatial/spatial_oscillator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lavanda {

void RenderDopplerSine(AudioBufferView output, double& phase,
                       float base_frequency_hz, const GainRamp& playback_ratio,
                       std::uint64_t first_frame,
                       double sample_rate_hz) noexcept {
  if (output.empty() || !(sample_rate_hz > 0.0)) {
    output.Clear();
    return;
  }

  const double nyquist = 0.5 * sample_rate_hz;
  const double base = static_cast<double>(base_frequency_hz);

  for (std::uint32_t frame = 0; frame < output.frame_count(); ++frame) {
    const double ratio =
        static_cast<double>(playback_ratio.ValueAt(first_frame + frame));
    const double frequency = std::min(base * ratio, nyquist);

    output(frame, 0) = static_cast<float>(std::sin(phase));
    phase += 2.0 * std::numbers::pi * frequency / sample_rate_hz;
  }
}

}  // namespace lavanda
