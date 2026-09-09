# Lavanda

Lavanda is a C++20 real-time audio engine, built in phases. Phase 1
established a platform-independent audio device interface plus a working
Core Audio backend for macOS. Phase 2 builds a real-time runtime on top of
that boundary, letting ordinary application code control audio playback
safely from outside the audio thread.

```
Application -> Lavanda Engine -> DSP/Graph/Runtime -> Audio Device
            Abstraction -> Native Platform Backend -> OS/Hardware
```

See [`docs/architecture/audio_device.md`](docs/architecture/audio_device.md)
for the Phase 1 device abstraction, and
[`docs/architecture/realtime_runtime.md`](docs/architecture/realtime_runtime.md)
for the Phase 2 runtime, and [`CHANGELOG.md`](CHANGELOG.md) for what's
shipped.

## Current platform support

| Platform | Backend       | Status                   |
| -------- | ------------- | ------------------------- |
| macOS    | Core Audio    | Implemented (Phase 1)     |
| Windows  | WASAPI        | Not implemented (future)  |
| Linux    | ALSA/PipeWire | Not implemented (future)  |

The library, its core types, and its unit/integration tests build and run
on any platform with a C++20 compiler; only the Core Audio backend and the
examples require macOS.

## Real-time runtime (Phase 2)

`AudioRuntime` sits on top of `AudioDevice`, letting application code
control playback safely from outside the audio thread via a bounded,
allocation-free command channel:

```cpp
lavanda::AudioRuntime runtime(std::move(device));
runtime.Start();
runtime.Submit({lavanda::CommandType::kSetFrequency, 440.0f});
runtime.Submit({lavanda::CommandType::kStartTone, 0.0f});
// ... later ...
runtime.Shutdown();
```

See [`docs/architecture/realtime_runtime.md`](docs/architecture/realtime_runtime.md)
for the full design -- the control/real-time domain split, command flow,
queue saturation policy, shutdown sequence, and real-time safety
guarantees. Try it with `examples/runtime_demo`.

## Build requirements

- CMake >= 3.24
- A C++20 compiler (Apple Clang from a recent Xcode/Command Line Tools
  install on macOS; GCC or Clang elsewhere for the platform-independent
  parts)
- macOS + the Core Audio / AudioToolbox / CoreFoundation system frameworks,
  to build and run the Core Audio backend and the examples

Lavanda has no third-party dependencies in the library itself. Tests use
GoogleTest, fetched automatically via CMake's `FetchContent` if no
system-installed copy is found (see `cmake/Dependencies.cmake`), which
requires network access on first configure unless GoogleTest is already
installed (`brew install googletest` if you'd rather not rely on the
fetch).

## Configure, build, and test

The Core Audio backend is opt-in via `LAVANDA_BUILD_COREAUDIO_BACKEND`
(defaults `OFF`, so the library and its platform-independent tests can
configure and build before the backend existed, or on non-macOS hosts).
On macOS, enable it explicitly:

```sh
cmake --preset debug -DLAVANDA_BUILD_COREAUDIO_BACKEND=ON
cmake --build --preset debug
ctest --preset debug
```

Substitute `release` for `debug` for an optimized, non-sanitized build.
The debug preset enables AddressSanitizer + UndefinedBehaviorSanitizer
(see `CMakePresets.json` / `cmake/Sanitizers.cmake`).

If you'd rather not pass `-DLAVANDA_BUILD_COREAUDIO_BACKEND=ON` on every
fresh configure, bake it into a personal `CMakeUserPresets.json` (already
gitignored) rather than editing the committed `CMakePresets.json`.

### Cleaning a build directory

```sh
cmake -P cmake/Clean.cmake
```

Removes `build/` entirely -- useful when switching CMake generators or
recovering from a broken configure, since it doesn't rely on an already-
working build directory the way `cmake --build --target clean` does.

### Running the hardware-dependent integration tests

Two integration tests exercise real hardware end to end -- the Phase 1
device lifecycle, and the Phase 2 runtime driving playback through
`Submit()`. Both are opt-in, since CI runners aren't guaranteed to have an
accessible audio output device, and because nobody wants audio I/O firing
as a side effect of an ordinary `ctest` run:

```sh
LAVANDA_RUN_HARDWARE_TESTS=1 ctest --preset debug -R "DefaultOutputDevice|AudioRuntimeIntegration"
```

Without that environment variable, both tests skip themselves rather than
failing.

## Running the examples

On macOS, after building with the Core Audio backend enabled:

```sh
./build/debug/examples/device_probe/device_probe
```

Opens the default output device, prints the negotiated format, plays a
quiet 440 Hz test tone for two seconds, then stops and closes cleanly --
exercises `AudioDevice` directly, with no runtime involved.

```sh
./build/debug/examples/runtime_demo/runtime_demo
```

Creates an `AudioRuntime`, plays 440 Hz then 880 Hz then a quiet tail, all
driven through `Submit()` from the control thread, then shuts down
cleanly -- the Phase 2 demonstration.

## Project layout

```
include/lavanda/     Public API (platform-independent)
src/lavanda/          Implementation
  platform/coreaudio/   Core Audio backend (Phase 1)
  runtime/               Real-time runtime internals (Phase 2)
tests/                Unit, concurrency, and integration tests
examples/              device_probe (Phase 1) and runtime_demo (Phase 2)
docs/architecture/    Design documentation
```

## Current project phase

**Phase 2 of the planned build-out** (real-time runtime, built on Phase
1's device abstraction). Deliberately out of scope for this phase: a
mixer, voices, an audio asset/resource manager, a DSP graph, filters,
effects, spatial audio, streaming, MIDI, plugin hosting, offline
rendering, a sample-accurate scheduler, additional backends, and device
enumeration. See
[`docs/architecture/realtime_runtime.md`](docs/architecture/realtime_runtime.md#what-remains-out-of-scope)
for the full list.

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

Unless you explicitly state otherwise, any contribution intentionally
submitted for inclusion in this project by you shall be dual licensed as
above, without any additional terms or conditions.
