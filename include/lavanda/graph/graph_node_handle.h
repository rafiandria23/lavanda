#ifndef LAVANDA_GRAPH_GRAPH_NODE_HANDLE_H_
#define LAVANDA_GRAPH_GRAPH_NODE_HANDLE_H_

#include "lavanda/core/status.h"
#include "lavanda/graph/node_id.h"

namespace lavanda {

class AudioRuntime;

class GraphNodeHandle {
 public:
  GraphNodeHandle() noexcept = default;
  GraphNodeHandle(AudioRuntime* runtime, NodeId id) noexcept
      : runtime_(runtime), id_(id) {}

  bool is_valid() const noexcept {
    return runtime_ != nullptr && id_.is_valid();
  }
  NodeId id() const noexcept { return id_; }

  Status SetGain(float gain);
  Status SetFrequency(float frequency_hz);
  Status SetCutoffHz(float cutoff_hz);
  Status SetPan(float pan);
  Status SetDelayFrames(float delay_frames);

 private:
  AudioRuntime* runtime_ = nullptr;
  NodeId id_;
};

}  // namespace lavanda

#endif
