#include <gtest/gtest.h>

#include "lavanda/graph/node.h"

namespace lavanda {
namespace {

class ConstantTestNode : public AudioNode {
 public:
  explicit ConstantTestNode(float value) : value_(value) {}

  void Process(NodeProcessContext& context) noexcept override {
    for (std::uint32_t f = 0; f < context.frame_count; ++f) {
      context.output(f, 0) = value_;
    }
  }

  std::uint32_t input_channel_count() const noexcept override { return 0; }
  std::uint32_t output_channel_count() const noexcept override { return 1; }
  std::uint32_t expected_input_count() const noexcept override { return 0; }

 private:
  float value_;
};

TEST(AudioNodeTest, ProcessWritesExpectedConstantOutput) {
  ConstantTestNode node(0.5f);

  AudioBuffer buffer(8, 1);
  NodeProcessContext context;

  context.output = buffer.View();
  context.frame_count = 8;
  context.sample_rate_hz = 48000.0;

  node.Process(context);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), 0.5f);
  }
}

TEST(AudioNodeTest, DeclaredChannelCountsAreQueryable) {
  ConstantTestNode node(0.0f);

  EXPECT_EQ(node.input_channel_count(), 0u);
  EXPECT_EQ(node.output_channel_count(), 1u);
  EXPECT_EQ(node.expected_input_count(), 0u);
}

TEST(AudioNodeTest, InputsArrayDefaultsToEmptyWithZeroCount) {
  NodeProcessContext context;

  EXPECT_EQ(context.input_count, 0u);

  for (const auto& input : context.inputs) {
    EXPECT_TRUE(input.empty());
  }
}

}  // namespace
}  // namespace lavanda
