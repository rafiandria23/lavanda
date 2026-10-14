#ifndef LAVANDA_SPATIAL_QUATERNION_H_
#define LAVANDA_SPATIAL_QUATERNION_H_

#include <cmath>
#include <type_traits>

#include "lavanda/spatial/vec3.h"

namespace lavanda {

struct Quaternion {
  float w = 1.0f;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

static_assert(std::is_trivially_copyable_v<Quaternion>,
              "Quaternion travels inside commands and must stay trivially "
              "copyable");

inline float Norm(const Quaternion& q) noexcept {
  return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

constexpr Quaternion Conjugate(const Quaternion& q) noexcept {
  return Quaternion{q.w, -q.x, -q.y, -q.z};
}

inline Quaternion Normalized(const Quaternion& q) noexcept {
  const float norm = Norm(q);

  if (!(norm > 0.0f) || !std::isfinite(norm)) {
    return Quaternion{};
  }

  const float inverse = 1.0f / norm;

  return Quaternion{q.w * inverse, q.x * inverse, q.y * inverse, q.z * inverse};
}

inline Quaternion QuaternionFromAxisAngle(const Vec3& axis,
                                          float radians) noexcept {
  const float length = Length(axis);

  if (!(length > 0.0f) || !std::isfinite(length) || !std::isfinite(radians)) {
    return Quaternion{};
  }

  const float half = 0.5f * radians;
  const float scale = std::sin(half) / length;

  return Quaternion{std::cos(half), axis.x * scale, axis.y * scale,
                    axis.z * scale};
}

constexpr Vec3 Rotate(const Quaternion& q, const Vec3& v) noexcept {
  const Vec3 u{q.x, q.y, q.z};
  const Vec3 t = 2.0f * Cross(u, v);

  return v + q.w * t + Cross(u, t);
}

constexpr Vec3 InverseRotate(const Quaternion& q, const Vec3& v) noexcept {
  return Rotate(Conjugate(q), v);
}

}  // namespace lavanda

#endif
