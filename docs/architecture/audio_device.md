# Audio Device Abstraction

This document describes Lavanda's audio device abstraction as it stands at
the end of Phase 1: why it exists, how public and platform-specific code
are separated, how Core Audio fits in, and what is intentionally deferred
to later phases.

## Why a device abstraction

Lavanda's long-term architecture puts a native platform backend
(Core Audio, WASAPI, ALSA, PipeWire, ...) at the bottom of the stack:

```
Application -> Lavanda Engine -> DSP/Graph/Runtime -> Audio Device
            Abstraction -> Native Platform Backend -> OS/Hardware
```

Every layer above "Audio Device Abstraction" needs to talk to *an* output
device without caring which operating system it's running on, or which
particular API that OS exposes for audio I/O. `lavanda::AudioDevice`
(`include/lavanda/device/audio_device.h`) is that boundary. It is a pure
interface: opening a device, starting/stopping it, and reading back the
format and buffer size the hardware actually settled on. Nothing above it
needs to know that, in Phase 1, the only implementation is Core Audio.

## Public vs. platform-specific code

Everything under `include/lavanda/` is the public API, and it is
Core-Audio-free by design: no `AudioUnit`, `AudioComponentInstance`,
`AudioDeviceID`, `CFTypeRef`, or `OSStatus` appears anywhere in it. The
concrete Core Audio implementation, `CoreAudioDevice`, lives entirely under
`src/lavanda/platform/coreaudio/` and is not reachable through any public
header.

The only bridge between the two is `lavanda::CreateDefaultOutputDevice()`
(declared in the public `audio_device.h`, defined in
`src/lavanda/device/audio_device.cc`). That one translation unit is the
only place in the codebase that is allowed to know which concrete backend
exists on the current platform:

```
lavanda::AudioDevice (public interface)
        |
        +-- lavanda::platform::coreaudio::CoreAudioDevice (macOS only,
        |     reached only via CreateDefaultOutputDevice())
        |
        +-- (future) WASAPI / ALSA / PipeWire backends, same pattern
```

`CreateDefaultOutputDevice()` is guarded with `#if defined(__APPLE__)`. On
any other platform it returns `ErrorCode::kDeviceUnavailable` rather than
failing to link or silently doing nothing -- callers get an explicit,
typed answer either way.

## Core Audio's role

Core Audio's system "Default Output" AudioUnit
(`kAudioUnitSubType_DefaultOutput`) is the standard way to send audio to
whatever the user has selected as their system output, without touching
the HAL device list directly. `CoreAudioDevice::Open()`:

1. Finds and instantiates the Default Output `AudioComponent`.
2. Sets a *requested* `AudioStreamBasicDescription` (sample rate, channel
   count, 32-bit float, packed, linear PCM).
3. Installs a render callback via `kAudioUnitProperty_SetRenderCallback`.
4. Calls `AudioUnitInitialize()`.
5. Reads the stream format property back -- this is the *negotiated*
   format, and may differ from what was requested (see below).

`Close()` calls `AudioUnitUninitialize()` and
`AudioComponentInstanceDispose()`, stopping the unit first if it is
running, so a device that is destroyed while open or running cannot leak
the `AudioComponentInstance` or leave Core Audio holding a callback pointer
into a torn-down object.

Every `OSStatus` Core Audio can return is translated into a
`lavanda::Status` at this boundary (`StatusFromOSStatus` in
`coreaudio_device.cc`) -- callers of the public API never see an
`OSStatus`. Worth calling out explicitly: `AudioUnitInitialize()` is not
optional and its status must be checked. Skipping it (as an early draft of
this backend briefly did) doesn't fail loudly at `Open()` time in every
case -- it surfaces later, at `Start()`, as
`kAudioUnitErr_Uninitialized`. The lesson generalizes: with Core Audio,
"a later call failed" doesn't always mean the bug is in that call.

## Device lifecycle

```
constructed -> Open() -> Start() -> Stop() -> Start() -> ... -> Stop() -> Close()
constructed -> Open() -> Close()                      (never started -- also valid)
```

`Open()`/`Close()` are a pair, as are `Start()`/`Stop()`. Calling a method
out of its valid state returns a `Status` (`kNotOpen`, `kAlreadyOpen`)
rather than invoking undefined behavior:

- `Start()` before a successful `Open()` returns `kNotOpen`.
- `Open()` on an already-open device returns `kAlreadyOpen`.
- `Stop()` on a stopped (or never-started) device is a no-op success.
- `Close()` on a device that isn't open is a no-op success.
- Destroying an `AudioDevice` that is open or running implicitly stops and
  closes it (see `CoreAudioDevice::~CoreAudioDevice()`).

Verified against real hardware, including repeated start/stop cycles
within a single open device -- see
`tests/integration/device/default_output_device_test.cc`
(`FullLifecycleAgainstRealHardware`, opt-in via
`LAVANDA_RUN_HARDWARE_TESTS=1`) and `examples/device_probe`.

## Negotiated format vs. requested format

`DeviceConfig` describes what a caller *requests*: sample rate, channel
count, buffer size, sample format, device selection. None of these are
guarantees -- hardware may not support an arbitrary sample rate, and a
buffer size is, at best, a size the backend tries to get close to.

After a successful `Open()`, `AudioDevice::format()` and
`AudioDevice::buffer_size_frames()` report what was *actually* negotiated.
For Core Audio specifically, this means reading
`kAudioUnitProperty_StreamFormat` back out *after* `AudioUnitInitialize()`,
rather than trusting the structure that was set going in. Application code
(see `examples/device_probe/main.cc`) should always build anything
sample-rate-dependent (like a signal generator's phase increment) from the
negotiated format, never from the request.

`buffer_size_frames()` currently reports the requested buffer size rather
than a value read back from Core Audio, because Core Audio does not expose
a single property that is guaranteed to reflect the exact per-callback
frame count before the first callback fires. A future phase that needs the
true negotiated buffer size for scheduling purposes can read it directly
from a callback's `frame_count` parameter instead of relying on a static
property.

## Callback ownership

`RenderCallback` (`std::function<void(AudioBufferView)>`) is the boundary
between the control domain (application setup, device lifecycle calls) and
the real-time audio domain (a native audio thread Core Audio owns). Once
`Start()` succeeds, this callback can be invoked at any time from that
thread until `Stop()` returns.

The callback must not:

- allocate dynamically,
- perform filesystem or network I/O,
- log synchronously,
- sleep,
- take a lock that could block for an unbounded time, or
- do any work unrelated to producing the current block of samples.

Phase 1 does not build the full command/control architecture a real
engine needs to talk to this thread safely at scale (lock-free queues, a
scheduler, etc.) -- that belongs to Phase 2. What Phase 1 does establish is
the boundary itself and the discipline around it: `CoreAudioDevice::Render()`
is written to do the absolute minimum (look up a pointer, wrap it in a
view, forward to the caller's callback), and every allocation in the
codebase happens either at construction time (`AudioBuffer`,
`std::function`'s initial capture) or in non-real-time control code
(`Open()`, `Close()`, test setup).

## Error model

See `include/lavanda/core/status.h` for the full rationale. In short:
ordinary lifecycle operations return `Status`/`StatusOr<T>` rather than
throwing, because these are expected, recoverable outcomes (unsupported
format, device unavailable, misuse of the state machine), not programmer
errors -- and because the real-time callback must never throw in the first
place, so the codebase is written from the start assuming exceptions
aren't the primary error-reporting mechanism anywhere near the audio path.

## Current Phase 1 limitations

- **One backend.** Only macOS/Core Audio is implemented. The public API
  (`AudioDevice`, `DeviceConfig`, `DeviceSelector`) is written so that
  adding WASAPI/ALSA/PipeWire later means adding another
  `src/lavanda/platform/<backend>/` implementation and a branch in
  `CreateDefaultOutputDevice()` -- not redesigning the interface.
- **No device enumeration.** `DeviceSelector` can express "the system
  default" or an opaque backend-specific ID, but nothing in Phase 1
  produces a list of available devices to choose an ID from.
- **No real-time command/control architecture.** The render callback is a
  plain `std::function` set once at `Open()` time. There is no lock-free
  queue for the control domain to talk to the audio thread after that,
  because there is nothing yet on the audio thread worth commanding beyond
  "render into this buffer".
- **`buffer_size_frames()` is a best-effort report**, not a value read back
  from the hardware (see above).
- **No offline/headless testing of the exact negotiated Core Audio format**
  without real hardware -- the integration test suite handles this by
  skipping (not failing) when no backend/device is available, and gating
  the full lifecycle exercise behind `LAVANDA_RUN_HARDWARE_TESTS=1`.

## What is intentionally deferred to Phase 2

Per the Phase 1 specification, none of the following exist yet, and none
of Phase 1's types were designed to accommodate them prematurely: a DSP
graph, mixer, voices, an audio asset/resource manager, streaming, a
scheduler or audio timeline, spatial audio/HRTF, a plugin system, MIDI, an
offline renderer, a sophisticated real-time command queue, a custom
allocator, SIMD optimizations, game-engine integration, or a GUI. Phase 2
is expected to build the real-time runtime and command/control
architecture on top of the `AudioDevice` boundary defined here, without
needing to change that boundary.
