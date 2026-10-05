#ifndef LAVANDA_RESOURCES_RESOURCE_CONFIG_H_
#define LAVANDA_RESOURCES_RESOURCE_CONFIG_H_

#include <cstddef>
#include <cstdint>

namespace lavanda {

struct ResourceConfig {
  std::size_t max_assets = 64;
  std::uint64_t max_total_bytes = std::uint64_t{256} * 1024 * 1024;
  std::uint32_t max_frames_per_asset = 48000u * 120u;
  std::uint64_t max_file_bytes = std::uint64_t{128} * 1024 * 1024;
};

}  // namespace lavanda

#endif
