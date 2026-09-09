#ifndef LAVANDA_RUNTIME_COMMAND_H_
#define LAVANDA_RUNTIME_COMMAND_H_

#include <cstdint>

#include "lavanda/runtime/handles.h"

namespace lavanda {

enum class CommandType : std::uint8_t {
  kStartTone,
  kStopTone,
  kSetFrequency,
  kSetGain,

  kCreateVoice,
  kStartVoice,
  kStopVoice,
  kDestroyVoice,
  kSetVoiceGain,
  kSetVoicePan,
  kSetVoiceFrequency,
  kSetVoiceBus,

  kCreateBus,
  kDestroyBus,
  kSetBusGain,
};

struct Command {
  CommandType type = CommandType::kStopTone;
  float value = 0.0f;
  VoiceId voice_id;
  BusId bus_id;
};

}  // namespace lavanda

#endif
