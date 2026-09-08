#ifndef LAVANDA_CORE_CHANNEL_LAYOUT_H_
#define LAVANDA_CORE_CHANNEL_LAYOUT_H_

#include <cstdint>

namespace lavanda {

enum class ChannelLayout : std::uint8_t {
  kMono,
  kStereo,
  kUnspecified,
};

constexpr std::uint32_t ChannelCountForLayout(ChannelLayout layout) noexcept {
  switch (layout) {
    case ChannelLayout::kMono:
      return 1;
    case ChannelLayout::kStereo:
      return 2;
    case ChannelLayout::kUnspecified:
      return 0;
  }

  return 0;
}

}  // namespace lavanda

#endif
