#ifndef LAVANDA_RUNTIME_RUNTIME_STATS_H_
#define LAVANDA_RUNTIME_RUNTIME_STATS_H_

#include <cstdint>

namespace lavanda {

struct RuntimeStats {
  std::uint64_t render_count = 0;

  std::uint64_t missed_deadline_count = 0;

  double last_render_duration_seconds = 0.0;

  double max_render_duration_seconds = 0.0;

  std::uint32_t last_callback_frame_count = 0;
};

}  // namespace lavanda

#endif
