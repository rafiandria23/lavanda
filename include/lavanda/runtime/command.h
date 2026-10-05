#ifndef LAVANDA_RUNTIME_COMMAND_H_
#define LAVANDA_RUNTIME_COMMAND_H_

#include <cstdint>

#include "lavanda/graph/graph_plan_handle.h"
#include "lavanda/graph/node_id.h"
#include "lavanda/resources/audio_asset_id.h"
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

  kSetNodeGain,
  kSetNodeFrequency,
  kSetNodeCutoff,
  kSetNodePan,
  kSetNodeDelayFrames,

  kActivateGraphPlan,
};

struct Command {
  CommandType type = CommandType::kStopTone;
  float value = 0.0f;
  VoiceId voice_id;
  BusId bus_id;
  NodeId node_id;
  GraphPlanHandle plan_handle;
  AudioAssetId asset_id;
};

}  // namespace lavanda

#endif
