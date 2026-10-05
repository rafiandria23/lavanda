#include "lavanda/runtime/audio_runtime.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/graph/graph_executor.h"
#include "lavanda/graph/graph_plan_store.h"
#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/one_pole_high_pass_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/resources/resource_loader.h"
#include "lavanda/resources/resource_store.h"
#include "lavanda/runtime/audio_clock.h"
#include "lavanda/runtime/bus_system.h"
#include "lavanda/runtime/command_queue.h"
#include "lavanda/runtime/mixer_math.h"
#include "lavanda/runtime/render_context.h"
#include "lavanda/runtime/render_diagnostics.h"
#include "lavanda/runtime/render_state.h"
#include "lavanda/runtime/stereo_balance.h"
#include "lavanda/runtime/voice_pool.h"

namespace lavanda {

namespace {

void ApplyNodeParameterCommand(AudioNode* node, CommandType type,
                               float value) noexcept {
  switch (type) {
    case CommandType::kSetNodeGain:
      if (auto* gain = dynamic_cast<GainNode*>(node)) {
        gain->SetGain(value);
      } else if (auto* osc = dynamic_cast<OscillatorNode*>(node)) {
        osc->SetGain(value);
      }
      break;
    case CommandType::kSetNodeFrequency:
      if (auto* osc = dynamic_cast<OscillatorNode*>(node)) {
        osc->SetFrequency(value);
      }
      break;
    case CommandType::kSetNodeCutoff:
      if (auto* low_pass = dynamic_cast<OnePoleLowPassNode*>(node)) {
        low_pass->SetCutoffHz(value);
      } else if (auto* high_pass = dynamic_cast<OnePoleHighPassNode*>(node)) {
        high_pass->SetCutoffHz(value);
      }
      break;
    case CommandType::kSetNodePan:
      if (auto* pan = dynamic_cast<PanNode*>(node)) {
        pan->SetPan(value);
      }
      break;
    case CommandType::kSetNodeDelayFrames:
      if (auto* delay = dynamic_cast<DelayNode*>(node)) {
        delay->SetDelayFrames(static_cast<std::uint32_t>(value));
      }
      break;
    default:
      break;
  }
}

}  // namespace

class AudioRuntime::Impl {
 public:
  Impl(std::unique_ptr<AudioDevice> device, const RuntimeConfig& config)
      : device_(std::move(device)),
        command_queue_(config.command_queue_capacity),
        resource_store_(config.resource_config),
        resource_loader_(resource_store_, config.resource_config),
        voice_pool_(config.max_voices, &resource_store_),
        bus_system_(config.max_user_buses, config.max_frames_per_block),
        mix_scratch_(static_cast<std::uint32_t>(config.max_frames_per_block),
                     1),
        stereo_scratch_(static_cast<std::uint32_t>(config.max_frames_per_block),
                        2),
        graph_(config.graph_config),
        graph_plan_store_(config.max_graph_plans),
        max_frames_per_block_(
            static_cast<std::uint32_t>(config.max_frames_per_block)) {}

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
  RuntimeStats stats() const noexcept {
    RuntimeStats snapshot = diagnostics_.Snapshot();

    snapshot.resident_asset_count = resource_store_.resident_count();
    snapshot.retiring_asset_count = resource_store_.retiring_count();
    snapshot.resident_asset_bytes = resource_store_.total_bytes();

    return snapshot;
  }

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

  AudioGraph& graph() noexcept { return graph_; }

  StatusOr<GraphPlanHandle> CompileAndStageGraph() {
    StatusOr<GraphPlanHandle> handle_or =
        graph_plan_store_.BuildAndStage(graph_, max_frames_per_block_);

    if (!handle_or.ok()) {
      diagnostics_.RecordGraphCompilationFailure();
    }

    return handle_or;
  }

  void MarkGraphActivationSubmitted(GraphPlanHandle handle) noexcept {
    graph_plan_store_.MarkActivationSubmitted(handle);
  }

  bool ReleaseStagedGraphPlan(GraphPlanHandle handle) noexcept {
    return graph_plan_store_.ReleaseStagedPlan(handle);
  }

  StatusOr<AudioAssetId> LoadAudioAsset(const std::string& path) {
    StatusOr<std::uint32_t> rate = DeviceSampleRateHz();

    if (!rate.ok()) {
      diagnostics_.RecordAssetLoadFailure();
      return rate.status();
    }

    StatusOr<AudioAssetId> id = resource_loader_.Load(path, rate.value());

    if (!id.ok()) {
      diagnostics_.RecordAssetLoadFailure();
    }

    return id;
  }

  StatusOr<AudioAssetInfo> GetAudioAssetInfo(AudioAssetId id) const {
    return resource_store_.GetInfo(id);
  }

  Status ReleaseAudioAsset(AudioAssetId id) {
    return resource_store_.Release(id);
  }

  Status ValidateAssetForVoice(AudioAssetId id) const {
    if (!id.is_valid()) {
      return Status(ErrorCode::kInvalidArgument, "invalid audio asset id");
    }

    StatusOr<AudioAssetInfo> info = resource_store_.GetInfo(id);

    if (!info.ok()) {
      return info.status();
    }

    StatusOr<std::uint32_t> rate = DeviceSampleRateHz();

    if (!rate.ok()) {
      return rate.status();
    }

    if (info.value().sample_rate_hz != rate.value()) {
      return Status(ErrorCode::kInvalidArgument,
                    "audio asset sample rate does not match the device rate");
    }

    return Status::Ok();
  }

  bool PinAsset(AudioAssetId id) { return resource_store_.Pin(id) != nullptr; }
  void UnpinAsset(AudioAssetId id) noexcept { resource_store_.Unpin(id); }

  bool IsVoicePlaying(VoiceId id) const noexcept {
    return voice_pool_.observed_state(id) == VoiceState::kPlaying;
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

      if (command.type == CommandType::kActivateGraphPlan) {
        if (graph_plan_store_.TryActivate(command.plan_handle)) {
          GraphExecutionPlan* activated = graph_plan_store_.ActivePlan();

          if (activated != nullptr) {
            diagnostics_.RecordGraphActivation(
                activated->step_count(), graph_plan_store_.ActiveGeneration());
          }
        }
      } else {
        GraphExecutionPlan* active_plan = graph_plan_store_.ActivePlan();

        if (active_plan != nullptr) {
          const std::uint32_t step_index =
              active_plan->FindStepIndexForNode(command.node_id);

          if (step_index < active_plan->step_count()) {
            ApplyNodeParameterCommand(
                active_plan->steps()[step_index].node.get(), command.type,
                command.value);
          }
        }
      }
    }

    RenderContext context;

    context.output = output;
    context.sample_rate_hz = device_->format().sample_rate_hz();
    context.clock_frame = clock_.current_frame();
    context.render_state = &render_state_;

    context.render_state->Render(context.output, context.sample_rate_hz);
    clock_.Advance(output.frame_count());

    RenderVoicesAndBuses(output);

    GraphExecutionPlan* active_plan = graph_plan_store_.ActivePlan();

    if (active_plan != nullptr) {
      GraphExecutor::Render(*active_plan, output, output.frame_count(),
                            context.sample_rate_hz, context.clock_frame);
    }

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
    AudioBufferView stereo_scratch = stereo_scratch_.View(output.frame_count());
    const double sample_rate_hz = device_->format().sample_rate_hz();
    const std::size_t voice_count = voice_pool_.capacity();
    std::uint32_t active_voice_count = 0;

    for (std::size_t i = 0; i < voice_count; ++i) {
      if (!voice_pool_.is_active(i)) {
        continue;
      }

      ++active_voice_count;

      const std::size_t target_index =
          bus_system_.ResolveBusIndex(voice_pool_.target_bus(i));
      AudioBufferView bus_accumulator =
          bus_system_.MutableAccumulator(target_index, output.frame_count());

      if (voice_pool_.source_channel_count(i) == 2) {
        voice_pool_.RenderVoiceSource(i, stereo_scratch, sample_rate_hz);

        AccumulateStereoWithBalance(
            bus_accumulator, stereo_scratch,
            StereoBalanceGains(voice_pool_.pan(i), voice_pool_.gain(i)));
      } else {
        voice_pool_.RenderVoiceSource(i, mono_scratch, sample_rate_hz);

        StereoGains gains =
            EqualPowerPan(voice_pool_.pan(i), voice_pool_.gain(i));

        AccumulateMonoToStereo(bus_accumulator, mono_scratch, gains);
      }
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

  StatusOr<std::uint32_t> DeviceSampleRateHz() const {
    if (!device_ || !device_->is_open()) {
      return Status(ErrorCode::kNotOpen,
                    "audio assets can only be loaded while the device is open "
                    "(after Start()): they are prepared at the device's "
                    "sample rate");
    }

    const double rate = device_->format().sample_rate_hz();
    const long rounded = std::lround(rate);

    if (rounded < 1 || rounded > static_cast<long>(kMaxAssetSampleRateHz)) {
      return Status(ErrorCode::kUnsupportedFormat,
                    "device sample rate " + std::to_string(rate) +
                        " Hz is outside the supported asset range");
    }

    return static_cast<std::uint32_t>(rounded);
  }

  std::unique_ptr<AudioDevice> device_;
  CommandQueue command_queue_;
  RenderState render_state_;
  AudioClock clock_;
  RenderDiagnostics diagnostics_;

  ResourceStore resource_store_;
  ResourceLoader resource_loader_;

  VoicePool voice_pool_;
  BusSystem bus_system_;
  AudioBuffer mix_scratch_;
  AudioBuffer stereo_scratch_;
  AudioGraph graph_;

  GraphPlanStore graph_plan_store_;
  std::uint32_t max_frames_per_block_;
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

StatusOr<Voice> AudioRuntime::CreateVoice(AudioAssetId asset) {
  Status check = impl_->ValidateAssetForVoice(asset);

  if (!check.ok()) {
    impl_->RecordVoiceCreationFailure();
    return check;
  }

  StatusOr<VoiceId> id_or = impl_->ReserveVoice();

  if (!id_or.ok()) {
    impl_->RecordVoiceCreationFailure();
    return id_or.status();
  }

  VoiceId id = id_or.value();

  if (!impl_->PinAsset(asset)) {
    impl_->ReleaseVoice(id);
    impl_->RecordVoiceCreationFailure();

    return Status(ErrorCode::kInvalidArgument,
                  "audio asset is not available (stale or released)");
  }

  Status submit_status = Submit(
      {.type = CommandType::kCreateVoice, .voice_id = id, .asset_id = asset});

  if (!submit_status.ok()) {
    impl_->UnpinAsset(asset);
    impl_->ReleaseVoice(id);
    impl_->RecordVoiceCreationFailure();

    return submit_status;
  }

  return Voice(this, id);
}

bool AudioRuntime::IsVoicePlaying(VoiceId id) const noexcept {
  return impl_->IsVoicePlaying(id);
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

AudioGraph& AudioRuntime::graph() noexcept { return impl_->graph(); }

GraphNodeHandle AudioRuntime::GetGraphNode(NodeId id) noexcept {
  return GraphNodeHandle(this, id);
}

StatusOr<GraphPlanHandle> AudioRuntime::CompileAndStageGraph() {
  return impl_->CompileAndStageGraph();
}

Status AudioRuntime::ActivateGraphPlan(GraphPlanHandle handle) {
  Status status =
      Submit({.type = CommandType::kActivateGraphPlan, .plan_handle = handle});

  if (status.ok()) {
    impl_->MarkGraphActivationSubmitted(handle);
  }

  return status;
}

Status AudioRuntime::ReleaseStagedGraphPlan(GraphPlanHandle handle) {
  if (!impl_->ReleaseStagedGraphPlan(handle)) {
    return Status(ErrorCode::kInvalidArgument,
                  "plan is not a releasable staged plan (stale handle, not "
                  "pending, or activation already submitted)");
  }

  return Status::Ok();
}

StatusOr<AudioAssetId> AudioRuntime::LoadAudioAsset(const std::string& path) {
  return impl_->LoadAudioAsset(path);
}

StatusOr<AudioAssetInfo> AudioRuntime::GetAudioAssetInfo(
    AudioAssetId id) const {
  return impl_->GetAudioAssetInfo(id);
}

Status AudioRuntime::ReleaseAudioAsset(AudioAssetId id) {
  return impl_->ReleaseAudioAsset(id);
}

}  // namespace lavanda
