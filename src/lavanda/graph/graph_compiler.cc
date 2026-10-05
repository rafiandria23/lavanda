#include "lavanda/graph/graph_compiler.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lavanda/graph/asset_pins.h"
#include "lavanda/resources/audio_asset.h"

namespace lavanda {

namespace {

bool ByIndex(const NodeId& a, const NodeId& b) { return a.index < b.index; }

Status BindRequiredAsset(AudioNode& node, AudioAssetId id,
                         const GraphCompileContext& context, AssetPins& pins) {
  if (context.resource_store == nullptr) {
    return Status(ErrorCode::kInvalidArgument,
                  "the graph uses an audio asset but no resource store was "
                  "provided to the compiler");
  }

  if (context.sample_rate_hz == 0) {
    return Status(ErrorCode::kNotOpen,
                  "audio asset nodes can only be compiled while the device "
                  "is open (after Start())");
  }

  const AudioAsset* asset = pins.Pin(id);

  if (asset == nullptr) {
    return Status(ErrorCode::kInvalidArgument,
                  "a node refers to an audio asset that is not available "
                  "(unknown, stale or released)");
  }

  if (asset->channel_count() != node.output_channel_count()) {
    return Status(ErrorCode::kInvalidArgument,
                  "an audio asset node's channel count does not match its "
                  "asset");
  }

  if (asset->sample_rate_hz() != context.sample_rate_hz) {
    return Status(ErrorCode::kInvalidArgument,
                  "an audio asset's sample rate does not match the device "
                  "rate");
  }

  node.BindAsset(asset);

  return Status::Ok();
}

}  // namespace

StatusOr<GraphExecutionPlan> GraphCompiler::Compile(
    const AudioGraph& graph, std::uint32_t max_frames_per_block,
    const GraphCompileContext& context) {
  const NodeId output = graph.output();

  if (!output.is_valid() || !graph.IsValidNode(output)) {
    return Status(ErrorCode::kInvalidArgument,
                  "graph has no valid output designated");
  }

  // --- Reachability ------------------------------------------------------

  std::unordered_set<std::uint32_t> reachable_indices;
  std::vector<NodeId> reachable;
  std::vector<NodeId> frontier{output};

  reachable_indices.insert(output.index);
  reachable.push_back(output);

  while (!frontier.empty()) {
    NodeId current = frontier.back();

    frontier.pop_back();

    for (const Connection& connection : graph.connections()) {
      if (connection.destination_node != current) {
        continue;
      }

      const NodeId& source = connection.source_node;

      if (reachable_indices.insert(source.index).second) {
        reachable.push_back(source);
        frontier.push_back(source);
      }
    }
  }

  std::sort(reachable.begin(), reachable.end(), ByIndex);

  // --- Required-input validation ------------------------------------------

  for (const NodeId& id : reachable) {
    AudioNode* node = graph.GetNode(id);
    const std::uint32_t expected = node->expected_input_count();

    for (std::uint32_t slot = 0; slot < expected; ++slot) {
      bool connected = false;

      for (const Connection& connection : graph.connections()) {
        if (connection.destination_node == id &&
            connection.destination_input_index == slot) {
          connected = true;
          break;
        }
      }

      if (!connected) {
        return Status(ErrorCode::kInvalidArgument,
                      "a node reachable from the graph's output has an "
                      "unconnected required input");
      }
    }
  }

  // --- Cycle detection + deterministic topological sort -------------------

  std::unordered_map<std::uint32_t, std::uint32_t> in_degree;
  std::unordered_map<std::uint32_t, std::vector<NodeId>> consumers;

  for (const NodeId& id : reachable) {
    in_degree[id.index] = 0;
  }

  for (const Connection& connection : graph.connections()) {
    if (reachable_indices.count(connection.source_node.index) == 0) {
      continue;
    }

    if (reachable_indices.count(connection.destination_node.index) == 0) {
      continue;
    }

    in_degree[connection.destination_node.index] += 1;
    consumers[connection.source_node.index].push_back(
        connection.destination_node);
  }

  std::set<NodeId, decltype(&ByIndex)> ready(&ByIndex);

  for (const NodeId& id : reachable) {
    if (in_degree[id.index] == 0) {
      ready.insert(id);
    }
  }

  std::vector<NodeId> order;
  order.reserve(reachable.size());

  while (!ready.empty()) {
    NodeId current = *ready.begin();
    ready.erase(ready.begin());

    order.push_back(current);

    for (const NodeId& consumer : consumers[current.index]) {
      std::uint32_t& degree = in_degree.at(consumer.index);
      degree -= 1;

      if (degree == 0) {
        ready.insert(consumer);
      }
    }
  }

  if (order.size() != reachable.size()) {
    return Status(ErrorCode::kInvalidArgument,
                  "the graph's output depends on a cycle -- feedback "
                  "loops are not supported in Phase 4");
  }

  // --- Buffer slot allocation (simple liveness-based reuse) --------------

  struct BufferSlotInfo {
    std::uint32_t channel_count;
  };

  std::vector<BufferSlotInfo> slot_info;
  std::vector<std::uint32_t> free_mono_slots;
  std::vector<std::uint32_t> free_stereo_slots;

  auto allocate_slot = [&](std::uint32_t channel_count) -> std::uint32_t {
    std::vector<std::uint32_t>& free_list =
        (channel_count <= 1) ? free_mono_slots : free_stereo_slots;

    if (!free_list.empty()) {
      const std::uint32_t slot = free_list.back();

      free_list.pop_back();

      return slot;
    }

    const std::uint32_t slot = static_cast<std::uint32_t>(slot_info.size());

    slot_info.push_back(BufferSlotInfo{channel_count});

    return slot;
  };

  auto release_slot = [&](std::uint32_t slot) {
    if (slot_info[slot].channel_count <= 1) {
      free_mono_slots.push_back(slot);
    } else {
      free_stereo_slots.push_back(slot);
    }
  };

  std::unordered_map<std::uint32_t, std::uint32_t> remaining_consumers;

  for (const NodeId& id : reachable) {
    remaining_consumers[id.index] =
        static_cast<std::uint32_t>(consumers[id.index].size());
  }

  AssetPins pins(context.resource_store);

  std::unordered_map<std::uint32_t, std::uint32_t> node_output_slot;
  std::vector<GraphExecutionPlan::Step> steps;

  steps.reserve(order.size());

  for (const NodeId& id : order) {
    AudioNode* source_node = graph.GetNode(id);

    GraphExecutionPlan::Step step;

    step.node = source_node->Clone();
    step.input_count = source_node->expected_input_count();

    const AudioAssetId required_asset = step.node->required_asset();

    if (required_asset.is_valid()) {
      Status bound =
          BindRequiredAsset(*step.node, required_asset, context, pins);

      if (!bound.ok()) {
        return bound;
      }
    }

    for (const Connection& connection : graph.connections()) {
      if (connection.destination_node != id) {
        continue;
      }

      const std::uint32_t dependency_slot =
          node_output_slot.at(connection.source_node.index);

      step.input_buffer_indices[connection.destination_input_index] =
          dependency_slot;
    }

    const std::uint32_t output_slot =
        allocate_slot(source_node->output_channel_count());

    step.output_buffer_index = output_slot;
    node_output_slot[id.index] = output_slot;

    steps.push_back(std::move(step));

    for (const Connection& connection : graph.connections()) {
      if (connection.destination_node != id) {
        continue;
      }

      const std::uint32_t dependency_index = connection.source_node.index;
      std::uint32_t& remaining = remaining_consumers.at(dependency_index);

      remaining -= 1;

      if (remaining == 0) {
        release_slot(node_output_slot.at(dependency_index));
      }
    }
  }

  std::vector<AudioBuffer> buffers;
  buffers.reserve(slot_info.size());

  for (const BufferSlotInfo& info : slot_info) {
    buffers.emplace_back(max_frames_per_block, info.channel_count);
  }

  const std::uint32_t output_buffer_index = node_output_slot.at(output.index);
  const std::uint32_t output_channel_count =
      graph.GetNode(output)->output_channel_count();

  return GraphExecutionPlan(std::move(steps), std::move(order),
                            std::move(buffers), output_buffer_index,
                            output_channel_count, max_frames_per_block,
                            std::move(pins));
}

}  // namespace lavanda
