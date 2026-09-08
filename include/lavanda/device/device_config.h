#ifndef LAVANDA_DEVICE_CONFIG_H_
#define LAVANDA_DEVICE_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "lavanda/core/sample.h"

namespace lavanda {

class DeviceSelector {
 public:
  static DeviceSelector Default() noexcept { return DeviceSelector(); }

  static DeviceSelector ById(std::string backend_id) {
    DeviceSelector selector;
    selector.backend_id_ = std::move(backend_id);

    return selector;
  }

  bool is_default() const noexcept { return !backend_id_.has_value(); }
  const std::optional<std::string>& backend_id() const noexcept {
    return backend_id_;
  }

 private:
  DeviceSelector() = default;
  std::optional<std::string> backend_id_;
};

struct DeviceConfig {
  DeviceSelector device = DeviceSelector::Default();
  double sample_rate_hz = 48000.0;
  std::uint32_t channel_count = 2;
  std::uint32_t buffer_size_frames = 512;
  SampleFormat sample_format = SampleFormat::kFloat32;

  bool IsValid() const noexcept {
    return sample_rate_hz > 0.0 && channel_count > 0 && buffer_size_frames > 0;
  }
};

}  // namespace lavanda

#endif
