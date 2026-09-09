# Lavanda

Lavanda is a C++20 real-time audio engine, built in phases. This is
**Phase 1: Audio Device Abstraction** -- a platform-independent audio
device interface, plus a real Core Audio backend for macOS capable of
opening the system's default output and producing a deterministic test
tone through it.

```
Application -> Lavanda Engine -> DSP/Graph/Runtime -> Audio Device
            Abstraction -> Native Platform Backend -> OS/Hardware
```

Phase 1 implements the bottom two layers above (Audio Device Abstraction
and a Core Audio backend). See
[`docs/architecture/audio_device.md`](docs/architecture/audio_device.md)
for the full design, and [`CHANGELOG.md`](CHANGELOG.md) for what shipped.

## Current platform support

| Platform | Backend       | Status                   |
| -------- | ------------- | ------------------------- |
| macOS    | Core Audio    | Implemented (Phase 1)     |
| Windows  | WASAPI        | Not implemented (future)  |
| Linux    | ALSA/PipeWire | Not implemented (future)  |

The library, its core types, and its unit/integration tests build and run
on any platform with a C++20 compiler; only the Core Audio backend and
`examples/device_probe` require macOS.

## Build requirements

- CMake >= 3.24
- A C++20 compiler (Apple Clang from a recent Xcode/Command Line Tools
  install on macOS; GCC or Clang elsewhere for the platform-independent
  parts)
- macOS + the Core Audio / AudioToolbox / CoreFoundation system frameworks,
  to build and run the Core Audio backend and the example

Lavanda has no third-party dependencies in the library itself. Tests use
GoogleTest, fetched automatically via CMake's `FetchContent` if no
system-installed copy is found (see `cmake/Dependencies.cmake`), which
requires network access on first configure unless GoogleTest is already
installed (`brew install googletest` if you'd rather not rely on the
fetch).

## Configure, build, and test

The Core Audio backend is opt-in via `LAVANDA_BUILD_COREAUDIO_BACKEND`
(defaults `OFF`, so the library and its platform-independent tests can
configure and build before the backend exists, or on non-macOS hosts).
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

### Running the hardware-dependent integration test

One integration test exercises the real Core Audio backend end to end
(open -> start -> stop -> close against actual hardware, twice in a row).
It's opt-in, since CI runners aren't guaranteed to have an accessible
audio output device, and because nobody wants audio I/O firing as a side
effect of an ordinary `ctest` run:

```sh
LAVANDA_RUN_HARDWARE_TESTS=1 ctest --preset debug -R DefaultOutputDevice
```

Without that environment variable, the test skips itself rather than
failing (see `tests/integration/device/default_output_device_test.cc`).

## Running the device probe

On macOS, after building with the Core Audio backend enabled:

```sh
./build/debug/examples/device_probe/device_probe
```

It opens the default output device, prints the negotiated format, plays a
quiet 440 Hz test tone for two seconds, then stops and closes cleanly.

## Project layout

```
include/lavanda/     Public API (platform-independent)
src/lavanda/          Implementation, including the Core Audio backend
                      under src/lavanda/platform/coreaudio/
tests/                Unit tests (core types) and integration tests
                      (device/backend layer)
examples/device_probe Minimal CLI demo of the public API
docs/architecture/    Design documentation
```

## Current project phase

**Phase 1 of the planned build-out.** Deliberately out of scope for this
phase: a DSP graph, mixer, resource manager, scheduler, additional
backends, and anything else from the real-time runtime layer above the
device abstraction. See
[`docs/architecture/audio_device.md`](docs/architecture/audio_device.md#what-is-intentionally-deferred-to-phase-2)
for the full list.

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

Unless you explicitly state otherwise, any contribution intentionally
submitted for inclusion in this project by you shall be dual licensed as
above, without any additional terms or conditions.
