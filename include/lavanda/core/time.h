#ifndef LAVANDA_CORE_TIME_H_
#define LAVANDA_CORE_TIME_H_

#include <cstdint>

namespace lavanda {

enum class FrameCount : std::uint64_t {};

constexpr std::uint64_t ToInteger(FrameCount count) noexcept {
  return static_cast<std::uint64_t>(count);
}

constexpr FrameCount operator+(FrameCount a, FrameCount b) noexcept {
  return FrameCount{ToInteger(a) + ToInteger(b)};
}

constexpr double FramesToSeconds(FrameCount frames,
                                 double sample_rate_hz) noexcept {
  return sample_rate_hz > 0.0
             ? static_cast<double>(ToInteger(frames)) / sample_rate_hz
             : 0.0;
}

}  // namespace lavanda

#endif
