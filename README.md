# Lavanda

Lavanda is a C++20 real-time audio engine, built in phases. Phase 1
established a platform-independent audio device interface plus a working
Core Audio backend for macOS. Phase 2 built a real-time runtime on top of
that boundary. Phase 3 added voices, mixing, and buses, letting an
application create and independently control multiple simultaneous
sounds. Phase 4 adds a DSP graph: nodes (oscillators, gains, filters,
delays, mixers, pans) wired into a graph on the control side, compiled to
an immutable execution plan, and executed by the audio thread.

```
Application -> Lavanda Engine -> DSP/Graph/Runtime -> Audio Device
            Abstraction -> Native Platform Backend -> OS/Hardware
```

See [`docs/architecture/audio_device.md`](docs/architecture/audio_device.md)
for the Phase 1 device abstraction,
[`docs/architecture/realtime_runtime.md`](docs/architecture/realtime_runtime.md)
for the Phase 2 runtime,
[`docs/architecture/voices_and_mixing.md`](docs/architecture/voices_and_mixing.md)
for the Phase 3 voice/bus mixer,
[`docs/architecture/dsp_graph.md`](docs/architecture/dsp_graph.md) for the
Phase 4 DSP graph, and [`CHANGELOG.md`](CHANGELOG.md) for what's shipped.

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

## Voices and mixing (Phase 3)

Multiple independently controlled voices, routed through buses into a
master output, all driven through the same command channel:

```cpp
lavanda::AudioRuntime runtime(std::move(device));
runtime.Start();

lavanda::StatusOr<lavanda::Bus> sfx_bus = runtime.CreateBus();
lavanda::StatusOr<lavanda::Voice> voice = runtime.CreateVoice();
voice.value().SetFrequency(440.0f);
voice.value().SetGain(0.5f);
voice.value().SetPan(-0.3f);
voice.value().SetBus(sfx_bus.value());
voice.value().Start();
```

See [`docs/architecture/voices_and_mixing.md`](docs/architecture/voices_and_mixing.md)
for the full design -- handle identity and stale-handle safety, the
mixer's gain/pan math, bus routing, and real-time safety guarantees for a
populated engine. Try it with `examples/mixing`.

## DSP graph (Phase 4)

Build a graph of nodes on the control side, compile it, and activate it. The
audio thread never sees the mutable graph -- it only executes the compiled,
immutable plan:

```cpp
lavanda::AudioRuntime runtime(std::move(device));
runtime.Start();

lavanda::AudioGraph& graph = runtime.graph();
auto osc  = graph.AddNode(std::make_unique<lavanda::OscillatorNode>());
auto gain = graph.AddNode(std::make_unique<lavanda::GainNode>(1));
auto pan  = graph.AddNode(std::make_unique<lavanda::PanNode>());
auto out  = graph.AddNode(std::make_unique<lavanda::OutputNode>(2));
graph.Connect(osc.value(), gain.value(), 0);
graph.Connect(gain.value(), pan.value(), 0);
graph.Connect(pan.value(), out.value(), 0);
graph.SetOutput(out.value());

lavanda::StatusOr<lavanda::GraphPlanHandle> plan = runtime.CompileAndStageGraph();
runtime.ActivateGraphPlan(plan.value());

// Live parameter changes go through the command queue:
runtime.GetGraphNode(osc.value()).SetFrequency(660.0f);
```

Compilation validates the whole graph (reachability, required inputs,
cycles), orders it deterministically, and preallocates every buffer; a
graph that fails to compile leaves the currently playing plan untouched. A
replaced plan is never freed on the audio thread.

See [`docs/architecture/dsp_graph.md`](docs/architecture/dsp_graph.md) for
the full design -- mutable graph vs. immutable plan, the compiler, plan
lifetime and activation, command ordering, and real-time safety
verification. Try it with `examples/dsp_graph`.

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

Four integration tests exercise real hardware end to end -- the Phase 1
device lifecycle, the Phase 2 runtime driving playback through
`Submit()`, the Phase 3 voice/bus mixer's full lifecycle checklist
(multiple voices, gain/pan changes, bus routing, stop/destroy/recreate
with stale-handle verification), and the Phase 4 DSP graph (linear graph
with live parameter changes, multi-source replacement, a stateful delay
sweep, and a deliberately broken graph that must leave the playing plan
untouched). All are opt-in, since CI runners aren't guaranteed to have an
accessible audio output device, and because nobody wants audio I/O firing
as a side effect of an ordinary `ctest` run:

```sh
LAVANDA_RUN_HARDWARE_TESTS=1 ctest --preset debug -R "DefaultOutputDevice|AudioRuntimeIntegration|AudioRuntimeMixingIntegration|AudioRuntimeGraphIntegration"
```

Without that environment variable, all four skip themselves rather than
failing.

### Benchmarks

The graph benchmark is off by default:

```sh
cmake --preset release -DLAVANDA_BUILD_BENCHMARKS=ON
cmake --build --preset release
```

The `graph_benchmark` binary lands under `build/release/benchmarks/`; run it
with no arguments for the default scenarios.

Only a Release build gives meaningful numbers. A `GraphBenchmarkSmoke`
ctest runs a short pass so the benchmark keeps building and running.
Baseline numbers are in
[`docs/architecture/dsp_graph.md`](docs/architecture/dsp_graph.md#performance-baseline).

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

```sh
./build/debug/examples/mixing/mixing
```

Two voices on an "SFX" bus (panned left/right), one voice on a "Music"
bus (centered), both buses feeding master -- demonstrates independent
voice gain/pan, bus routing, and bus gain all mixing together audibly --
the Phase 3 demonstration.

```sh
./build/debug/examples/dsp_graph/dsp_graph
```

Builds two oscillators -> mixer -> gain -> low-pass -> pan -> output,
compiles and activates it, changes parameters live, shows a deliberately
broken graph being rejected while the previous plan keeps playing, then
replaces the graph by inserting a delay -- the Phase 4 demonstration.

## Project layout

```
include/lavanda/     Public API (platform-independent)
src/lavanda/          Implementation
  platform/coreaudio/   Core Audio backend (Phase 1)
  runtime/               Real-time runtime internals (Phase 2),
                         voice/bus/mixer internals (Phase 3)
  graph/                 Graph compiler, execution plan, plan store,
                         executor, node implementations (Phase 4)
include/lavanda/graph/ Public graph API, including built-in nodes
tests/                Unit, concurrency, and integration tests
benchmarks/            graph_benchmark (Phase 4, opt-in)
examples/              device_probe (Phase 1), runtime_demo (Phase 2),
                       mixing (Phase 3), dsp_graph (Phase 4)
docs/architecture/    Design documentation
```

## Current project phase

**Phase 4 of the planned build-out** (a DSP graph and execution engine,
built on Phase 2's runtime and Phase 3's mixer). Still explicitly out of
scope: crossfading or state migration between graph plans, parameter
smoothing, higher-order filters and effects, spatial audio, an
asset/resource manager, streaming, MIDI, plugins, and a sample-accurate
scheduler. See
[`docs/architecture/dsp_graph.md`](docs/architecture/dsp_graph.md#out-of-scope)
for the full list.

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

Unless you explicitly state otherwise, any contribution intentionally
submitted for inclusion in this project by you shall be dual licensed as
above, without any additional terms or conditions.
