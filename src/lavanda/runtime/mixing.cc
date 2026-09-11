#include "lavanda/runtime/mixing.h"

#include <cmath>

#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"

namespace lavanda {

Status Voice::Start() {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  return runtime_->Submit({.type = CommandType::kStartVoice, .voice_id = id_});
}

Status Voice::Stop() {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  return runtime_->Submit({.type = CommandType::kStopVoice, .voice_id = id_});
}

Status Voice::Destroy() {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  Status status =
      runtime_->Submit({.type = CommandType::kDestroyVoice, .voice_id = id_});

  if (status.ok()) {
    runtime_->ReleaseVoiceReservation(id_);

    id_ = VoiceId();
    runtime_ = nullptr;
  }

  return status;
}

Status Voice::SetGain(float gain) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  if (!std::isfinite(gain) || gain < 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "voice gain must be finite and non-negative");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetVoiceGain, .value = gain, .voice_id = id_});
}

Status Voice::SetPan(float pan) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  if (!std::isfinite(pan)) {
    return Status(ErrorCode::kInvalidArgument, "voice pan must be finite");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetVoicePan, .value = pan, .voice_id = id_});
}

Status Voice::SetFrequency(float frequency_hz) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "voice frequency must be finite and positive");
  }

  return runtime_->Submit({.type = CommandType::kSetVoiceFrequency,
                           .value = frequency_hz,
                           .voice_id = id_});
}

Status Voice::SetBus(BusId bus_id) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Voice is not valid");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetVoiceBus, .voice_id = id_, .bus_id = bus_id});
}

Status Voice::SetBus(const Bus& bus) { return SetBus(bus.id()); }

Status Bus::Destroy() {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Bus is not valid");
  }

  if (id_ == kMasterBusId) {
    return Status(ErrorCode::kInvalidArgument,
                  "the master bus cannot be destroyed");
  }

  Status status =
      runtime_->Submit({.type = CommandType::kDestroyBus, .bus_id = id_});

  if (status.ok()) {
    runtime_->ReleaseBusReservation(id_);

    id_ = BusId();
    runtime_ = nullptr;
  }

  return status;
}

Status Bus::SetGain(float gain) {
  if (!is_valid()) {
    return Status(ErrorCode::kInvalidArgument, "Bus is not valid");
  }

  if (!std::isfinite(gain) || gain < 0.0f) {
    return Status(ErrorCode::kInvalidArgument,
                  "bus gain must be finite and non-negative");
  }

  return runtime_->Submit(
      {.type = CommandType::kSetBusGain, .value = gain, .bus_id = id_});
}

}  // namespace lavanda
