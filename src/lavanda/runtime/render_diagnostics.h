#ifndef LAVANDA_RUNTIME_RENDER_DIAGNOSTICS_H_
#define LAVANDA_RUNTIME_RENDER_DIAGNOSTICS_H_

#include <atomic>
#include <cstdint>

#include "lavanda/runtime/runtime_stats.h"

namespace lavanda {

static_assert(std::atomic<double>::is_always_lock_free,
              "RenderDiagnostics requires lock-free double atomics on this "
              "platform to remain real-time safe");

class RenderDiagnostics {
 public:
  RenderDiagnostics() noexcept = default;

  RenderDiagnostics(const RenderDiagnostics&) = delete;
  RenderDiagnostics& operator=(const RenderDiagnostics&) = delete;

  void RecordRender(double duration_seconds, std::uint32_t frame_count,
                    double sample_rate_hz) noexcept;

  RuntimeStats Snapshot() const noexcept;

 private:
  std::atomic<std::uint64_t> render_count_{0};
  std::atomic<std::uint64_t> missed_deadline_count_{0};
  std::atomic<double> last_render_duration_seconds_{0.0};
  std::atomic<double> max_render_duration_seconds_{0.0};
  std::atomic<std::uint32_t> last_callback_frame_count_{0};
};

}  // namespace lavanda

#endif
