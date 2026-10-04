#!/usr/bin/env python3

"""Generates the deterministic WAV fixtures used by the decoder tests.

Usage (from anywhere):
  python3 tests/fixtures/audio/generate_fixtures.py          # (re)write fixtures
  python3 tests/fixtures/audio/generate_fixtures.py --check  # verify, write nothing

The .wav files are committed; this script documents exactly what is in them.
--check regenerates every fixture in memory and compares it byte-for-byte
with the committed file, and also reports any .wav file in this directory
that the script does not produce. It exits non-zero on any difference, and is
registered with CTest as AudioFixturesUpToDate.
"""

import os
import struct
import sys

OUT = os.path.dirname(os.path.abspath(__file__))

# ---- WAV building blocks ----------------------------------------------------


def chunk(cid, body):
    pad = b"\x00" if len(body) % 2 else b""
    return cid + struct.pack("<I", len(body)) + body + pad


def fmt_chunk(tag, channels, rate, bits, block_align=None, extensible_tag=None):
    if block_align is None:
        block_align = channels * (bits // 8)

    body = struct.pack(
        "<HHIIHH", tag, channels, rate, rate * block_align, block_align, bits
    )

    if extensible_tag is not None:
        # cbSize=22, valid bits, channel mask, then the SubFormat GUID whose
        # first two bytes are the real format tag.
        body += struct.pack("<HHI", 22, bits, 0)
        body += struct.pack("<H", extensible_tag)
        body += bytes.fromhex("000000001000800000AA00389B71")

    return chunk(b"fmt ", body)


def data_chunk(data, declared=None):
    n = len(data) if declared is None else declared
    blob = b"data" + struct.pack("<I", n) + data

    return blob + (b"\x00" if len(data) % 2 else b"")


def riff(chunks, riff_size=None):
    body = b"WAVE" + chunks
    size = len(body) if riff_size is None else riff_size

    return b"RIFF" + struct.pack("<I", size) + body


def pcm16(vals):
    return struct.pack("<%dh" % len(vals), *vals)


def pcm24(vals):
    return b"".join(struct.pack("<i", v)[:3] for v in vals)


def pcm32(vals):
    return struct.pack("<%di" % len(vals), *vals)


def f32(vals):
    return struct.pack("<%df" % len(vals), *vals)


def interleave(*channels):
    return [s for frame in zip(*channels) for s in frame]


# ---- fixture definitions ----------------------------------------------------

fixtures = {}  # file name -> bytes, in definition order


def add(name, blob):
    if name in fixtures:
        raise ValueError("duplicate fixture name: " + name)

    fixtures[name] = blob


RAMP16 = pcm16([i * 4096 for i in range(8)])  # decodes to i / 8

# valid
add("mono_pcm16.wav", riff(fmt_chunk(1, 1, 48000, 16) + data_chunk(RAMP16)))
add(
    "stereo_pcm16.wav",
    riff(
        fmt_chunk(1, 2, 44100, 16)
        + data_chunk(
            pcm16(
                interleave(
                    [0, 16384, -16384, -32768],  # L: 0, .5, -.5, -1
                    [8192, -8192, 4096, -4096],
                )
            )
        )
    ),
)  # R: .25, -.25, .125, -.125
add(
    "mono_pcm8.wav",
    riff(fmt_chunk(1, 1, 22050, 8) + data_chunk(bytes([128, 192, 64, 0, 255]))),
)
add(
    "mono_pcm24.wav",
    riff(
        fmt_chunk(1, 1, 48000, 24) + data_chunk(pcm24([0, 4194304, -4194304, -8388608]))
    ),
)
add(
    "mono_pcm32.wav",
    riff(
        fmt_chunk(1, 1, 48000, 32)
        + data_chunk(pcm32([0, 1073741824, -1073741824, -2147483648]))
    ),
)
add(
    "mono_float32.wav",
    riff(fmt_chunk(3, 1, 48000, 32) + data_chunk(f32([0.0, 0.25, -0.75, 1.0]))),
)
add(
    "mono_pcm16_extensible.wav",
    riff(fmt_chunk(0xFFFE, 1, 48000, 16, extensible_tag=1) + data_chunk(RAMP16)),
)
add(
    "mono_pcm16_list_chunk.wav",
    riff(  # odd-sized chunk exercises padding
        fmt_chunk(1, 1, 48000, 16) + chunk(b"LIST", b"abcde") + data_chunk(RAMP16)
    ),
)

# malformed / unsupported
add("not_a_wav.wav", b"This is definitely not a RIFF file.\n")
add(
    "truncated_data.wav",
    riff(  # claims 16 data bytes, contains 8
        fmt_chunk(1, 1, 48000, 16) + data_chunk(pcm16([0, 1, 2, 3]), declared=16)
    ),
)
add(
    "riff_size_too_large.wav",
    riff(fmt_chunk(1, 1, 48000, 16) + data_chunk(RAMP16), riff_size=1000),
)
add(
    "bad_block_align.wav",
    riff(fmt_chunk(1, 1, 48000, 16, block_align=3) + data_chunk(RAMP16)),
)
add(
    "partial_frame.wav",
    riff(  # 7 bytes is not a whole number of frames
        fmt_chunk(1, 1, 48000, 16) + data_chunk(RAMP16[:7])
    ),
)
add("zero_frames.wav", riff(fmt_chunk(1, 1, 48000, 16) + data_chunk(b"")))
add("zero_channels.wav", riff(fmt_chunk(1, 0, 48000, 16) + data_chunk(RAMP16)))
add("six_channels.wav", riff(fmt_chunk(1, 6, 48000, 16) + data_chunk(pcm16([0] * 6))))
add("unsupported_adpcm.wav", riff(fmt_chunk(2, 1, 48000, 16) + data_chunk(RAMP16)))
add("unsupported_12bit.wav", riff(fmt_chunk(1, 1, 48000, 12) + data_chunk(RAMP16)))
add(
    "nonfinite_float.wav",
    riff(fmt_chunk(3, 1, 48000, 32) + data_chunk(f32([0.0, float("nan")]))),
)
add("missing_fmt.wav", riff(data_chunk(RAMP16)))
add("missing_data.wav", riff(fmt_chunk(1, 1, 48000, 16)))


# ---- modes ------------------------------------------------------------------


def read_file(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except FileNotFoundError:
        return None


def generate():
    for name, blob in fixtures.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(blob)

    print("wrote %d fixtures to %s" % (len(fixtures), OUT))

    return 0


def check():
    problems = []

    for name, blob in fixtures.items():
        existing = read_file(os.path.join(OUT, name))

        if existing is None:
            problems.append("missing:  " + name)
        elif existing != blob:
            problems.append("differs:  " + name)

    for name in sorted(os.listdir(OUT)):
        if name.endswith(".wav") and name not in fixtures:
            problems.append("orphan:   " + name)

    if problems:
        print("audio fixtures are out of date:")

        for p in problems:
            print("  " + p)

        print("Run: python3 " + os.path.abspath(__file__))

        return 1

    print("audio fixtures are up to date (%d files)" % len(fixtures))

    return 0


if __name__ == "__main__":
    unknown = [a for a in sys.argv[1:] if a != "--check"]

    if unknown:
        print("usage: generate_fixtures.py [--check]", file=sys.stderr)
        sys.exit(2)

    sys.exit(check() if "--check" in sys.argv[1:] else generate())
