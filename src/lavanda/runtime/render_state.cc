#include "lavanda/runtime/render_state.h"

#include <cmath>
#include <numbers>

namespace lavanda {

void RenderState::ApplyCommand(const Command& command) noexcept {
  switch (command.type) {
    case CommandType::kStartTone:
      is_playing_ = true;
      break;
    case CommandType::kStopTone:
      is_playing_ = false;
      break;
    case CommandType::kSetFrequency:
      frequency_hz_ = command.value;
      break;
    case CommandType::kSetGain:
      gain_ = command.value;
      break;
  }
}

void RenderState::Render(AudioBufferView output,
                         double sample_rate_hz) noexcept {
  if (!is_playing_ || sample_rate_hz <= 0.0 || output.empty()) {
    output.Clear();
    return;
  }

  const double phase_increment = 2.0 * std::numbers::pi *
                                 static_cast<double>(frequency_hz_) /
                                 sample_rate_hz;

  for (std::uint32_t frame = 0; frame < output.frame_count(); ++frame) {
    const float sample = static_cast<float>(std::sin(phase_)) * gain_;

    for (std::uint32_t channel = 0; channel < output.channel_count();
         ++channel) {
      output(frame, channel) = sample;
    }

    phase_ += phase_increment;
  }
}

}  // namespace lavanda
