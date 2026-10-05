# Changelog

All notable changes to Lavanda are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Phase 5: Audio Resources, Assets, and Decoding

#### Added

- `AudioAssetId` / `AudioAssetInfo` (public): generation-checked asset
  handle and metadata.
- WAV decoder (1-2 channels, up to 768 kHz source rate) and a linear
  resampler to the device rate. Assets are stored as immutable, interleaved
  32-bit float data.
- `ResourceStore` (internal): bounded, generation-checked slots with an
  atomic pin count. The audio thread only resolves and unpins; memory is
  freed on the control thread once an asset is Retiring with no pins.
- `ResourceLoader` (internal): decode, resample, and insert, with limits on
  asset count, total bytes, frames per asset, and file size.
- `AudioRuntime` asset API: `LoadAudioAsset()`, `GetAudioAssetInfo()`,
  `ReleaseAudioAsset()` (deferred release), `ReclaimAssets()`,
  `CreateVoice(AudioAssetId)`, `AddAssetSourceNode()`.
- Asset-backed voices: play once, `Start()` to replay, `Stop()` resets, state
  published to the control side via `Voice::IsPlaying()`. Stereo assets use
  a balance law (center is unity per channel) in `stereo_balance.h`.
- `AudioAssetSourceNode` (public): graph node that plays an asset once per
  plan activation. `AudioNode` gained `required_asset()` and `BindAsset()`;
  `GraphCompileContext` carries the store and device rate into the compiler,
  which pins reachable assets and fails the compile on an unavailable or
  mismatched asset.
- `AssetPins` (internal): move-only RAII pin holder owned by each
  `GraphExecutionPlan`. `GraphPlanStore::ReclaimRetiredPlans()` destroys
  retired plans on the control thread.
- `Command::asset_id`, carried on `kCreateVoice`.
- `RuntimeConfig::resource_config`; `RuntimeStats` fields
  `resident_asset_count`, `retiring_asset_count`, `resident_asset_bytes`,
  `asset_load_failures`.
- Committed WAV test fixtures, a Python generator, and an
  `AudioFixturesUpToDate` ctest that regenerates and compares them.
- Tests: decoder, resampler, store, loader, voice-pool, runtime, node, graph
  lifetime, and reclamation unit tests; real-time-safety tests under
  `AllocationGuard` for asset voices and asset graphs (with a counter-test
  showing loading *does* allocate, and a test that plays an asset after its
  file is deleted); concurrency tests with a real render thread; a
  hardware-gated integration test.
- `examples/playback`.
- `docs/architecture/resources_and_assets.md`.

#### Changed

- Public `Command` gained an `asset_id` field, after `plan_handle`;
  designated initializers must follow that order.
- `GraphCompiler::Compile()` and `GraphPlanStore::BuildAndStage()` take a
  defaulted `GraphCompileContext`; existing call sites compile unchanged.
- `CompileAndStageGraph()` and `ReleaseAudioAsset()` now reclaim retired
  assets and plans on the control thread.

#### Fixed

- `RuntimeStats::command_failures` was not initialized.

#### Verified

- Debug (ASan + UBSan) and Release builds; the full test suite.
- ThreadSanitizer on `lavanda_concurrency_tests`, including the asset voice
  churn, release/reload, and plan-swap scenarios: no reports (macOS arm64,
  RelWithDebInfo, `FakeAudioDevice` render thread).
- Hardware-gated asset integration test and `examples/playback` against real
  Core Audio output, including stereo balance.

#### Scope notes

- Streaming is deferred to Phase 8. Also not included: compressed codecs,
  looping, pausing, seeking, anti-aliased resampling. Asset identity is by
  path, not content. See `docs/architecture/resources_and_assets.md` for the
  full list of known limitations.

### Phase 4: DSP Graph and Execution Engine

#### Added

- `AudioGraph` (public): mutable, control-side topology with generation-
  checked `NodeId`s. Per-edit validation only (valid ids, input index,
  channel match, self-connection, occupied input, capacity); cycles and
  completeness are deferred to compilation. Move-only.
- `AudioNode` / `NodeProcessContext` (public): node interface with
  `Process()`, channel/input counts, and `Clone()`.
- Built-in nodes (public, `include/lavanda/graph/nodes/`):
  `OscillatorNode`, `GainNode`, `PanNode`, `MixerNode`,
  `OnePoleLowPassNode`, `OnePoleHighPassNode`, `DelayNode`, `OutputNode`.
- `GraphCompiler` (internal): reachability from the designated output,
  required-input checks, cycle detection via Kahn's algorithm with a
  deterministic smallest-`NodeId.index` tie-break, and liveness-based
  buffer reuse (free lists keyed by channel count).
- `GraphExecutionPlan` (internal): immutable, flat, preallocated plan
  holding cloned nodes and the `NodeId` each step was compiled from.
- `GraphPlanStore` (internal): fixed slots with atomic state and
  generation; the audio thread activates and retires plans but never
  destroys one -- destruction happens only on the control thread.
- `GraphExecutor` (internal): audio-thread execution of a plan,
  accumulating into the output; a graceful no-op on channel mismatch or an
  oversized block.
- `AudioRuntime` graph API: `graph()`, `GetGraphNode()`,
  `CompileAndStageGraph()`, `ActivateGraphPlan()`,
  `ReleaseStagedGraphPlan()`. `GraphNodeHandle` for live parameter changes.
- New commands: `kSetNodeGain`, `kSetNodeFrequency`, `kSetNodeCutoff`,
  `kSetNodePan`, `kSetNodeDelayFrames`, `kActivateGraphPlan`.
- `RuntimeConfig`: `graph_config`, `max_graph_plans`.
- `RuntimeStats`: `graph_compilation_failures`, `graph_activation_count`,
  `active_graph_node_count`, `active_graph_plan_generation`.
- Public `GraphPlanHandle` and `NodeId`, so public `Command` no longer
  depends on internal headers.
- Tests: unit tests for nodes, graph, compiler, plan store, executor, and
  runtime/graph integration; real-time-safety tests under
  `AllocationGuard` (including a test asserting compilation *does*
  allocate); concurrency tests with a real render thread; a hardware-gated
  integration test.
- `benchmarks/graph_benchmark` (opt-in via `LAVANDA_BUILD_BENCHMARKS`) and a
  `GraphBenchmarkSmoke` ctest.
- `examples/dsp_graph`.
- `docs/architecture/dsp_graph.md`.

#### Changed

- Render order is now legacy tone (overwrites) -> voice/bus mixer
  (accumulates) -> graph (accumulates).
- Node headers live under `include/lavanda/graph/nodes/` so applications
  can construct them.

#### Verified

- Hardware-gated graph integration test and `examples/dsp_graph` against
  real Core Audio output.
- Benchmark baseline recorded in `docs/architecture/dsp_graph.md`.

#### Scope notes

- Recompiling gives nodes fresh state; there is no crossfade, state
  migration, or parameter smoothing between or within plans. See
  `docs/architecture/dsp_graph.md` for the full list.

### Phase 3: Voices, Mixing, and Buses

#### Added

- `Voice` and `Bus` (public): application-facing handles for creating and
  independently controlling simultaneous sounds and routing them through
  buses into a master output.
- `VoiceId` / `BusId`: generation-checked handles so stale handles are
  rejected rather than addressing a reused slot.
- Voice/bus commands: create, start, stop, destroy, gain, pan, frequency,
  bus assignment, bus gain.
- `RuntimeConfig` capacity limits for voices and buses; fixed-size,
  preallocated pools on the audio side.
- Equal-power pan math and mixing helpers.
- `examples/mixing`, `docs/architecture/voices_and_mixing.md`, and
  hardware-gated integration tests.

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
