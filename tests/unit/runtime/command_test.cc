#include "lavanda/runtime/command.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace lavanda {
namespace {

TEST(CommandTest, DesignatedInitializerSetsOnlyNamedFieldsDefaultsRest) {
  Command command{.type = CommandType::kSetFrequency, .value = 440.0f};

  EXPECT_EQ(command.type, CommandType::kSetFrequency);
  EXPECT_FLOAT_EQ(command.value, 440.0f);
  EXPECT_FALSE(command.voice_id.is_valid());
  EXPECT_FALSE(command.bus_id.is_valid());
}

TEST(CommandTest, DefaultConstructedCommandIsStopToneWithInvalidIds) {
  Command command;

  EXPECT_EQ(command.type, CommandType::kStopTone);
  EXPECT_FLOAT_EQ(command.value, 0.0f);
  EXPECT_FALSE(command.voice_id.is_valid());
  EXPECT_FALSE(command.bus_id.is_valid());
}

TEST(CommandTest, DesignatedInitCanSkipMiddleFields) {
  VoiceId voice{2, 1};
  BusId bus{0, 0};
  Command command{
      .type = CommandType::kSetVoiceBus, .voice_id = voice, .bus_id = bus};

  EXPECT_EQ(command.type, CommandType::kSetVoiceBus);
  EXPECT_FLOAT_EQ(command.value, 0.0f);
  EXPECT_EQ(command.voice_id, voice);
  EXPECT_EQ(command.bus_id, bus);
}

TEST(CommandTest, IsTriviallyCopyable) {
  static_assert(std::is_trivially_copyable_v<Command>,
                "Command must stay trivially copyable to remain safe for "
                "CommandQueue's SPSC ring buffer");
}

}  // namespace
}  // namespace lavanda
