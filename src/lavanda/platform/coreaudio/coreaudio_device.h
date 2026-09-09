#ifndef LAVANDA_PLATFORM_COREAUDIO_COREAUDIO_DEVICE_H_
#define LAVANDA_PLATFORM_COREAUDIO_COREAUDIO_DEVICE_H_

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <memory>

#include "lavanda/core/status.h"
#include "lavanda/device/audio_device.h"

namespace lavanda {
namespace platform {
namespace coreaudio {

class CoreAudioDevice final : public lavanda::AudioDevice {
 public:
  CoreAudioDevice();
  ~CoreAudioDevice() override;

  CoreAudioDevice(const CoreAudioDevice&) = delete;
  CoreAudioDevice& operator=(const CoreAudioDevice&) = delete;

  Status Open(const DeviceConfig& config,
              RenderCallback render_callback) override;
  Status Start() override;
  Status Stop() override;
  Status Close() override;

  bool is_open() const noexcept override;
  bool is_running() const noexcept override;
  const AudioFormat& format() const noexcept override;
  std::uint32_t buffer_size_frames() const noexcept override;

 private:
  static OSStatus RenderThunk(void* ref_con,
                              AudioUnitRenderActionFlags* action_flags,
                              const AudioTimeStamp* timestamp,
                              UInt32 bus_number, UInt32 frame_count,
                              AudioBufferList* buffer_list);

  OSStatus Render(AudioUnitRenderActionFlags* action_flags,
                  const AudioTimeStamp* timestamp, UInt32 frame_count,
                  AudioBufferList* buffer_list) noexcept;

  AudioComponentInstance output_unit_ = nullptr;
  RenderCallback render_callback_;
  AudioFormat negotiated_format_;
  std::uint32_t negotiated_buffer_frames_ = 0;
  bool is_open_ = false;
  std::atomic<bool> is_running_{false};
};

std::unique_ptr<lavanda::AudioDevice> MakeCoreAudioDevice();

}  // namespace coreaudio
}  // namespace platform
}  // namespace lavanda

#endif
