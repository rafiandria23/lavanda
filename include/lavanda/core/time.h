#ifndef LAVANDA_CORE_TIME_H_
#define LAVANDA_CORE_TIME_H_

#include <cstdint>
#include <limits>

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

enum class FramePosition : std::uint64_t {};

constexpr std::uint64_t ToInteger(FramePosition position) noexcept {
  return static_cast<std::uint64_t>(position);
}

inline constexpr FramePosition kMaxFramePosition{
    std::numeric_limits<std::uint64_t>::max()};

constexpr FramePosition AddSaturating(FramePosition position,
                                      FrameCount offset) noexcept {
  const std::uint64_t base = ToInteger(position);
  const std::uint64_t add = ToInteger(offset);
  const std::uint64_t room = std::numeric_limits<std::uint64_t>::max() - base;

  return add > room ? kMaxFramePosition : FramePosition{base + add};
}

constexpr FrameCount FramesBetween(FramePosition from,
                                   FramePosition to) noexcept {
  return FrameCount{ToInteger(to) > ToInteger(from)
                        ? ToInteger(to) - ToInteger(from)
                        : std::uint64_t{0}};
}

}  // namespace lavanda

#endif
