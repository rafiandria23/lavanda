#include "lavanda/runtime/audio_runtime.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include "lavanda/runtime/audio_clock.h"
#include "lavanda/runtime/command_queue.h"
#include "lavanda/runtime/render_context.h"
#include "lavanda/runtime/render_diagnostics.h"
#include "lavanda/runtime/render_state.h"

namespace lavanda {

class AudioRuntime::Impl {
 public:
  Impl(std::unique_ptr<AudioDevice> device, std::size_t command_queue_capacity)
      : device_(std::move(device)), command_queue_(command_queue_capacity) {}

  Status Start(const DeviceConfig& config) {
    if (!device_) {
      return Status(ErrorCode::kDeviceUnavailable,
                    "AudioRuntime was constructed with a null device");
    }

    if (device_->is_running()) {
      return Status(ErrorCode::kAlreadyOpen, "AudioRuntime is already running");
    }

    if (!device_->is_open()) {
      Status open_status = device_->Open(
          config, [this](AudioBufferView output) { Render(output); });

      if (!open_status.ok()) {
        return open_status;
      }

      clock_.SetSampleRate(device_->format().sample_rate_hz());
    }

    is_active_.store(true, std::memory_order_release);

    Status start_status = device_->Start();

    if (!start_status.ok()) {
      is_active_.store(false, std::memory_order_release);
    }

    return start_status;
  }

  Status Stop() {
    if (!device_) {
      return Status(ErrorCode::kDeviceUnavailable,
                    "AudioRuntime was constructed with a null device");
    }

    is_active_.store(false, std::memory_order_release);

    return device_->Stop();
  }

  Status Shutdown() {
    if (!device_) {
      return Status(ErrorCode::kDeviceUnavailable,
                    "AudioRuntime was constructed with a null device");
    }

    is_active_.store(false, std::memory_order_release);

    Status stop_status = device_->Stop();
    Status close_status = device_->Close();

    return stop_status.ok() ? close_status : stop_status;
  }

  Status Submit(const Command& command) {
    if (!device_) {
      return Status(ErrorCode::kDeviceUnavailable,
                    "AudioRuntime was constructed with a null device");
    }

    if (!command_queue_.TryPush(command)) {
      return Status(ErrorCode::kQueueFull, "command queue is at capacity");
    }

    return Status::Ok();
  }

  bool is_running() const noexcept { return device_ && device_->is_running(); }

  RuntimeStats stats() const noexcept { return diagnostics_.Snapshot(); }

 private:
  void Render(AudioBufferView output) noexcept {
    if (!is_active_.load(std::memory_order_acquire)) {
      output.Clear();
      return;
    }

    const auto render_start = std::chrono::steady_clock::now();

    Command command;

    while (command_queue_.TryPop(command)) {
      render_state_.ApplyCommand(command);
    }

    RenderContext context;

    context.output = output;
    context.sample_rate_hz = device_->format().sample_rate_hz();
    context.clock_frame = clock_.current_frame();
    context.render_state = &render_state_;

    context.render_state->Render(context.output, context.sample_rate_hz);
    clock_.Advance(output.frame_count());

    const auto render_end = std::chrono::steady_clock::now();
    const double duration_seconds =
        std::chrono::duration<double>(render_end - render_start).count();

    diagnostics_.RecordRender(duration_seconds, output.frame_count(),
                              context.sample_rate_hz);
  }

  std::unique_ptr<AudioDevice> device_;
  CommandQueue command_queue_;
  RenderState render_state_;
  AudioClock clock_;
  RenderDiagnostics diagnostics_;
  std::atomic<bool> is_active_{false};
};

AudioRuntime::AudioRuntime(std::unique_ptr<AudioDevice> device,
                           std::size_t command_queue_capacity)
    : impl_(std::make_unique<Impl>(std::move(device), command_queue_capacity)) {
}

AudioRuntime::~AudioRuntime() {
  if (impl_) {
    static_cast<void>(impl_->Shutdown());
  }
}

Status AudioRuntime::Start(const DeviceConfig& config) {
  return impl_->Start(config);
}

Status AudioRuntime::Stop() { return impl_->Stop(); }

Status AudioRuntime::Shutdown() { return impl_->Shutdown(); }

Status AudioRuntime::Submit(const Command& command) {
  return impl_->Submit(command);
}

bool AudioRuntime::is_running() const noexcept { return impl_->is_running(); }

RuntimeStats AudioRuntime::stats() const noexcept { return impl_->stats(); }

}  // namespace lavanda
