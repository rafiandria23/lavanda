#ifndef LAVANDA_RUNTIME_FLOAT_CONVERSION_H_
#define LAVANDA_RUNTIME_FLOAT_CONVERSION_H_

#include <cstdint>

namespace lavanda {

inline std::uint32_t ClampFloatToCount(float value,
                                       std::uint32_t upper_bound) noexcept {
  const double requested = static_cast<double>(value);

  if (!(requested > 0.0)) {
    return 0;
  }

  const double upper = static_cast<double>(upper_bound);

  return static_cast<std::uint32_t>(requested < upper ? requested : upper);
}

}  // namespace lavanda

#endif
