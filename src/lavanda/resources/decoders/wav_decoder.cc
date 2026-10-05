#include "lavanda/resources/decoders/wav_decoder.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/sample.h"

namespace lavanda {
namespace {

constexpr std::size_t kRiffHeaderSize = 12;
constexpr std::size_t kChunkHeaderSize = 8;
constexpr std::size_t kMinFmtSize = 16;
constexpr std::size_t kExtensibleFmtSize = 40;
constexpr std::uint16_t kTagPcm = 0x0001;
constexpr std::uint16_t kTagFloat = 0x0003;
constexpr std::uint16_t kTagExtensible = 0xFFFE;

std::uint16_t ReadU16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t ReadU32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

bool HasTag(const std::uint8_t* p, const char (&tag)[5]) noexcept {
  return std::memcmp(p, tag, 4) == 0;
}

struct WavFormat {
  std::uint16_t tag = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate_hz = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 0;
};

StatusOr<WavFormat> ParseFmt(const std::uint8_t* body, std::size_t size) {
  if (size < kMinFmtSize) {
    return Status(ErrorCode::kInvalidArgument, "WAV 'fmt ' chunk is too small");
  }

  WavFormat format;

  format.tag = ReadU16(body);
  format.channels = ReadU16(body + 2);
  format.sample_rate_hz = ReadU32(body + 4);
  format.block_align = ReadU16(body + 12);
  format.bits_per_sample = ReadU16(body + 14);

  if (format.tag == kTagExtensible) {
    if (size < kExtensibleFmtSize) {
      return Status(ErrorCode::kInvalidArgument,
                    "WAV EXTENSIBLE 'fmt ' chunk is too small");
    }

    format.tag = ReadU16(body + 24);
  }

  return format;
}

bool ReadSample(const std::uint8_t* p, const WavFormat& format,
                Sample* out) noexcept {
  if (format.tag == kTagFloat) {
    const std::uint32_t raw = ReadU32(p);
    float value = 0.0f;

    std::memcpy(&value, &raw, sizeof value);

    if (!std::isfinite(value)) {
      return false;
    }

    *out = value;

    return true;
  }

  switch (format.bits_per_sample) {
    case 8: {
      *out = (static_cast<float>(p[0]) - 128.0f) / 128.0f;
      break;
    }
    case 16: {
      const auto value = static_cast<std::int16_t>(ReadU16(p));
      *out = static_cast<float>(value) / 32768.0f;

      break;
    }
    case 24: {
      std::uint32_t raw = static_cast<std::uint32_t>(p[0]) |
                          (static_cast<std::uint32_t>(p[1]) << 8) |
                          (static_cast<std::uint32_t>(p[2]) << 16);

      if ((raw & 0x800000u) != 0) {
        raw |= 0xFF000000u;
      }

      const auto value = static_cast<std::int32_t>(raw);
      *out = static_cast<float>(static_cast<double>(value) / 8388608.0);

      break;
    }
    default: {
      const auto value = static_cast<std::int32_t>(ReadU32(p));
      *out = static_cast<float>(static_cast<double>(value) / 2147483648.0);

      break;
    }
  }

  return true;
}

}  // namespace

StatusOr<AudioAsset> DecodeWav(std::span<const std::uint8_t> bytes,
                               std::uint32_t max_frames) {
  if (bytes.size() < kRiffHeaderSize || !HasTag(bytes.data(), "RIFF") ||
      !HasTag(bytes.data() + 8, "WAVE")) {
    return Status(ErrorCode::kUnsupportedFormat, "not a RIFF/WAVE file");
  }

  const std::size_t riff_size = ReadU32(bytes.data() + 4);

  if (riff_size > bytes.size() - 8) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV file is truncated: RIFF size exceeds file size");
  }

  if (riff_size < 4) {
    return Status(ErrorCode::kInvalidArgument, "WAV RIFF size is too small");
  }

  const std::size_t end = riff_size + 8;

  std::optional<WavFormat> format;
  const std::uint8_t* data = nullptr;
  std::size_t data_size = 0;
  bool have_data = false;

  std::size_t pos = kRiffHeaderSize;

  while (pos + kChunkHeaderSize <= end) {
    const std::uint8_t* header = bytes.data() + pos;
    const std::size_t chunk_size = ReadU32(header + 4);
    const std::size_t body_pos = pos + kChunkHeaderSize;

    if (chunk_size > end - body_pos) {
      return Status(ErrorCode::kInvalidArgument,
                    "WAV chunk extends past end of file (truncated)");
    }

    const std::uint8_t* body = bytes.data() + body_pos;

    if (HasTag(header, "fmt ")) {
      if (!format) {
        StatusOr<WavFormat> parsed = ParseFmt(body, chunk_size);

        if (!parsed.ok()) {
          return parsed.status();
        }

        format = parsed.value();
      }
    } else if (HasTag(header, "data")) {
      if (!have_data) {
        data = body;
        data_size = chunk_size;
        have_data = true;
      }
    }

    pos = body_pos + chunk_size + (chunk_size & 1u);
  }

  if (!format) {
    return Status(ErrorCode::kInvalidArgument, "WAV file has no 'fmt ' chunk");
  }

  if (!have_data) {
    return Status(ErrorCode::kInvalidArgument, "WAV file has no 'data' chunk");
  }

  const WavFormat& f = *format;

  if (f.channels == 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV file declares zero channels");
  }

  if (f.channels > kMaxAssetChannels) {
    return Status(ErrorCode::kUnsupportedFormat,
                  "WAV file has " + std::to_string(f.channels) +
                      " channels; only mono and stereo are supported");
  }

  if (f.sample_rate_hz == 0 || f.sample_rate_hz > kMaxAssetSampleRateHz) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV sample rate " + std::to_string(f.sample_rate_hz) +
                      " Hz is out of range");
  }

  const bool supported =
      (f.tag == kTagPcm &&
       (f.bits_per_sample == 8 || f.bits_per_sample == 16 ||
        f.bits_per_sample == 24 || f.bits_per_sample == 32)) ||
      (f.tag == kTagFloat && f.bits_per_sample == 32);

  if (!supported) {
    return Status(ErrorCode::kUnsupportedFormat,
                  "unsupported WAV encoding (format tag " +
                      std::to_string(f.tag) + ", " +
                      std::to_string(f.bits_per_sample) + " bits per sample)");
  }

  const std::uint32_t channels = f.channels;
  const std::uint32_t bytes_per_sample = f.bits_per_sample / 8u;
  const std::uint32_t frame_bytes = channels * bytes_per_sample;

  if (static_cast<std::uint32_t>(f.block_align) != frame_bytes) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV block alignment is inconsistent with channels and "
                  "bit depth");
  }

  if (data_size % frame_bytes != 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV data chunk is not a whole number of frames");
  }

  const std::uint64_t frames = data_size / frame_bytes;

  if (frames == 0) {
    return Status(ErrorCode::kInvalidArgument,
                  "WAV file contains no audio frames");
  }

  if (frames > max_frames) {
    return Status(ErrorCode::kResourceExhausted,
                  "WAV file has " + std::to_string(frames) +
                      " frames; the maximum is " + std::to_string(max_frames));
  }

  AudioBuffer buffer(static_cast<std::uint32_t>(frames), channels);
  const std::uint8_t* src = data;

  for (std::uint32_t frame = 0; frame < buffer.frame_count(); ++frame) {
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      Sample value = 0;

      if (!ReadSample(src, f, &value)) {
        return Status(ErrorCode::kInvalidArgument,
                      "WAV file contains a non-finite float sample");
      }

      buffer(frame, channel) = value;
      src += bytes_per_sample;
    }
  }

  return AudioAsset::Create(std::move(buffer), f.sample_rate_hz);
}

}  // namespace lavanda
