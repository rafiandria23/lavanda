#ifndef LAVANDA_RUNTIME_RENDER_STATE_H_
#define LAVANDA_RUNTIME_RENDER_STATE_H_

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/command.h"

namespace lavanda {

class RenderState {
 public:
  RenderState() noexcept = default;

  void ApplyCommand(const Command& command) noexcept;

  void Render(AudioBufferView output, double sample_rate_hz) noexcept;

  bool is_playing() const noexcept { return is_playing_; }
  float frequency_hz() const noexcept { return frequency_hz_; }
  float gain() const noexcept { return gain_; }

 private:
  bool is_playing_ = false;
  float frequency_hz_ = 440.0f;
  float gain_ = 0.2f;
  double phase_ = 0.0;
};

}  // namespace lavanda

#endif
