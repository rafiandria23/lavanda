#ifndef LAVANDA_SPATIAL_VEC3_H_
#define LAVANDA_SPATIAL_VEC3_H_

#include <cmath>
#include <type_traits>

namespace lavanda {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

static_assert(std::is_trivially_copyable_v<Vec3>,
              "Vec3 travels inside commands and must stay trivially copyable");

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vec3 operator*(const Vec3& v, float scale) noexcept {
  return Vec3{v.x * scale, v.y * scale, v.z * scale};
}

constexpr Vec3 operator*(float scale, const Vec3& v) noexcept {
  return v * scale;
}

constexpr float Dot(const Vec3& a, const Vec3& b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x};
}

constexpr float LengthSquared(const Vec3& v) noexcept { return Dot(v, v); }

inline float Length(const Vec3& v) noexcept {
  return std::sqrt(LengthSquared(v));
}

}  // namespace lavanda

#endif
