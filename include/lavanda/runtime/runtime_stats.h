#ifndef LAVANDA_RUNTIME_RUNTIME_STATS_H_
#define LAVANDA_RUNTIME_RUNTIME_STATS_H_

#include <cstddef>
#include <cstdint>

namespace lavanda {

struct RuntimeStats {
  std::uint64_t render_count = 0;
  std::uint64_t missed_deadline_count = 0;
  double last_render_duration_seconds = 0.0;
  double max_render_duration_seconds = 0.0;
  std::uint32_t last_callback_frame_count = 0;
  std::uint32_t active_voice_count = 0;
  std::uint32_t active_bus_count = 0;
  std::uint64_t voice_creation_failures = 0;
  std::uint64_t bus_creation_failures = 0;
  std::uint64_t graph_compilation_failures = 0;
  std::uint64_t graph_activation_count = 0;
  std::uint32_t active_graph_node_count = 0;
  std::uint32_t active_graph_plan_generation = 0;
  std::uint64_t command_failures = 0;
  std::size_t resident_asset_count = 0;
  std::size_t retiring_asset_count = 0;
  std::uint64_t resident_asset_bytes = 0;
  std::uint64_t asset_load_failures = 0;
};

}  // namespace lavanda

#endif
