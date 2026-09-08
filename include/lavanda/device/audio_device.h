#ifndef LAVANDA_DEVICE_AUDIO_DEVICE_H_
#define LAVANDA_DEVICE_AUDIO_DEVICE_H_

#include <cstdint>
#include <functional>
#include <memory>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/audio_format.h"
#include "lavanda/core/status.h"
#include "lavanda/device/device_config.h"

namespace lavanda {

using RenderCallback = std::function<void(AudioBufferView output)>;

class AudioDevice {
 public:
  virtual ~AudioDevice() = default;

  virtual Status Open(const DeviceConfig& config,
                      RenderCallback render_callback) = 0;
  virtual Status Start() = 0;
  virtual Status Stop() = 0;
  virtual Status Close() = 0;

  virtual bool is_open() const noexcept = 0;
  virtual bool is_running() const noexcept = 0;
  virtual const AudioFormat& format() const noexcept = 0;
  virtual std::uint32_t buffer_size_frames() const noexcept = 0;
};

StatusOr<std::unique_ptr<AudioDevice>> CreateDefaultOutputDevice();

}  // namespace lavanda

#endif
