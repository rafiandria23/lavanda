#include "lavanda/platform/coreaudio/coreaudio_device.h"

#include <string>
#include <utility>

namespace lavanda {
namespace platform {
namespace coreaudio {

namespace {

constexpr AudioComponentDescription kDefaultOutputDescription = {
    kAudioUnitType_Output,
    kAudioUnitSubType_DefaultOutput,
    kAudioUnitManufacturer_Apple,
    0,
    0,
};

Status StatusFromOSStatus(OSStatus os_status, const char* context) {
  if (os_status == noErr) {
    return Status::Ok();
  }

  char code_chars[5] = {
      static_cast<char>((os_status >> 24) & 0xFF),
      static_cast<char>((os_status >> 16) & 0xFF),
      static_cast<char>((os_status >> 8) & 0xFF),
      static_cast<char>(os_status & 0xFF),
      '\0',
  };

  std::string message = std::string(context) + " failed with OSStatus " +
                        std::to_string(os_status) + " ('" + code_chars + "')";

  return Status(ErrorCode::kPlatformError, std::move(message));
}

}  // namespace

CoreAudioDevice::CoreAudioDevice() = default;

CoreAudioDevice::~CoreAudioDevice() { Close(); }

Status CoreAudioDevice::Open(const DeviceConfig& config,
                             RenderCallback render_callback) {
  if (is_open_) {
    return Status(ErrorCode::kAlreadyOpen, "device is already open");
  }

  if (!config.IsValid()) {
    return Status(ErrorCode::kInvalidArgument,
                  "DeviceConfig is not self-consistent");
  }

  if (!render_callback) {
    return Status(ErrorCode::kInvalidArgument,
                  "render_callback must not be empty");
  }

  AudioComponent component =
      AudioComponentFindNext(nullptr, &kDefaultOutputDescription);

  if (component == nullptr) {
    return Status(ErrorCode::kDeviceUnavailable,
                  "no default output AudioComponent is available");
  }

  OSStatus os_status = AudioComponentInstanceNew(component, &output_unit_);

  if (os_status != noErr) {
    output_unit_ = nullptr;

    return StatusFromOSStatus(os_status, "AudioComponentInstanceNew");
  }

  AudioStreamBasicDescription requested_format = {};
  requested_format.mSampleRate = config.sample_rate_hz;
  requested_format.mFormatID = kAudioFormatLinearPCM;
  requested_format.mFormatFlags =
      kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  requested_format.mChannelsPerFrame = config.channel_count;
  requested_format.mBitsPerChannel = 32;
  requested_format.mBytesPerFrame =
      sizeof(float) * requested_format.mChannelsPerFrame;
  requested_format.mFramesPerPacket = 1;
  requested_format.mBytesPerPacket = requested_format.mBytesPerFrame;

  os_status = AudioUnitSetProperty(
      output_unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
      &requested_format, sizeof(requested_format));

  if (os_status != noErr) {
    Status status =
        StatusFromOSStatus(os_status, "AudioUnitSetProperty(StreamFormat)");

    AudioComponentInstanceDispose(output_unit_);
    output_unit_ = nullptr;

    return status;
  }

  UInt32 requested_buffer_frames = config.buffer_size_frames;
  AudioUnitSetProperty(output_unit_, kAudioUnitProperty_MaximumFramesPerSlice,
                       kAudioUnitScope_Global, 0, &requested_buffer_frames,
                       sizeof(requested_buffer_frames));

  render_callback_ = std::move(render_callback);

  AURenderCallbackStruct callback_struct = {};
  callback_struct.inputProc = &CoreAudioDevice::RenderThunk;
  callback_struct.inputProcRefCon = this;

  os_status = AudioUnitSetProperty(
      output_unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
      0, &callback_struct, sizeof(callback_struct));

  if (os_status != noErr) {
    Status status = StatusFromOSStatus(
        os_status, "AudioUnitSetProperty(SetRenderCallback)");
    render_callback_ = nullptr;

    AudioComponentInstanceDispose(output_unit_);
    output_unit_ = nullptr;

    return status;
  }

  os_status = AudioUnitInitialize(output_unit_);

  if (os_status != noErr) {
    Status status = StatusFromOSStatus(os_status, "AudioUnitInitialize");
    render_callback_ = nullptr;

    AudioComponentInstanceDispose(output_unit_);
    output_unit_ = nullptr;

    return status;
  }

  AudioStreamBasicDescription negotiated = {};
  UInt32 negotiated_size = sizeof(negotiated);

  os_status = AudioUnitGetProperty(
      output_unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
      &negotiated, &negotiated_size);

  if (os_status != noErr) {
    Status status =
        StatusFromOSStatus(os_status, "AudioUnitGetProperty(StreamFormat)");

    AudioUnitUninitialize(output_unit_);
    render_callback_ = nullptr;

    AudioComponentInstanceDispose(output_unit_);
    output_unit_ = nullptr;

    return status;
  }

  negotiated_format_ =
      AudioFormat(negotiated.mSampleRate, negotiated.mChannelsPerFrame,
                  SampleFormat::kFloat32);

  negotiated_buffer_frames_ = config.buffer_size_frames;

  is_open_ = true;

  return Status::Ok();
}

Status CoreAudioDevice::Start() {
  if (!is_open_) {
    return Status(ErrorCode::kNotOpen, "Start() before a successful Open()");
  }

  if (is_running_.load(std::memory_order_acquire)) {
    return Status::Ok();
  }

  OSStatus os_status = AudioOutputUnitStart(output_unit_);

  if (os_status != noErr) {
    return StatusFromOSStatus(os_status, "AudioOutputUnitStart");
  }

  is_running_.store(true, std::memory_order_release);

  return Status::Ok();
}

Status CoreAudioDevice::Stop() {
  if (!is_open_ || !is_running_.load(std::memory_order_acquire)) {
    return Status::Ok();
  }

  OSStatus os_status = AudioOutputUnitStop(output_unit_);

  is_running_.store(false, std::memory_order_release);

  if (os_status != noErr) {
    return StatusFromOSStatus(os_status, "AudioOutputUnitStop");
  }

  return Status::Ok();
}

Status CoreAudioDevice::Close() {
  if (!is_open_) {
    return Status::Ok();
  }

  Stop();

  AudioUnitUninitialize(output_unit_);
  AudioComponentInstanceDispose(output_unit_);

  output_unit_ = nullptr;
  render_callback_ = nullptr;

  is_open_ = false;

  negotiated_format_ = AudioFormat();
  negotiated_buffer_frames_ = 0;

  return Status::Ok();
}

bool CoreAudioDevice::is_open() const noexcept { return is_open_; }
bool CoreAudioDevice::is_running() const noexcept {
  return is_running_.load(std::memory_order_acquire);
}

const AudioFormat& CoreAudioDevice::format() const noexcept {
  return negotiated_format_;
}
std::uint32_t CoreAudioDevice::buffer_size_frames() const noexcept {
  return negotiated_buffer_frames_;
}

OSStatus CoreAudioDevice::RenderThunk(void* ref_con,
                                      AudioUnitRenderActionFlags* action_flags,
                                      const AudioTimeStamp* timestamp,
                                      UInt32 /*bus_number*/, UInt32 frame_count,
                                      AudioBufferList* buffer_list) {
  return static_cast<CoreAudioDevice*>(ref_con)->Render(
      action_flags, timestamp, frame_count, buffer_list);
}

OSStatus CoreAudioDevice::Render(AudioUnitRenderActionFlags* /*action_flags*/,
                                 const AudioTimeStamp* /*timestamp*/,
                                 UInt32 frame_count,
                                 AudioBufferList* buffer_list) noexcept {
  if (buffer_list->mNumberBuffers == 0) {
    return noErr;
  }

  ::AudioBuffer* native_buffer = &buffer_list->mBuffers[0];
  auto* samples = static_cast<Sample*>(native_buffer->mData);
  std::uint32_t channels = negotiated_format_.channel_count();

  if (samples == nullptr || channels == 0) {
    return noErr;
  }

  AudioBufferView view(samples, frame_count, channels);

  if (render_callback_) {
    render_callback_(view);
  } else {
    view.Clear();
  }

  return noErr;
}

std::unique_ptr<lavanda::AudioDevice> MakeCoreAudioDevice() {
  return std::make_unique<CoreAudioDevice>();
}

}  // namespace coreaudio
}  // namespace platform
}  // namespace lavanda
