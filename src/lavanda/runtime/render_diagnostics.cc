#include "lavanda/runtime/render_diagnostics.h"

namespace lavanda {

void RenderDiagnostics::RecordRender(double duration_seconds,
                                     std::uint32_t frame_count,
                                     double sample_rate_hz) noexcept {
  render_count_.fetch_add(1, std::memory_order_relaxed);
  last_render_duration_seconds_.store(duration_seconds,
                                      std::memory_order_relaxed);
  last_callback_frame_count_.store(frame_count, std::memory_order_relaxed);

  if (duration_seconds >
      max_render_duration_seconds_.load(std::memory_order_relaxed)) {
    max_render_duration_seconds_.store(duration_seconds,
                                       std::memory_order_relaxed);
  }

  if (sample_rate_hz > 0.0) {
    const double budget_seconds =
        static_cast<double>(frame_count) / sample_rate_hz;

    if (duration_seconds > budget_seconds) {
      missed_deadline_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

RuntimeStats RenderDiagnostics::Snapshot() const noexcept {
  RuntimeStats stats;

  stats.render_count = render_count_.load(std::memory_order_relaxed);
  stats.missed_deadline_count =
      missed_deadline_count_.load(std::memory_order_relaxed);
  stats.last_render_duration_seconds =
      last_render_duration_seconds_.load(std::memory_order_relaxed);
  stats.max_render_duration_seconds =
      max_render_duration_seconds_.load(std::memory_order_relaxed);
  stats.last_callback_frame_count =
      last_callback_frame_count_.load(std::memory_order_relaxed);

  return stats;
}

}  // namespace lavanda
