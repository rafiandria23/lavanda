# Changelog

All notable changes to Lavanda are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Phase 2: Real-Time Audio Runtime

#### Added

- `AudioRuntime`: real-time orchestration layer above `AudioDevice`. Owns
  the command queue, render-side state, audio clock, and diagnostics.
  Non-copyable, non-movable; PImpl'd so internals never leak into the
  public header.
- `Command` / `CommandType` (public): value-based, trivially copyable
  commands -- `StartTone`, `StopTone`, `SetFrequency`, `SetGain`.
- `CommandQueue` (internal): bounded, lock-free, single-producer/
  single-consumer ring buffer. Allocation-free and wait-free on the
  consumer (audio-thread) side; rejects pushes when full rather than
  blocking or dropping silently, surfaced as `Status(kQueueFull, ...)`.
- `AudioClock` (internal): frame-based, not wall-clock, monotonic audio
  timeline.
- `RenderState` plus a minimal sine oscillator (internal): audio-thread-
  owned render state, mutated only via `ApplyCommand()`.
- `RenderContext` (internal): small, stack-only bundle of what one render
  callback needs.
- `RenderDiagnostics` / `RuntimeStats`: render count, missed-deadline
  count, last/max render duration, last callback frame count --
  real-time-safe accumulation via relaxed atomics, exposed to the control
  thread through `AudioRuntime::stats()`.
- `ErrorCode::kQueueFull` added to the existing `Status` error model.
- Test-only real-time-safety verification: `AllocationGuard` (global
  `operator new`/`delete` override, per-thread tracked) and
  `DoNotOptimizeAway()` (a compiler-barrier helper needed to keep the
  guard's own self-tests meaningful under Release optimization -- see
  `docs/architecture/realtime_runtime.md`), plus `RealtimeSafetyTest`
  asserting a real `AudioRuntime`'s render path performs zero heap
  allocation.
- `FakeAudioDevice` test double, enabling deterministic, hardware-free
  testing of `AudioRuntime`'s render path via synchronous `PumpRender()`.
- A concurrency test (`tests/concurrency/`) exercising `CommandQueue`
  under real producer/consumer threads (200,000 commands, forced index
  wraparound).
- Integration tests for the full `AudioDevice` + `AudioRuntime` stack
  against real hardware, gated behind `LAVANDA_RUN_HARDWARE_TESTS=1`.
- `examples/runtime_demo`: CLI demonstration driving playback entirely
  through `Submit()` -- start, an audible frequency change (440->880Hz),
  a gain change, stop, shutdown. Kept alongside (not replacing)
  `examples/device_probe`.
- `docs/architecture/realtime_runtime.md`: the control/real-time domain
  split, command flow, queue saturation policy, shutdown sequence,
  callback lifetime model, diagnostics, and known limitations.

#### Verified

- Debug (ASan+UBSan) and Release presets, both from a clean `build/`
  wipe.
- Real Core Audio hardware: tone playback, audible frequency changes,
  gain changes, clean shutdown, repeated start/stop -- via both
  `examples/runtime_demo` and the hardware-gated integration test.
- A one-off ThreadSanitizer build (`-DLAVANDA_ENABLE_TSAN=ON`) run against
  real hardware via `runtime_demo`: zero data races reported across
  several hundred real render callbacks racing against live `Submit()`
  calls from the control thread.

#### Scope notes

- No mixer, voices, resource/asset manager, DSP graph, filters, effects,
  spatial audio, streaming, MIDI, plugin hosting, offline rendering,
  sample-accurate scheduler, custom allocator, SIMD, additional backends,
  or device enumeration -- see `docs/architecture/realtime_runtime.md`
  for the full out-of-scope list.

### Phase 1: Audio Device Abstraction

#### Added

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

#### Scope notes

- Only macOS/Core Audio is implemented as a backend. WASAPI/ALSA/PipeWire
  are explicitly out of scope for this phase (see the architecture doc).
- The full Phase 2 real-time runtime (command queues, scheduling, DSP
  graph, resource management) is intentionally not part of this phase.
