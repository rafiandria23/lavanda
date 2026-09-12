#ifndef LAVANDA_GRAPH_GRAPH_H_
#define LAVANDA_GRAPH_GRAPH_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "lavanda/core/status.h"
#include "lavanda/graph/graph_config.h"
#include "lavanda/graph/node.h"
#include "lavanda/graph/node_id.h"

namespace lavanda {

struct Connection {
  NodeId source_node;
  NodeId destination_node;
  std::uint32_t destination_input_index = 0;
};

class AudioGraph {
 public:
  explicit AudioGraph(GraphConfig config = GraphConfig());

  AudioGraph(const AudioGraph&) = delete;
  AudioGraph& operator=(const AudioGraph&) = delete;

  const GraphConfig& config() const noexcept { return config_; }

  StatusOr<NodeId> AddNode(std::unique_ptr<AudioNode> node);

  Status RemoveNode(NodeId id);

  Status Connect(NodeId source, NodeId destination,
                 std::uint32_t destination_input_index);

  Status Disconnect(NodeId destination, std::uint32_t destination_input_index);

  Status SetOutput(NodeId id);

  NodeId output() const noexcept { return output_; }

  bool IsValidNode(NodeId id) const noexcept;
  AudioNode* GetNode(NodeId id) const noexcept;

  const std::vector<Connection>& connections() const noexcept {
    return connections_;
  }

  std::vector<NodeId> AllNodeIds() const;

 private:
  struct Slot {
    std::unique_ptr<AudioNode> node;
    std::uint32_t generation = 0;
    bool in_use = false;
  };

  GraphConfig config_;
  std::vector<Slot> slots_;
  std::vector<Connection> connections_;
  NodeId output_;
};

}  // namespace lavanda

#endif
