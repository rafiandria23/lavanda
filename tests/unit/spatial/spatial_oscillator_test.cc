#include "lavanda/spatial/spatial_oscillator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/runtime/gain_ramp.h"

namespace lavanda {
namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = std::numbers::pi;

void ExpectPlainSine(const AudioBuffer& output, std::uint32_t frames,
                     double frequency_hz, double start_phase) {
  double phase = start_phase;

  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    EXPECT_FLOAT_EQ(output(frame, 0), static_cast<float>(std::sin(phase)))
        << "frame " << frame;

    phase += 2.0 * kPi * frequency_hz / kSampleRate;
  }
}

TEST(SpatialOscillatorTest, ConstantUnitRatioMatchesAPlainSine) {
  AudioBuffer output(256, 1);
  double phase = 0.0;

  RenderDopplerSine(output.View(), phase, 440.0f, GainRamp(1.0f), 0,
                    kSampleRate);

  ExpectPlainSine(output, 256, 440.0, 0.0);
  EXPECT_NEAR(phase, 256.0 * 2.0 * kPi * 440.0 / kSampleRate, 1.0e-9);
}

TEST(SpatialOscillatorTest, ConstantDoubleRatioDoublesTheFrequency) {
  AudioBuffer output(256, 1);
  double phase = 0.0;

  RenderDopplerSine(output.View(), phase, 440.0f, GainRamp(2.0f), 0,
                    kSampleRate);

  ExpectPlainSine(output, 256, 880.0, 0.0);
}

TEST(SpatialOscillatorTest, PhaseIsContinuousAcrossCalls) {
  AudioBuffer whole(256, 1);
  AudioBuffer first(100, 1);
  AudioBuffer second(156, 1);
  double whole_phase = 0.0;
  double split_phase = 0.0;

  RenderDopplerSine(whole.View(), whole_phase, 440.0f, GainRamp(1.5f), 0,
                    kSampleRate);
  RenderDopplerSine(first.View(), split_phase, 440.0f, GainRamp(1.5f), 0,
                    kSampleRate);
  RenderDopplerSine(second.View(), split_phase, 440.0f, GainRamp(1.5f), 100,
                    kSampleRate);

  for (std::uint32_t frame = 0; frame < 100; ++frame) {
    EXPECT_FLOAT_EQ(first(frame, 0), whole(frame, 0));
  }

  for (std::uint32_t frame = 0; frame < 156; ++frame) {
    EXPECT_FLOAT_EQ(second(frame, 0), whole(100 + frame, 0));
  }

  EXPECT_NEAR(split_phase, whole_phase, 1.0e-9);
}

TEST(SpatialOscillatorTest, RampIsIntegratedPerFrame) {
  AudioBuffer output(200, 1);
  double phase = 0.0;

  GainRamp ratio(1.0f);
  ratio.Start(0, 2.0f, 200);

  RenderDopplerSine(output.View(), phase, 440.0f, ratio, 0, kSampleRate);

  const double expected =
      2.0 * kPi * 440.0 / kSampleRate * (200.0 + 199.0 / 2.0);

  EXPECT_NEAR(phase, expected, 1.0e-4);
}

TEST(SpatialOscillatorTest, NyquistClampBoundsTheIncrement) {
  AudioBuffer output(100, 1);
  double phase = 0.0;

  RenderDopplerSine(output.View(), phase, 20000.0f, GainRamp(2.0f), 0,
                    kSampleRate);

  EXPECT_NEAR(phase, 100.0 * kPi, 1.0e-9);
}

TEST(SpatialOscillatorTest, StartingPhaseIsHonoured) {
  AudioBuffer output(4, 1);
  double phase = 1.0;

  RenderDopplerSine(output.View(), phase, 440.0f, GainRamp(1.0f), 0,
                    kSampleRate);

  EXPECT_FLOAT_EQ(output(0, 0), static_cast<float>(std::sin(1.0)));
}

TEST(SpatialOscillatorTest, DegenerateInputsClearTheOutput) {
  AudioBuffer output(8, 1);
  AudioBufferView view = output.View();

  for (std::uint32_t frame = 0; frame < 8; ++frame) {
    view(frame, 0) = 1.0f;
  }

  double phase = 0.25;

  RenderDopplerSine(output.View(), phase, 440.0f, GainRamp(1.0f), 0, 0.0);

  for (std::uint32_t frame = 0; frame < 8; ++frame) {
    EXPECT_EQ(output(frame, 0), 0.0f);
  }

  EXPECT_EQ(phase, 0.25);
}

}  // namespace
}  // namespace lavanda
