#ifndef LAVANDA_RUNTIME_RENDER_CONTEXT_H_
#define LAVANDA_RUNTIME_RENDER_CONTEXT_H_

#include <cstdint>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/render_state.h"

namespace lavanda {

struct RenderContext {
  AudioBufferView output;
  double sample_rate_hz = 0.0;
  std::uint64_t clock_frame = 0;
  RenderState* render_state = nullptr;
};

}  // namespace lavanda

#endif
