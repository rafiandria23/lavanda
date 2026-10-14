#ifndef LAVANDA_SPATIAL_SPATIAL_STATE_H_
#define LAVANDA_SPATIAL_SPATIAL_STATE_H_

#include <type_traits>

#include "lavanda/core/status.h"
#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/vec3.h"

namespace lavanda {

inline constexpr float kMaxSpatialCoordinate = 1.0e6f;  // metres
inline constexpr float kMaxSpatialSpeed = 1.0e5f;       // metres/second
inline constexpr float kMaxQuaternionComponent = 1.0e6f;
inline constexpr float kMinQuaternionNorm = 1.0e-6f;

struct ListenerState {
  Vec3 position;
  Quaternion orientation;
  Vec3 velocity;
};

struct EmitterState {
  Vec3 position;
  Vec3 velocity;
};

static_assert(std::is_trivially_copyable_v<ListenerState>);
static_assert(std::is_trivially_copyable_v<EmitterState>);

Status ValidateListenerState(const ListenerState& state);
Status ValidateEmitterState(const EmitterState& state);

}  // namespace lavanda

#endif
