#include "lavanda/device/audio_device.h"

#if defined(__APPLE__)
#include "lavanda/platform/coreaudio/coreaudio_device.h"
#endif

namespace lavanda {

StatusOr<std::unique_ptr<AudioDevice>> CreateDefaultOutputDevice() {
#if defined(__APPLE__)
  return StatusOr<std::unique_ptr<AudioDevice>>(
      platform::coreaudio::MakeCoreAudioDevice());
#else
  return StatusOr<std::unique_ptr<AudioDevice>>(Status(
      ErrorCode::kDeviceUnavailable,
      "no AudioDevice backend is implemented for this platform in Phase 1"));
#endif
}

}  // namespace lavanda
