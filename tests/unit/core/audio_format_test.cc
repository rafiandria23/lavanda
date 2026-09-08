#include "lavanda/core/audio_format.h"

#include <gtest/gtest.h>

#include <string>

namespace lavanda {
namespace {

TEST(AudioFormatTest, DefaultIsInvalid) {
  AudioFormat format;

  EXPECT_FALSE(format.IsValid());
}

TEST(AudioFormatTest, ValidFormatReportsFields) {
  AudioFormat format(48000.0, 2, SampleFormat::kFloat32,
                     ChannelLayout::kStereo);

  EXPECT_TRUE(format.IsValid());
  EXPECT_DOUBLE_EQ(format.sample_rate_hz(), 48000.0);
  EXPECT_EQ(format.channel_count(), 2u);
  EXPECT_EQ(format.sample_format(), SampleFormat::kFloat32);
  EXPECT_EQ(format.channel_layout(), ChannelLayout::kStereo);
}

TEST(AudioFormatTest, ZeroSampleRateIsInvalid) {
  AudioFormat format(0.0, 2, SampleFormat::kFloat32);

  EXPECT_FALSE(format.IsValid());
}

TEST(AudioFormatTest, ZeroChannelsIsInvalid) {
  AudioFormat format(48000.0, 0, SampleFormat::kFloat32);

  EXPECT_FALSE(format.IsValid());
}

TEST(AudioFormatTest, BytesPerFrameAccountsForChannelsAndFormat) {
  AudioFormat stereo_float(48000.0, 2, SampleFormat::kFloat32);

  EXPECT_EQ(stereo_float.bytes_per_frame(), 8u);

  AudioFormat mono_int16(44100.0, 1, SampleFormat::kInt16);

  EXPECT_EQ(mono_int16.bytes_per_frame(), 2u);
}

TEST(AudioFormatTest, EqualityComparesAllFields) {
  AudioFormat a(48000.0, 2, SampleFormat::kFloat32);
  AudioFormat b(48000.0, 2, SampleFormat::kFloat32);
  AudioFormat different_rate(44100.0, 2, SampleFormat::kFloat32);

  EXPECT_EQ(a, b);

  EXPECT_NE(a, different_rate);
}

TEST(AudioFormatTest, ToStringIsHumanReadable) {
  AudioFormat format(48000.0, 2, SampleFormat::kFloat32);
  const std::string text = format.ToString();

  EXPECT_NE(text.find("48000"), std::string::npos);
  EXPECT_NE(text.find("float32"), std::string::npos);
}

}  // namespace
}  // namespace lavanda
