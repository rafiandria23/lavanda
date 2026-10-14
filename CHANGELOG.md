# Changelog

All notable changes to Lavanda are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Phase 6: Sample-Accurate Scheduling, Timeline, and Automation

#### Added

- `FramePosition` and `FrameCount` (public, `lavanda/core/time.h`): opaque
  64-bit frame types with `ToInteger()`, `AddSaturating()`, `FramesBetween()`,
  `FramesToSeconds()` and `kMaxFramePosition`. Neither converts implicitly to
  an integer.
- `AudioRuntime::frame_position()`: the start frame of the next device
  callback. Monotonic, never reset, safe to read from any thread. Stopping
  freezes it; shutting down and restarting does not rewind it.
- `AudioRuntime::ScheduleAtFrame(command, frame)`: apply a command at an exact
  audio frame, including in the middle of a device callback. The callback is
  rendered in segments split at event frames, so an event at frame T first
  affects sample T. Returns `kInvalidArgument`, `kResourceExhausted` (event cap
  reached), `kQueueFull` or `kDeviceUnavailable` on failure.
- Schedulable commands: `kStartVoice`, `kStopVoice`, `kSetVoiceGain`,
  `kSetVoicePan`, `kSetVoiceFrequency`, `kSetBusGain`, the graph-node setters
  (`kSetNodeGain`, `kSetNodeFrequency`, `kSetNodeCutoff`, `kSetNodePan`,
  `kSetNodeDelayFrames`), and the new ramps. Anything else is rejected with
  `kInvalidArgument` before any capacity is reserved.
- Gain ramps: `kRampVoiceGain` and `kRampBusGain`, with `Voice::RampGain()` and
  `Bus::RampGain()`. Linear, evaluated in closed form from the absolute frame
  so the result does not depend on how the callback is segmented. Duration 0
  is a step; a replacement ramp starts from the exact current value; `SetGain`
  cancels a ramp. The constant-gain path is bit-identical to Phase 5.
- `Command::duration_frames`.
- `RuntimeConfig::max_scheduled_events` (default 256; 0 disables scheduling).
- `RuntimeStats`: `scheduled_events_accepted`, `_executed`, `_late`,
  `_discarded`, `_dropped`, `_capacity_failures`, `_pending`, and
  `_max_lateness_frames`. In a quiescent snapshot,
  `accepted == executed + discarded + dropped + pending`.
- Internal: `EventScheduler` (fixed-capacity min-heap owned by the audio
  thread, ordered by frame then admission sequence), `EventReservations`
  (control-side atomic reservation, so capacity failures are reported
  synchronously and the heap cannot overflow), `GainRamp`, and a session epoch
  that lets the audio thread discard stale events after `Shutdown()`.
- Tests:
  - Unit: frame types, scheduler, reservations, ramps, event timing across
    segment boundaries, and every schedulable operation checked bit-for-bit
    against an immediate-replay oracle.
  - Real-time safety: the scheduled and ramp paths under `AllocationGuard`
    (full heap, mixed bursts, node events across a plan swap, Shutdown discard).
  - Concurrency: racing producers, exact capacity exhaustion, Stop/Start while
    scheduling, Shutdown/restart discard and capacity return, and a recorded
    output check that a scheduled start lands on exactly the expected sample.
  - Hardware-gated: scheduled events through real device callbacks, and
    Stop/Start/Shutdown/restart behavior.
  - Test support: `RenderLoop` and `hardware_gate.h`
    (`test_support::HardwareTestsRequested()`), shared by every hardware-gated
    integration test.
- `examples/scheduling`: a phrase of eight notes, each a scheduled pitch change
  with an attack and decay ramp, under a bus swell, with a deliberately
  past-dated event to show the late counter.
- `docs/architecture/scheduling_and_timeline.md`.

#### Changed

- Render order within a callback: commands are drained at the callback start
  (immediates applied, scheduled items admitted), then the block is rendered in
  segments with due events applied at segment boundaries.
- `Command` gained a `duration_frames` field, after `asset_id`; designated
  initializers must follow that order.
- `FakeAudioDevice` tracks its running flag with an atomic, so tests can
  Stop/Start from the control thread while a render thread pumps.
- Hardware-gated integration tests share `hardware_gate.h` instead of each
  carrying a private copy of the environment check.
- The TSan instructions in the README use the `LAVANDA_ENABLE_TSAN` option.
- CI: the Linux (GCC) job now configures, builds and tests both Debug and
  Release, prints the compiler version, and runs `ctest --output-on-failure`
  (the macOS job gained `--output-on-failure` too). Previously Linux built
  Debug only.
- `AudioGraph::Connect()` (Phase 4) tests the node pointers for null directly
  instead of going through `IsValidNode()`. Behavior, error codes and messages
  are unchanged; this silences a GCC `-Wnull-dereference` false positive that
  fires only at `-O3` (Release, `-Werror`).
- A Phase 6 unit test renamed a file-name constant that shared its name with an
  enumerator of a scoped enum, which GCC's `-Wshadow` rejects. Test-only.

#### Verified

- Debug (ASan + UBSan) and Release builds; the full test suite with
  `LAVANDA_RUN_HARDWARE_TESTS=1` against real Core Audio output (macOS), in
  both configurations, with no test skipped.
- ThreadSanitizer on `lavanda_concurrency_tests` (macOS arm64,
  RelWithDebInfo, `FakeAudioDevice` render thread): a full run, and the new
  scheduling concurrency suite repeated 20 times, with no reports.
- On real hardware, `Stop()` is synchronous: the clock is frozen when it
  returns.
- `examples/scheduling` on real Core Audio output: all 28 scheduled events
  executed, the one deliberately past-dated event counted as late
  (`max_lateness=1`), nothing discarded or dropped, `pending=0`, no missed
  deadlines, clean shutdown.

#### Known issues and scope notes

- The control-side API is single-producer: `ScheduleAtFrame()`, `Submit()` and
  the `Voice`/`Bus`/`GraphNodeHandle` setters feed one SPSC queue. They may be
  called from different threads over time but must be externally serialized.
  `frame_position()`, `stats()` and `is_running()` are safe from any thread.
- Scheduled events cannot be cancelled once accepted.
- Ramps are linear and apply to voice and bus gain only. Node-parameter ramps
  (including `GainNode`) are deferred. `kSetVoiceBus` is not schedulable.
- Callbacks larger than `max_frames_per_block` are rendered as one segment, so
  events inside them are applied at the next callback and counted late.
- `DelayNode` (Phase 4): `delay == max` reads the sample just written, so the
  effective maximum is `max - 1` frames. This applies to immediate and
  scheduled changes alike and is left as is.
- Three retry loops in the scheduling concurrency tests have no deadline;
  a broken scheduler could hang them rather than fail them.
- Phase 5 limitations carry over unchanged. See
  `docs/architecture/scheduling_and_timeline.md` for the full list.

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
  path, not content. Decoding is bounded by file size, so the transient peak
  while decoding a large 8-bit file can exceed the resident limits (the
  resident footprint never does). See
  `docs/architecture/resources_and_assets.md` for the full list of known
  limitations.
- Oscillator voices from Phase 3 are kept alongside asset voices.

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
