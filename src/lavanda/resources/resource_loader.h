#ifndef LAVANDA_RESOURCES_RESOURCE_LOADER_H_
#define LAVANDA_RESOURCES_RESOURCE_LOADER_H_

#include <cstdint>
#include <string>

#include "lavanda/core/status.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/resources/resource_config.h"
#include "lavanda/resources/resource_store.h"

namespace lavanda {

class ResourceLoader {
 public:
  ResourceLoader(ResourceStore& store, const ResourceConfig& config) noexcept
      : store_(store), config_(config) {}

  StatusOr<AudioAssetId> Load(const std::string& path,
                              std::uint32_t target_sample_rate_hz);

 private:
  ResourceStore& store_;
  ResourceConfig config_;
};

}  // namespace lavanda

#endif
