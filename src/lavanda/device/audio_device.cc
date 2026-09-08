#include "lavanda/device/audio_device.h"

namespace lavanda {

StatusOr<std::unique_ptr<AudioDevice>> CreateDefaultOutputDevice() {
  return StatusOr<std::unique_ptr<AudioDevice>>(Status(
      ErrorCode::kDeviceUnavailable, "no AudioDevice backend wired up yet"));
}
}  // namespace lavanda
