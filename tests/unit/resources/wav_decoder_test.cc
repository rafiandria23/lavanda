#include "lavanda/resources/decoders/wav_decoder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lavanda/resources/decoder.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kMaxFrames = 1u << 20;
constexpr std::size_t kMaxFileBytes = 1u << 24;

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

StatusOr<AudioAsset> Decode(const char* name) {
  return DecodeAudioFile(FixturePath(name), kMaxFrames, kMaxFileBytes);
}

// expected[frame][channel]
void ExpectFrames(const AudioAsset& asset,
                  const std::vector<std::vector<float>>& expected) {
  ASSERT_EQ(asset.frame_count(), expected.size());

  for (std::uint32_t f = 0; f < asset.frame_count(); ++f) {
    ASSERT_EQ(asset.channel_count(), expected[f].size());

    for (std::uint32_t c = 0; c < asset.channel_count(); ++c) {
      EXPECT_FLOAT_EQ(asset.sample(f, c), expected[f][c])
          << "frame " << f << " channel " << c;
    }
  }
}

const std::vector<std::vector<float>> kRamp8 = {
    {0.0f}, {0.125f}, {0.25f}, {0.375f}, {0.5f}, {0.625f}, {0.75f}, {0.875f}};

// ---- valid files ------------------------------------------------------------

TEST(WavDecoderTest, MonoPcm16) {
  StatusOr<AudioAsset> result = Decode("mono_pcm16.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().sample_rate_hz(), 48000u);
  EXPECT_EQ(result.value().channel_count(), 1u);

  ExpectFrames(result.value(), kRamp8);
}

TEST(WavDecoderTest, StereoPcm16IsInterleavedCorrectly) {
  StatusOr<AudioAsset> result = Decode("stereo_pcm16.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().sample_rate_hz(), 44100u);

  ExpectFrames(
      result.value(),
      {{0.0f, 0.25f}, {0.5f, -0.25f}, {-0.5f, 0.125f}, {-1.0f, -0.125f}});
}

TEST(WavDecoderTest, Pcm8IsUnsignedCenteredAt128) {
  StatusOr<AudioAsset> result = Decode("mono_pcm8.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().sample_rate_hz(), 22050u);

  ExpectFrames(result.value(),
               {{0.0f}, {0.5f}, {-0.5f}, {-1.0f}, {0.9921875f}});
}

TEST(WavDecoderTest, Pcm24SignExtends) {
  StatusOr<AudioAsset> result = Decode("mono_pcm24.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();

  ExpectFrames(result.value(), {{0.0f}, {0.5f}, {-0.5f}, {-1.0f}});
}

TEST(WavDecoderTest, Pcm32) {
  StatusOr<AudioAsset> result = Decode("mono_pcm32.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();

  ExpectFrames(result.value(), {{0.0f}, {0.5f}, {-0.5f}, {-1.0f}});
}

TEST(WavDecoderTest, Float32) {
  StatusOr<AudioAsset> result = Decode("mono_float32.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();

  ExpectFrames(result.value(), {{0.0f}, {0.25f}, {-0.75f}, {1.0f}});
}

TEST(WavDecoderTest, ExtensibleWrapperIsAccepted) {
  StatusOr<AudioAsset> result = Decode("mono_pcm16_extensible.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();

  ExpectFrames(result.value(), kRamp8);
}

TEST(WavDecoderTest, UnknownOddSizedChunkIsSkippedWithPadding) {
  StatusOr<AudioAsset> result = Decode("mono_pcm16_list_chunk.wav");

  ASSERT_TRUE(result.ok()) << result.status().message();

  ExpectFrames(result.value(), kRamp8);
}

// ---- malformed / unsupported files ---------------------------------------

struct RejectCase {
  const char* file;
  ErrorCode code;
};

TEST(WavDecoderTest, RejectsMalformedAndUnsupportedFilesWithExpectedCode) {
  const RejectCase cases[] = {
      {"not_a_wav.wav", ErrorCode::kUnsupportedFormat},
      {"truncated_data.wav", ErrorCode::kInvalidArgument},
      {"riff_size_too_large.wav", ErrorCode::kInvalidArgument},
      {"bad_block_align.wav", ErrorCode::kInvalidArgument},
      {"partial_frame.wav", ErrorCode::kInvalidArgument},
      {"zero_frames.wav", ErrorCode::kInvalidArgument},
      {"zero_channels.wav", ErrorCode::kInvalidArgument},
      {"six_channels.wav", ErrorCode::kUnsupportedFormat},
      {"unsupported_adpcm.wav", ErrorCode::kUnsupportedFormat},
      {"unsupported_12bit.wav", ErrorCode::kUnsupportedFormat},
      {"nonfinite_float.wav", ErrorCode::kInvalidArgument},
      {"missing_fmt.wav", ErrorCode::kInvalidArgument},
      {"missing_data.wav", ErrorCode::kInvalidArgument},
  };

  for (const RejectCase& c : cases) {
    SCOPED_TRACE(c.file);

    StatusOr<AudioAsset> result = Decode(c.file);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), c.code) << result.status().message();
  }
}

TEST(WavDecoderTest, NonexistentFileIsRejected) {
  StatusOr<AudioAsset> result = Decode("does_not_exist.wav");

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), ErrorCode::kInvalidArgument);
}

TEST(WavDecoderTest, FrameLimitIsEnforcedBeforeDecoding) {
  // mono_pcm16.wav has 8 frames.
  StatusOr<AudioAsset> result = DecodeAudioFile(
      FixturePath("mono_pcm16.wav"), /*max_frames=*/4, kMaxFileBytes);

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), ErrorCode::kResourceExhausted);
}

TEST(WavDecoderTest, FileSizeLimitIsEnforcedBeforeReading) {
  StatusOr<AudioAsset> result = DecodeAudioFile(
      FixturePath("mono_pcm16.wav"), kMaxFrames, /*max_file_bytes=*/16);

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), ErrorCode::kResourceExhausted);
}

TEST(WavDecoderTest, TinyInputsAreRejectedWithoutReadingPastTheEnd) {
  EXPECT_EQ(DecodeWav({}, kMaxFrames).status().code(),
            ErrorCode::kUnsupportedFormat);

  const std::vector<std::uint8_t> riff_only = {'R', 'I', 'F', 'F'};

  EXPECT_EQ(DecodeWav(riff_only, kMaxFrames).status().code(),
            ErrorCode::kUnsupportedFormat);
}

}  // namespace
}  // namespace lavanda
