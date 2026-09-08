#include "test_support/deterministic_signal.h"

namespace lavanda::test_support {

void FillWithRamp(AudioBufferView view) {
  float counter = 0.0f;

  for (std::uint32_t frame = 0; frame < view.frame_count(); ++frame) {
    for (std::uint32_t channel = 0; channel < view.channel_count(); ++channel) {
      view(frame, channel) = counter;
      counter += 1.0f;
    }
  }
}

}  // namespace lavanda::test_support
