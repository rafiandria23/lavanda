#include "lavanda/graph/graph.h"

#include <algorithm>
#include <utility>

namespace lavanda {

AudioGraph::AudioGraph(GraphConfig config) : config_(config) {}

StatusOr<NodeId> AudioGraph::AddNode(std::unique_ptr<AudioNode> node) {
  if (node == nullptr) {
    return Status(ErrorCode::kInvalidArgument, "node must not be null");
  }

  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (!slots_[i].in_use) {
      slots_[i].node = std::move(node);
      slots_[i].in_use = true;
      slots_[i].generation += 1;

      NodeId id;

      id.index = static_cast<std::uint32_t>(i);
      id.generation = slots_[i].generation;

      return id;
    }
  }

  if (slots_.size() >= config_.max_nodes) {
    return Status(ErrorCode::kResourceExhausted,
                  "graph is at max_nodes capacity");
  }

  slots_.push_back(Slot{});

  Slot& slot = slots_.back();

  slot.node = std::move(node);
  slot.in_use = true;
  slot.generation = 1;

  NodeId id;

  id.index = static_cast<std::uint32_t>(slots_.size() - 1);
  id.generation = slot.generation;

  return id;
}

Status AudioGraph::RemoveNode(NodeId id) {
  if (!IsValidNode(id)) {
    return Status::Ok();
  }

  slots_[id.index].in_use = false;
  slots_[id.index].node.reset();

  connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
                                    [&](const Connection& connection) {
                                      return connection.source_node == id ||
                                             connection.destination_node == id;
                                    }),
                     connections_.end());

  if (output_ == id) {
    output_ = NodeId();
  }

  return Status::Ok();
}

Status AudioGraph::Connect(NodeId source, NodeId destination,
                           std::uint32_t destination_input_index) {
  if (!IsValidNode(source) || !IsValidNode(destination)) {
    return Status(ErrorCode::kInvalidArgument,
                  "source or destination node id is invalid");
  }

  if (source == destination) {
    return Status(ErrorCode::kInvalidArgument,
                  "self-connections are not allowed");
  }

  AudioNode* destination_node = GetNode(destination);

  if (destination_input_index >= destination_node->expected_input_count()) {
    return Status(
        ErrorCode::kInvalidArgument,
        "destination_input_index exceeds the node's expected input count");
  }

  for (const Connection& existing : connections_) {
    if (existing.destination_node == destination &&
        existing.destination_input_index == destination_input_index) {
      return Status(ErrorCode::kInvalidArgument,
                    "destination input slot already has a connection --"
                    "Disconnect it first");
    }
  }

  AudioNode* source_node = GetNode(source);

  if (source_node->output_channel_count() !=
      destination_node->input_channel_count()) {
    return Status(ErrorCode::kInvalidArgument,
                  "source output channel count does not match destination "
                  "input channel count");
  }

  if (connections_.size() >= config_.max_connections) {
    return Status(ErrorCode::kResourceExhausted,
                  "graph is at max_connections capacity");
  }

  connections_.push_back(
      Connection{source, destination, destination_input_index});

  return Status::Ok();
}

Status AudioGraph::Disconnect(NodeId destination,
                              std::uint32_t destination_input_index) {
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [&](const Connection& connection) {
                       return connection.destination_node == destination &&
                              connection.destination_input_index ==
                                  destination_input_index;
                     }),
      connections_.end());

  return Status::Ok();
}

Status AudioGraph::SetOutput(NodeId id) {
  if (!IsValidNode(id)) {
    return Status(ErrorCode::kInvalidArgument, "node id is invalid");
  }

  output_ = id;

  return Status::Ok();
}

bool AudioGraph::IsValidNode(NodeId id) const noexcept {
  if (id.index >= slots_.size()) {
    return false;
  }

  const Slot& slot = slots_[id.index];

  return slot.in_use && slot.generation == id.generation;
}

AudioNode* AudioGraph::GetNode(NodeId id) const noexcept {
  if (!IsValidNode(id)) {
    return nullptr;
  }

  return slots_[id.index].node.get();
}

std::vector<NodeId> AudioGraph::AllNodeIds() const {
  std::vector<NodeId> ids;

  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].in_use) {
      NodeId id;

      id.index = static_cast<std::uint32_t>(i);
      id.generation = slots_[i].generation;

      ids.push_back(id);
    }
  }

  return ids;
}

}  // namespace lavanda
