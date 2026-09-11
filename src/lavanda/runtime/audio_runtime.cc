#include "lavanda/runtime/audio_runtime.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include "lavanda/runtime/audio_clock.h"
#include "lavanda/runtime/bus_system.h"
#include "lavanda/runtime/command_queue.h"
#include "lavanda/runtime/mixer_math.h"
#include "lavanda/runtime/render_context.h"
#include "lavanda/runtime/render_diagnostics.h"
#include "lavanda/runtime/render_state.h"
#include "lavanda/runtime/voice_pool.h"

namespace lavanda {

class AudioRuntime::Impl {
 public:
  Impl(std::unique_ptr<AudioDevice> device, const RuntimeConfig& config)
      : device_(std::move(device)),
        command_queue_(config.command_queue_capacity),
        voice_pool_(config.max_voices),
        bus_system_(config.max_user_buses, config.max_frames_per_block),
        mix_scratch_(static_cast<std::uint32_t>(config.max_frames_per_block),
                     1) {}

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
      diagnostics_.RecordCommandFailure();
      return Status(ErrorCode::kQueueFull, "command queue is at capacity");
    }

    return Status::Ok();
  }

  bool is_running() const noexcept { return device_ && device_->is_running(); }

  RuntimeStats stats() const noexcept { return diagnostics_.Snapshot(); }

  StatusOr<VoiceId> ReserveVoice() { return voice_pool_.ReserveSlot(); }
  void ReleaseVoice(VoiceId id) noexcept { voice_pool_.ReleaseSlot(id); }

  StatusOr<BusId> ReserveBus() { return bus_system_.ReserveSlot(); }
  void ReleaseBus(BusId id) noexcept { bus_system_.ReleaseSlot(id); }

  void RecordVoiceCreationFailure() noexcept {
    diagnostics_.RecordVoiceCreationFailure();
  }
  void RecordBusCreationFailure() noexcept {
    diagnostics_.RecordBusCreationFailure();
  }

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
      voice_pool_.ApplyCommand(command);
      bus_system_.ApplyCommand(command);
    }

    RenderContext context;

    context.output = output;
    context.sample_rate_hz = device_->format().sample_rate_hz();
    context.clock_frame = clock_.current_frame();
    context.render_state = &render_state_;

    context.render_state->Render(context.output, context.sample_rate_hz);
    clock_.Advance(output.frame_count());

    RenderVoicesAndBuses(output);

    const auto render_end = std::chrono::steady_clock::now();
    const double duration_seconds =
        std::chrono::duration<double>(render_end - render_start).count();

    diagnostics_.RecordRender(duration_seconds, output.frame_count(),
                              context.sample_rate_hz);
  }

  void RenderVoicesAndBuses(AudioBufferView output) noexcept {
    if (output.channel_count() != 2) {
      return;
    }

    if (output.frame_count() > bus_system_.max_frames_per_block()) {
      return;
    }

    bus_system_.ClearActiveAccumulators(output.frame_count());

    AudioBufferView mono_scratch = mix_scratch_.View(output.frame_count());
    const double sample_rate_hz = device_->format().sample_rate_hz();
    const std::size_t voice_count = voice_pool_.capacity();
    std::uint32_t active_voice_count = 0;

    for (std::size_t i = 0; i < voice_count; ++i) {
      if (!voice_pool_.is_active(i)) {
        continue;
      }

      ++active_voice_count;

      voice_pool_.RenderVoiceSource(i, mono_scratch, sample_rate_hz);

      StereoGains gains =
          EqualPowerPan(voice_pool_.pan(i), voice_pool_.gain(i));
      const std::size_t target_index =
          bus_system_.ResolveBusIndex(voice_pool_.target_bus(i));
      AudioBufferView bus_accumulator =
          bus_system_.MutableAccumulator(target_index, output.frame_count());

      AccumulateMonoToStereo(bus_accumulator, mono_scratch, gains);
    }

    AudioBufferView master_accumulator =
        bus_system_.MutableAccumulator(0, output.frame_count());
    const std::size_t bus_total = bus_system_.total_capacity();
    std::uint32_t active_bus_count = 1;

    for (std::size_t b = 1; b < bus_total; ++b) {
      if (!bus_system_.is_active(b)) {
        continue;
      }

      ++active_bus_count;

      AudioBufferView bus_accumulator =
          bus_system_.MutableAccumulator(b, output.frame_count());

      AccumulateInto(master_accumulator, bus_accumulator, bus_system_.gain(b));
    }

    ApplyGain(master_accumulator, bus_system_.gain(0));
    AccumulateInto(output, master_accumulator, 1.0f);

    diagnostics_.SetActiveCounts(active_voice_count, active_bus_count);
  }

  std::unique_ptr<AudioDevice> device_;
  CommandQueue command_queue_;
  RenderState render_state_;
  AudioClock clock_;
  RenderDiagnostics diagnostics_;
  VoicePool voice_pool_;
  BusSystem bus_system_;
  AudioBuffer mix_scratch_;
  std::atomic<bool> is_active_{false};
};

AudioRuntime::AudioRuntime(std::unique_ptr<AudioDevice> device,
                           RuntimeConfig config)
    : impl_(std::make_unique<Impl>(std::move(device), config)) {}

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

StatusOr<Voice> AudioRuntime::CreateVoice() {
  StatusOr<VoiceId> id_or = impl_->ReserveVoice();

  if (!id_or.ok()) {
    impl_->RecordVoiceCreationFailure();
    return id_or.status();
  }
  VoiceId id = id_or.value();
  Status submit_status =
      Submit({.type = CommandType::kCreateVoice, .voice_id = id});

  if (!submit_status.ok()) {
    impl_->ReleaseVoice(id);
    impl_->RecordVoiceCreationFailure();

    return submit_status;
  }

  return Voice(this, id);
}

StatusOr<Bus> AudioRuntime::CreateBus() {
  StatusOr<BusId> id_or = impl_->ReserveBus();

  if (!id_or.ok()) {
    impl_->RecordBusCreationFailure();
    return id_or.status();
  }

  BusId id = id_or.value();
  Status submit_status =
      Submit({.type = CommandType::kCreateBus, .bus_id = id});

  if (!submit_status.ok()) {
    impl_->ReleaseBus(id);
    impl_->RecordBusCreationFailure();

    return submit_status;
  }

  return Bus(this, id);
}

Bus AudioRuntime::MasterBus() noexcept { return Bus(this, kMasterBusId); }

void AudioRuntime::ReleaseVoiceReservation(VoiceId id) noexcept {
  impl_->ReleaseVoice(id);
}

void AudioRuntime::ReleaseBusReservation(BusId id) noexcept {
  impl_->ReleaseBus(id);
}

}  // namespace lavanda
