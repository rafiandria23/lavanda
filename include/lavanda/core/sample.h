#ifndef LAVANDA_CORE_SAMPLE_H_
#define LAVANDA_CORE_SAMPLE_H_

#include <cstddef>
#include <cstdint>

namespace lavanda {

using Sample = float;

enum class SampleFormat : std::uint8_t {
  kFloat32,
  kInt16,
  kInt32,
};

constexpr std::size_t SampleSizeBytes(SampleFormat format) noexcept {
  switch (format) {
    case SampleFormat::kFloat32:
      return 4;
    case SampleFormat::kInt16:
      return 2;
    case SampleFormat::kInt32:
      return 4;
  }

  return 0;
}

}  // namespace lavanda

#endif
