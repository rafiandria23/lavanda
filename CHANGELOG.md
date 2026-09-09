# Changelog

All notable changes to Lavanda are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased] - Phase 1: Audio Device Abstraction

### Added

- Repository scaffolding: modern target-based CMake project, CMake presets
  for Debug/Release, compiler-warnings and sanitizer modules, CTest
  integration, a standalone `cmake -P cmake/Clean.cmake` helper for wiping
  `build/` entirely.
- Platform-independent core types: `AudioFormat`, `AudioBuffer` /
  `AudioBufferView` (explicit sample/frame/channel distinction),
  `SampleFormat`, `ChannelLayout`, `FrameCount`.
- `Status` / `StatusOr<T>` error model for device lifecycle and
  configuration.
- `AudioDevice` abstraction (open/start/stop/close, format and buffer-size
  reporting) and `DeviceConfig` / `DeviceSelector` for platform-independent
  device configuration.
- `CoreAudioDevice`: a Core Audio backend for macOS, built on the system
  Default Output AudioUnit, producing a deterministic 440 Hz test tone
  through the public `AudioDevice` API. Gated behind the
  `LAVANDA_BUILD_COREAUDIO_BACKEND` CMake option.
- Unit tests for `AudioFormat`, `AudioBuffer`/`AudioBufferView`, and
  `DeviceConfig`/`DeviceSelector`.
- An integration test suite for the device/backend layer that distinguishes
  "backend unavailable", "backend available but test failed", and "backend
  successfully initialized" -- including a full `Open`/`Start`/`Stop`/
  `Close` lifecycle test against real hardware, opt-in via
  `LAVANDA_RUN_HARDWARE_TESTS=1` so it never becomes a CI dependency on a
  runner having live audio output.
- `examples/device_probe`: a command-line demonstration of the public API,
  verified against real hardware -- opens the default output, reports the
  negotiated format, and plays an audible test tone.
- `docs/architecture/audio_device.md` describing the device abstraction,
  the Core Audio boundary, and what is deferred to Phase 2.

### Scope notes

- Only macOS/Core Audio is implemented as a backend. WASAPI/ALSA/PipeWire
  are explicitly out of scope for this phase (see the architecture doc).
- The full Phase 2 real-time runtime (command queues, scheduling, DSP
  graph, resource management) is intentionally not part of this phase.
