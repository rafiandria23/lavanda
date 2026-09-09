#ifndef LAVANDA_RUNTIME_COMMAND_H_
#define LAVANDA_RUNTIME_COMMAND_H_

#include <cstdint>

namespace lavanda {

enum class CommandType : std::uint8_t {
  kStartTone,
  kStopTone,
  kSetFrequency,
  kSetGain,
};

struct Command {
  CommandType type = CommandType::kStopTone;
  float value = 0.0f;
};

}  // namespace lavanda

#endif
