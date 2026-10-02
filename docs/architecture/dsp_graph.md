# DSP Graph and Execution Engine

Phase 4 adds a node-based DSP graph on top of the Phase 2 runtime and the
Phase 3 voice/bus mixer. An application builds a graph of audio nodes
(oscillators, gains, filters, delays, mixers, pans), compiles it, and hands
the result to the audio thread.

The one rule that shapes the whole design:

> **Graph construction and mutation happen on the control side. The audio
> thread executes only a precompiled, immutable execution plan.**

The audio thread never sees an `AudioGraph`. It never validates, sorts,
allocates, or frees anything graph-related.

## Contents

1. [Core idea](#core-idea)
2. [Nodes](#nodes)
3. [Building a graph](#building-a-graph)
4. [Compilation](#compilation)
5. [Plan activation and lifetime](#plan-activation-and-lifetime)
6. [Parameter commands](#parameter-commands)
7. [Channel model](#channel-model)
8. [Integration with the runtime render path](#integration-with-the-runtime-render-path)
9. [Capacities](#capacities)
10. [Real-time safety verification](#real-time-safety-verification)
11. [Diagnostics](#diagnostics)
12. [Concurrency verification](#concurrency-verification)
13. [Performance baseline](#performance-baseline)
14. [Public vs internal types](#public-vs-internal-types)
15. [Known limitations](#known-limitations)
16. [Out of scope](#out-of-scope)

## Core idea

Two representations of the same graph exist, and each is used by exactly one
side.

| | `AudioGraph` | `GraphExecutionPlan` |
| --- | --- | --- |
| Domain | Control | Real-time (read-only after staging) |
| Mutable | Yes | No |
| Shape | Nodes + connections, arbitrary edit order | Flat, topologically ordered list of steps |
| Validated | Per edit (cheap checks only) | Whole-graph, once, at compile time |
| Memory | Allocates freely | All buffers preallocated at compile time |
| Visible to the audio thread | Never | Yes, via a generation-checked handle |

```
control thread                                 audio thread (backend callback)
--------------                                 -------------------------------
AudioGraph  (edit freely)
    |
    |  GraphCompiler::Compile()   <- all allocation, sorting, validation
    v
GraphExecutionPlan  (immutable)
    |
    |  GraphPlanStore::BuildAndStage()  -> slot: kStaged
    |  AudioRuntime::ActivateGraphPlan() -> kActivateGraphPlan command
    |                                      ---------------------->  drain commands
    |                                                              TryActivate(handle)
    |                                                              GraphExecutor::Render(plan, ...)
```

The audio thread's whole involvement is: dequeue a command, flip a slot's
state, then walk a flat array of steps. There is no lock, no allocation, and
no traversal of a pointer-linked structure that someone else may be editing.

## Nodes

`AudioNode` is the interface every node implements:

```cpp
class AudioNode {
 public:
  virtual ~AudioNode() = default;
  virtual void Process(NodeProcessContext& context) noexcept = 0;
  virtual std::size_t input_channel_count() const noexcept = 0;
  virtual std::size_t output_channel_count() const noexcept = 0;
  virtual std::size_t expected_input_count() const noexcept = 0;
  virtual std::unique_ptr<AudioNode> Clone() const = 0;
};
```

`NodeProcessContext` carries up to `kMaxNodeInputs` (8) input views, the
input count, one writable output view, the frame count, the sample rate, and
the audio clock position. It is built on the stack for each step.

**Contract.** `Process()` writes only to `context.output`. It must not
allocate, lock, block, throw, or touch inputs for writing. A node's state is
sized in its constructor (control side), never inside `Process()`.

### Built-in nodes

| Node | Inputs | Output | Notes |
| --- | --- | --- | --- |
| `OscillatorNode` | 0 | mono | Sine; own phase accumulator. `SetFrequency`, `SetGain`. |
| `GainNode(ch)` | 1 | `ch` | Scalar gain. |
| `PanNode` | 1 (mono) | stereo | Equal-power pan, shared with the Phase 3 mixer math. |
| `MixerNode(ch, n)` | `n` (clamped to 8) | `ch` | Sums its inputs. |
| `OnePoleLowPassNode(ch)` | 1 | `ch` | Per-channel state. |
| `OnePoleHighPassNode(ch)` | 1 | `ch` | Per-channel state. |
| `DelayNode(ch, max)` | 1 | `ch` | Preallocated ring buffer; delay clamped to `max`. |
| `OutputNode(ch)` | 1 | `ch` | Passthrough; marks the graph's result. |

Node headers live in `include/lavanda/graph/nodes/`, because applications
must be able to construct them (see [Public vs internal types](#public-vs-internal-types)).

### Cloning and node state

The graph owns its nodes. A compiled plan owns **clones** of them, made with
`Clone()`:

- **Parameters are copied** (frequency, gain, cutoff, pan, delay time).
- **Transient state is reset** (oscillator phase, filter memory, delay line).

This has a visible consequence, deliberately: *recompiling the topology gives
every node fresh state.* A delay line is empty after a graph replacement, and
an oscillator restarts at phase zero. Only in-place parameter commands
against an already-active plan preserve ongoing state.

Because the plan has its own copies, editing the `AudioGraph` after compiling
never affects what is playing, and the audio thread never shares a node
object with the control side.

## Building a graph

`AudioGraph` is the control-side, mutable model. It is move-only.

```cpp
lavanda::AudioGraph& graph = runtime.graph();

auto osc  = graph.AddNode(std::make_unique<lavanda::OscillatorNode>());
auto gain = graph.AddNode(std::make_unique<lavanda::GainNode>(1));
auto pan  = graph.AddNode(std::make_unique<lavanda::PanNode>());
auto out  = graph.AddNode(std::make_unique<lavanda::OutputNode>(2));

graph.Connect(osc.value(),  gain.value(), /*input_index=*/0);
graph.Connect(gain.value(), pan.value(),  0);
graph.Connect(pan.value(),  out.value(),  0);
graph.SetOutput(out.value());
```

Operations: `AddNode`, `RemoveNode`, `Connect(src, dst, input_index)`,
`Disconnect(dst, input_index)`, `SetOutput`, plus read accessors
(`output()`, `IsValidNode`, `GetNode`, `connections()`, `AllNodeIds()`).

### Per-edit validation

Each edit is checked immediately for things that can be decided locally, and
returns a `Status` / `StatusOr`:

- the `NodeId` is valid (index in range, generation matches);
- `input_index` is below the destination's `expected_input_count()`;
- channel counts match across the new connection;
- the connection is not a self-connection;
- the destination input slot is not already occupied;
- the node and connection capacity from `GraphConfig` is not exceeded
  (`kResourceExhausted`).

An edit that fails changes nothing. `RemoveNode` also severs every connection
touching the node, as source or destination, so a later `Connect` can never
attach to a half-removed node.

What is **not** checked per edit: cycles, whole-graph completeness, and
required inputs. Checking those on every edit would make intermediate states
(for example, adding a node before wiring it) illegal. They are checked once,
at compile time.

### Node identity

`NodeId{index, generation}` is the same hand-written, generation-checked
handle pattern used for `VoiceId` and `BusId`. A removed node's slot can be
reused; the generation bump makes any old `NodeId` fail validation instead of
silently addressing the new occupant.

`GraphNodeHandle` (from `runtime.GetGraphNode(id)`) is a non-owning facade for
sending parameter commands to a node. It holds a runtime pointer and a
`NodeId`, and it validates that values are finite before submitting.

## Compilation

`AudioRuntime::CompileAndStageGraph()` runs the compiler on the control
thread and stages the result. Compilation fails with a `Status`; it never
partially succeeds.

### Steps

1. **Output check.** `SetOutput` must have been called with a valid node.
2. **Reachability.** Starting from the output node, walk connections
   backwards. Only reachable nodes are compiled. *Unreachable islands are
   excluded, not rejected* -- a half-built side chain does not block
   compiling the part that is complete.
3. **Required inputs.** Every reachable node must have all of its
   `expected_input_count()` inputs connected. A missing input fails the
   compile, since the node would otherwise read undefined data.
4. **Cycle detection and ordering.** Kahn's algorithm over the reachable
   subgraph. If nodes remain unprocessed, there is a cycle and compilation
   fails. Among ready nodes, the one with the **smallest `NodeId.index`** is
   taken first, so the same graph always compiles to the same step order.
5. **Buffer assignment.** Each step is given an output buffer; buffers are
   reused once no later step still reads them (below).
6. **Plan construction.** Nodes are cloned, buffers are allocated at the
   plan's maximum block size, and the result is sealed into an immutable
   `GraphExecutionPlan`.

### Buffer reuse

Intermediate buffers are assigned by liveness. A buffer is released the
moment its last consumer has run, and that slot can be reused by a later
step.

- Free lists are keyed by **channel count**; a mono buffer is never handed to
  a stereo output.
- A step's **output slot is allocated before its inputs' slots are
  released.** A node therefore never writes into a buffer it is still
  reading from, which keeps `Process()` free of aliasing concerns.

This is why a long chain of gains needs only a handful of buffers. In the
[baseline](#performance-baseline), a 35-step linear graph uses 4 buffers.

### Node identity across compilation

The plan records, for each step, the `NodeId` it was compiled from.
`FindStepIndexForNode(NodeId)` maps a control-side `NodeId` to a step index
(returning the maximum `uint32` for an unknown node). This is how
[parameter commands](#parameter-commands) find the right cloned node inside
the active plan.

## Plan activation and lifetime

### Slot state machine

`GraphPlanStore` holds a fixed number of slots (`max_graph_plans`). Each slot
has an atomic state, an atomic generation, and the plan itself.

```
        BuildAndStage()                 TryActivate()             a later TryActivate()
 kFree ----------------> kStaged ----------------------> kActive ------------------------> kFree
   ^                        |                                                                |
   |  ReleaseStagedPlan()   |                                                                |
   +------------------------+                                                                |
   ^                                                                                         |
   +-----------------------------------------------------------------------------------------+
        (slot is reused only by a later BuildAndStage(), on the control thread)
```

| Transition | Thread | What it does |
| --- | --- | --- |
| `kFree -> kStaged` | Control | `BuildAndStage()` compiles into a free slot. Checks for a free slot *before* compiling. |
| `kStaged -> kFree` | Control | `ReleaseStagedPlan()`, only if activation was never submitted. |
| `kStaged -> kActive` | Audio | `TryActivate(handle)` on `kActivateGraphPlan`. |
| `kActive -> kFree` | Audio | The previous plan is retired when a new one activates. A release store only. |

### The audio thread never destroys a plan

When a new plan activates, the old slot is flipped to `kFree` with a release
store, and **nothing is destroyed**. The old `GraphExecutionPlan` stays in its
slot, intact, until a later `BuildAndStage()` on the control thread assigns a
new plan into that slot -- at which point the control thread destroys the old
one.

So the expensive, potentially allocating, potentially blocking work of
freeing nodes and buffers can never happen inside the audio callback. This is
a structural guarantee, not a convention: the audio-side code has no path
that frees a plan.

### Synchronization protocol

- `TryActivate(handle)` loads the slot's `state` with **acquire** first, then
  reads `generation`. A handle whose generation no longer matches is stale and
  is refused.
- A staged plan is published to the audio thread by the command queue's own
  release/acquire ordering. The plan is fully built before the command is
  pushed.
- Retiring a slot is a **release** store of `kFree`; the control thread's
  acquire load of the state makes sure the audio thread has finished with the
  plan before the slot is reused.
- Only one plan is active at a time.

### Release rule

`ReleaseStagedGraphPlan(handle)` frees a staged plan that was never
activated. Once `ActivateGraphPlan(handle)` has been submitted, the plan
belongs to the audio side until it is retired, and
`ReleaseStagedGraphPlan` returns `kInvalidArgument`. This is tracked with a
control-only `activation_submitted` flag, so a plan the audio thread may be
about to pick up is never freed out from under it.

### Failure behavior

- **Compilation fails** (cycle, missing input, no output, no free slot): a
  `Status` is returned, `graph_compilation_failures` is incremented, and
  nothing changes. The currently active plan keeps playing.
- **Queue full** while submitting activation: `kQueueFull`; the plan stays
  staged and can be retried or released.
- **Stale handle** at activation (the slot was released and reused): the
  audio thread refuses it and the active plan is unaffected.
- **Runtime shutdown** with plans staged or activation commands pending: all
  plans are destroyed on the control thread during shutdown; no audio thread
  is running at that point.

## Parameter commands

Live changes go through the same bounded SPSC command queue as everything
else. They are value-based and trivially copyable.

| Command | Handle method | Applies to |
| --- | --- | --- |
| `kSetNodeGain` | `SetGain` | `GainNode`, `OscillatorNode` |
| `kSetNodeFrequency` | `SetFrequency` | `OscillatorNode` |
| `kSetNodeCutoff` | `SetCutoffHz` | low-pass / high-pass nodes |
| `kSetNodePan` | `SetPan` | `PanNode` |
| `kSetNodeDelayFrames` | `SetDelayFrames` | `DelayNode` |
| `kActivateGraphPlan` | `ActivateGraphPlan` | the plan store |

On the audio side, the node is located with `FindStepIndexForNode`, then the
parameter is applied with a `dynamic_cast` to the expected node type. The
cast happens once per command, at block granularity, never per sample. A
command aimed at a node of the wrong type, or at a node that is not in the
active plan, is ignored.

### Command ordering

Commands are FIFO. Each command is applied **against whichever plan is active
at the moment it is dequeued**, and a plan switch takes effect before any
node runs in that block. Consequences:

| Sequence submitted | Result |
| --- | --- |
| `SetNodeGain(n)`, then `Activate(plan B)` | Gain applied to plan A's copy of `n`. Plan B has the *graph-time* value of `n`. |
| `Activate(plan B)`, then `SetNodeGain(n)` | Gain applied to plan B's copy of `n`. |
| `SetNodeGain(n)` on a node not in the active plan | Ignored. |

Because compilation clones the control-side node, a parameter command sent
through a handle after compiling does **not** retroactively change an
already-staged plan. If a change must survive a recompile, apply it to the
graph's node before compiling as well as through the handle.

## Channel model

Each node declares its input and output channel counts. The graph is
channel-strict: `Connect` rejects a connection whose source output channel
count differs from the destination's input channel count. There is no
implicit up- or down-mixing; use `PanNode` (mono to stereo) or a `MixerNode`
of the appropriate width.

`GraphExecutor::Render` accumulates the plan's output into the device buffer.
If the plan's output channel count does not match the device buffer, or the
block is larger than the plan's compiled maximum, `Render` is a graceful
no-op rather than a failure: the callback produces silence for the graph
instead of crashing or reading out of bounds.

## Integration with the runtime render path

One render callback now has three sources, run in a fixed order:

1. **Phase 2 legacy tone.** *Overwrites* the output buffer.
2. **Phase 3 voice/bus mixer.** *Accumulates* into the buffer.
3. **Phase 4 graph.** *Accumulates* into the buffer.

Commands are drained first and offered to each owner (render state, voice
pool, bus system, graph). Each recognizes only its own command types. The
graph's output is added at unity gain, so an application can use any
combination of the three sources at once. Because the legacy tone overwrites,
it must stay first.

## Capacities

Everything the audio thread touches is fixed-size and preallocated.

| Limit | Value | Where set |
| --- | --- | --- |
| Nodes per graph | 64 | `GraphConfig::max_nodes` |
| Connections per graph | 256 | `GraphConfig::max_connections` |
| Inputs per node | 8 | `kMaxNodeInputs` (fixed) |
| Plan slots (staged + active + retired) | 4 | `RuntimeConfig::max_graph_plans` |
| Frames per block | the maximum compiled into the plan | plan construction |

When the plan slots are exhausted, `CompileAndStageGraph()` fails with a
`Status` before doing any compile work; release a staged plan, or let a
retired one be reclaimed by a later compile.

## Real-time safety verification

`tests/unit/runtime/realtime_safety_test.cc` runs the render path under
`AllocationGuard`, which replaces global `operator new` / `operator delete`
and fails the test if the guarded thread allocates. Graph cases:

- linear, multi-source, and branching graphs;
- stateful nodes (delay, high-pass) across multiple blocks;
- parameter commands against an active plan;
- plan replacement, including the deferred destruction of the retired plan;
- rendering with no active plan, or with a plan that is staged but not
  activated.

Each test also asserts the output is **non-silent**, so a graph that
silently did nothing cannot pass "allocation-free" vacuously.

A complementary test, `GraphCompilationAllocatesOnTheControlSide`, asserts
that compilation *does* allocate. This guards the guard: if the allocation
tracking were ever disabled, the zero-allocation assertions elsewhere would
pass for the wrong reason.

## Diagnostics

`RuntimeStats` gains four Phase 4 fields:

| Field | Written by | Meaning |
| --- | --- | --- |
| `graph_compilation_failures` | Control thread | Compile attempts that returned an error. |
| `graph_activation_count` | Audio thread | Successful plan activations. |
| `active_graph_node_count` | Audio thread | Steps in the active plan. |
| `active_graph_plan_generation` | Audio thread | Generation of the active plan's slot. |

All are relaxed atomics, read through `AudioRuntime::stats()`. The
audio-thread fields are written only on activation, not per block.

## Concurrency verification

`tests/concurrency/audio_runtime_graph_concurrency_test.cc` runs a real
render thread against the control thread:

- `RepeatedRecompileAndActivateUnderConcurrentRendering`: the control thread
  repeatedly recompiles and activates while the render thread runs.
- `NodeParameterCommandStormAgainstActivePlan`: a high rate of parameter
  commands against the active plan.
- `ShutdownWithStagedAndPendingPlansAfterConcurrentActivity`: shutdown with
  staged and in-flight plans after concurrent activity.

The tests keep the single-producer/single-consumer discipline strict: one
control thread submits, one render thread consumes, and `Start` / `Stop` /
`Shutdown` are never called while the render thread is live. Queue-full
rejections are retried (`RetryUntilOk`, `CompileWithRetry`) rather than
treated as failures.

ThreadSanitizer result: **[record the result of the TSan run here once
confirmed]**

Run it with a TSan configure of the project and
`lavanda_concurrency_tests`, as for Phase 2 and 3.

## Performance baseline

Measured on arm64 macOS, Release, 512 frames at 48 kHz (10,666.7 us block
budget), via `benchmarks/graph_benchmark`:

| Scenario | Steps | Buffers | Plan KiB | Compile us | Render us (median) | Render p99 us | Of budget |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| linear | 5 | 4 | 12.0 | 3.8 | 4.54 | 8.54 | 0.043% |
| linear-long (32 gains) | 35 | 4 | 12.0 | 10.7 | 9.62 | 15.46 | 0.090% |
| branching | 7 | 6 | 16.0 | 3.0 | 6.21 | 7.25 | 0.058% |
| converging | 10 | 6 | 16.0 | 4.0 | 11.96 | 14.79 | 0.112% |
| multi-source (8 osc) | 12 | 11 | 26.0 | 5.1 | 20.71 | 33.92 | 0.194% |

Observations:

- Oscillators dominate render time (`std::sin` on doubles). The 8-oscillator
  scenario costs about 4x the 5-step linear one, almost all of it sine
  evaluation.
- Buffer reuse works: 35 steps need 4 buffers.
- Compilation is a control-side cost of single-digit microseconds at these
  sizes, and is not on the audio thread.
- These are a baseline on one machine, not a guarantee. Treat them as the
  number to compare against when optimizing.

The benchmark is off by default. Enable it with `-DLAVANDA_BUILD_BENCHMARKS=ON`
at configure time. A `GraphBenchmarkSmoke` ctest runs `graph_benchmark 64 50`
to make sure it keeps building and running. Only a Release build gives
meaningful numbers; the benchmark banner reports whether it was compiled
optimized.

## Public vs internal types

Rule: **anything a public type exposes, or an application must name, lives in
`include/`.** Everything else stays under `src/`.

Public (`include/lavanda/graph/`): `NodeId`, `AudioNode`,
`NodeProcessContext`, `kMaxNodeInputs`, `AudioGraph`, `GraphConfig`,
`GraphNodeHandle`, `GraphPlanHandle`, and all built-in nodes under `nodes/`.

Internal (`src/lavanda/graph/`): `GraphCompiler`, `GraphExecutionPlan`,
`GraphPlanStore`, `GraphExecutor`.

Two layering mistakes were found and fixed during Phase 4, and both are worth
remembering as patterns:

1. **`GraphPlanHandle` was first defined next to the plan store**, an
   internal header. The public `Command` needs to carry it, so the public
   header ended up including an internal one. The handle moved to
   `include/lavanda/graph/graph_plan_handle.h`.
2. **The node classes were first placed under `src/`.** Unit tests could
   build them (the test targets have `src/` on their include path), but an
   application using only the installed headers could not construct a
   graph. They moved to `include/lavanda/graph/nodes/`, and the example
   `examples/dsp_graph` builds against the umbrella header
   `lavanda/lavanda.h` alone, which is what catches this class of bug.

## Known limitations

- **Recompile resets node state.** Delay lines empty and oscillators restart
  at phase zero when the topology is replaced. There is no state hand-off
  between the old and new plan.
- **No crossfade on plan switch.** Replacing a plan is an instantaneous
  switch at a block boundary; a sample discontinuity is possible if the two
  graphs differ audibly at that instant.
- **No parameter smoothing.** A node parameter command takes effect at the
  start of the block. Fast gain or frequency jumps can click.
- **Parameter commands use `dynamic_cast`.** It runs once per command, not
  per sample, and does not allocate, but it is RTTI on the audio thread. A
  typed command-dispatch table would remove it.
- **A parameter command does not reach a staged plan.** It addresses the
  active plan only. See [Command ordering](#command-ordering).
- **Channel-strict connections.** No implicit up/down-mixing.
- **One active graph.** There is a single graph per runtime.
- **Single-threaded execution.** Steps run sequentially on the audio thread.
- **Fixed capacities.** Limits come from `GraphConfig` / `RuntimeConfig` and
  are not grown at runtime.
- **Maximum 8 inputs per node.**
- **Transcendental math is scalar.** The oscillator uses `std::sin`; no SIMD
  or wavetable.

## Out of scope

Not in Phase 4, and not implied by this design:

- crossfading or state migration between plans;
- parameter smoothing and automation;
- sample-accurate scheduling of parameter changes;
- multiple simultaneous graphs, or a graph per voice;
- parallel / multithreaded graph execution;
- SIMD-optimized nodes and wavetable oscillators;
- higher-order filters (biquads, EQ), reverb, dynamics, and other effects;
- user-defined nodes loaded as plugins;
- spatial audio, asset/resource management, streaming, MIDI;
- additional platform backends.
