#include "lavanda/graph/graph_node_handle.h"

#include <cmath>

#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"

namespace lavanda {

Status GraphNodeHandle::SetGain(float gain) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "GraphNodeHandle is not valid");
  }

  if (!std::isfinite(gain) || gain < 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "node gain must be finite and non-negative");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetNodeGain, .value = gain, .node_id = id_});
}

Status GraphNodeHandle::SetFrequency(float frequency_hz) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "GraphNodeHandle is not valid");
  }

  if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "node frequency must be finite and positive");
  }

  return runtime_->Submit({.type = CommandType::kSetNodeFrequency,
                           .value = frequency_hz,
                           .node_id = id_});
}

Status GraphNodeHandle::SetCutoffHz(float cutoff_hz) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "GraphNodeHandle is not valid");
  }

  if (!std::isfinite(cutoff_hz) || cutoff_hz <= 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "node cutoff must be finite and positive");
  }

  return runtime_->Submit({.type = CommandType::kSetNodeCutoff,
                           .value = cutoff_hz,
                           .node_id = id_});
}

Status GraphNodeHandle::SetPan(float pan) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "GraphNodeHandle is not valid");
  }

  if (!std::isfinite(pan)) {
    return Status(ErrorCode::kInvalidArgument, "node pan must be finite");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetNodePan, .value = pan, .node_id = id_});
}

Status GraphNodeHandle::SetDelayFrames(float delay_frames) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "GraphNodeHandle is not valid");
  }

  if (!std::isfinite(delay_frames) || delay_frames < 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "node delay must be finite and non-negative");
  }

  return runtime_->Submit({.type = CommandType::kSetNodeDelayFrames,
                           .value = delay_frames,
                           .node_id = id_});
}

}  // namespace lavanda
