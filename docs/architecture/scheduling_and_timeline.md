# Sample-Accurate Scheduling, Timeline, and Automation

Phase 6 adds a frame-accurate timeline to the Phase 2 runtime. An
application can ask for a voice, bus, or graph-node change to happen at an
exact audio frame, including in the middle of a device callback, and can ask
for gain to move smoothly between two values over a number of frames.

The one rule that shapes the design:

> **Time is a count of rendered frames, owned by the audio thread. A scheduled
> event is data waiting in a bounded, preallocated structure until the frame
> it names is reached; reaching it changes no thread, allocates nothing, and
> blocks on nothing.**

## Contents

1. [Core idea](#core-idea)
2. [The timeline](#the-timeline)
3. [Scheduling an event](#scheduling-an-event)
4. [What can be scheduled](#what-can-be-scheduled)
5. [Validation](#validation)
6. [Execution inside a callback](#execution-inside-a-callback)
7. [Ordering rules](#ordering-rules)
8. [Late events](#late-events)
9. [Capacity and backpressure](#capacity-and-backpressure)
10. [Lifecycle: Stop, Shutdown, restart](#lifecycle-stop-shutdown-restart)
11. [Gain ramps](#gain-ramps)
12. [Threading contract](#threading-contract)
13. [Diagnostics](#diagnostics)
14. [Choosing a lead time](#choosing-a-lead-time)
15. [Real-time safety and verification](#real-time-safety-and-verification)
16. [Public vs internal types](#public-vs-internal-types)
17. [Known limitations](#known-limitations)
18. [Out of scope](#out-of-scope)

## Core idea

Three pieces cooperate, and each is used by exactly one side.

| Piece | Domain | Role |
| --- | --- | --- |
| Frame clock | Audio thread writes, any thread reads | Monotonic count of frames rendered so far |
| Command queue (SPSC) | Control pushes, audio pops | Carries immediate and scheduled items alike, in order |
| Event heap | Audio thread only | Fixed-capacity min-heap of admitted events |

```
control thread                                audio thread (device callback)
--------------                                ------------------------------
P = frame_position()
ScheduleAtFrame(cmd, P + lead)
    validate; reserve a slot
    push {cmd, target, epoch}  --queue-->     drain: immediates applied now,
                                                      scheduled items -> heap
                                              render: split block at due events,
                                                      apply each at its frame
```

Immediate commands (`Submit()`, the `Voice` / `Bus` setters) and scheduled
commands share one queue, so relative order between the two kinds is the
order in which the control thread submitted them.

## The timeline

`lavanda/core/time.h` defines two opaque integer types:

- `FramePosition`: an absolute point on the timeline (`enum class : uint64_t`).
- `FrameCount`: a distance between two points.

Helpers: `ToInteger()`, `AddSaturating(position, count)`,
`FramesBetween(from, to)`, `kMaxFramePosition`, `FramesToSeconds()`.

`AudioRuntime::frame_position()` returns the position at which the **next**
callback will begin: the total frames rendered so far. Properties:

- **Monotonic and never reset.** `Stop()`, `Start()`, and `Shutdown()` +
  restart do not move it backwards.
- **Callback intervals are half-open.** A callback that begins at frame `S`
  and renders `N` frames owns `[S, S + N)`. An event at `S + N` belongs to the
  next callback.
- **Submission guarantee.** A command submitted after `frame_position()`
  returned `P` takes effect at frame `P` or later, never earlier. The clock is
  published with a release store and read with an acquire load.
- **Readable from any thread.**

The timeline counts frames, not wall-clock time. Convert with the sample rate
you opened the device with: `frames = seconds * sample_rate_hz`.

## Scheduling an event

```cpp
const lavanda::FramePosition now = runtime.frame_position();
const lavanda::FramePosition when = lavanda::AddSaturating(
    now, lavanda::FrameCount{12000});  // ~0.25 s at 48 kHz

lavanda::Command command{.type = lavanda::CommandType::kSetVoiceGain,
                         .value = 0.5f,
                         .voice_id = voice.id()};

lavanda::Status status = runtime.ScheduleAtFrame(command, when);
```

`ScheduleAtFrame()` returns:

| Code | Meaning |
| --- | --- |
| `kOk` | Accepted. It will be applied at `when`, or late (see below). |
| `kInvalidArgument` | The command type is not schedulable, or its ids or value fail [validation](#validation). Nothing was reserved. |
| `kResourceExhausted` | `max_scheduled_events` events are already pending. |
| `kQueueFull` | The command queue is full; retry after the audio thread drains. |
| `kDeviceUnavailable` | The runtime was constructed without a device. |

An accepted event is never lost: it will either execute, be discarded by a
`Shutdown()` (see [Lifecycle](#lifecycle-stop-shutdown-restart)), or, only if
the heap were somehow full, be counted as dropped.

There is no cancel operation. See [Known limitations](#known-limitations).

## What can be scheduled

| Group | Commands |
| --- | --- |
| Voice | `kStartVoice`, `kStopVoice`, `kSetVoiceGain`, `kSetVoicePan`, `kSetVoiceFrequency`, `kRampVoiceGain` |
| Bus | `kSetBusGain`, `kRampBusGain` |
| Graph node | `kSetNodeGain`, `kSetNodeFrequency`, `kSetNodeCutoff`, `kSetNodePan`, `kSetNodeDelayFrames` |

Not schedulable, by design:

- `Create*` / `Destroy*`: they change which slots exist, and the handle they
  return must exist before anything can be scheduled against it.
- `kActivateGraphPlan`: plan activation keeps its own staged-plan protocol.
- `kSetVoiceBus`: it changes routing topology, not a parameter.
- The legacy Phase 2 tone commands.

A scheduled node event is delivered to **whichever plan is active when the
event executes**, not the plan that was active when it was scheduled. If a plan
swap happens in between, the event applies to the new plan's node with that
`NodeId`, or is consumed harmlessly if there is none.

## Validation

`ScheduleAtFrame()` validates before it reserves anything, so a rejected call
leaves no trace:

- ids are structurally valid (`is_valid()`);
- gains and ramp targets are finite and `>= 0`;
- pan is finite;
- frequency and cutoff are finite and `> 0`;
- delay is finite and `>= 0`.

Any `duration_frames` is valid. Whether an id still refers to a live object is
decided **at execution**, by the same generation checks immediate commands
use: an event aimed at a voice, bus, or node that has since been destroyed or
replaced is consumed, counted as executed, and has no effect.

## Execution inside a callback

For each callback the runtime:

1. reads the block start frame `S` from the clock;
2. adopts a new session epoch if there is one (see
   [Lifecycle](#lifecycle-stop-shutdown-restart));
3. drains the command queue (at most `command_queue_capacity` items):
   immediate commands are applied at once, at frame `S`; scheduled items are
   admitted into the heap;
4. applies every event due at `S`;
5. renders the block in **segments**: it finds the next event at frame `E`
   inside the block, renders `[cursor, E)`, applies every event due at `E`,
   and continues, until the block is done;
6. advances the clock by the block size, once, at the end.

The mixer and graph render each segment through a sub-view of the output
buffer, so DSP state (oscillator phase, filter memory, delay lines) continues
unbroken across a segment boundary. Rendering a block in one piece or in many
segments produces the same samples; the test suite checks this bit for bit.

If a device delivers a callback larger than `max_frames_per_block`
(default 4096), the block is rendered as a single segment: events due at its
start are applied, and events that fall inside it are applied at the start of
the next callback and counted as late.

## Ordering rules

1. **Immediate before scheduled, at the same frame.** All immediate commands
   drained in a callback are applied at the block start, before any scheduled
   event due at that frame.
2. **Scheduled events at the same frame run in submission order.** Each
   admitted event gets a sequence number equal to its queue position; the heap
   orders by `(target_frame, sequence)`.
3. **Earlier frames first.** Events execute in target order, not submission
   order.
4. **Every event is applied before the first sample of its frame is
   rendered.** An event at frame `T` first affects the sample at `T`. A
   frequency change at `T` is first visible at sample `T + 1`, because the
   oscillator output at `T` is computed from the phase before the increment;
   a voice started at `T` outputs `sin(0) = 0` at `T` and is first non-zero at
   `T + 1`.

## Late events

An event is **late** when its target frame is before the start of the
callback that admits it. This happens when the target is already in the past
when scheduled, or the control thread stalled between reading the clock and
pushing the event, or an oversized block was rendered as one segment.

Policy: a late event is **not** dropped and not rejected. It is applied at the
start of the next callback, counted in `scheduled_events_late`, and its
lateness (`frame_applied - target`) updates
`scheduled_events_max_lateness_frames`.

Because the audio thread admits queued events at the beginning of every
callback, an event pushed with a lead of more than one callback can only be
late if the control thread was delayed by more than that lead.

## Capacity and backpressure

Two bounded resources, both preallocated, both sized from `RuntimeConfig`:

| Setting | Default | Bounds |
| --- | --- | --- |
| `command_queue_capacity` | 64 | Items in flight between control and audio (immediate and scheduled) |
| `max_scheduled_events` | 256 | Events accepted and not yet executed or discarded |

`max_scheduled_events = 0` disables scheduling: every `ScheduleAtFrame()` is
rejected.

How they interact:

- A slot in `max_scheduled_events` is **reserved by the control thread before
  the push** (a compare-and-swap on a counter) and released by the audio
  thread when the event executes or is discarded, or by the control thread if
  the push fails. Admission therefore cannot overshoot, and an accepted event
  always has a heap slot waiting for it.
- A scheduled item also occupies a queue slot until the audio thread drains
  it, which happens at the beginning of the next callback.
- **Before `Start()`, or while stopped, nothing drains.** The number of events
  you can submit in such a burst is limited by `command_queue_capacity`, not
  by `max_scheduled_events`. Start the runtime first, or size the queue for the
  burst.
- Rejection is explicit: `kResourceExhausted` for the event cap, `kQueueFull`
  for the queue. Neither blocks. The caller decides whether to retry.

## Lifecycle: Stop, Shutdown, restart

| Operation | Pending events | Clock |
| --- | --- | --- |
| `Stop()` | **Retained.** They execute after `Start()` when the clock reaches them. Scheduling while stopped is accepted. | Frozen while stopped; not reset |
| `Shutdown()` | Still counted as pending until the next session starts | Not reset |
| `Start()` after `Shutdown()` | **Discarded** by the audio thread in the first callback of the new session | Continues from where it was |

`Shutdown()` advances a session epoch. Each queued item carries the epoch it
was submitted under; the audio thread adopts the new epoch in its first
callback, flushes the heap, and discards any queued item from an older epoch.
Discards are counted in `scheduled_events_discarded`.

The practical consequence: **capacity is returned to the application when the
next session starts rendering, not when `Shutdown()` returns.**
`scheduled_events_pending` stays elevated in between.

## Gain ramps

`Voice::RampGain(target, duration_frames)` and `Bus::RampGain(...)` move gain
linearly. They are also schedulable (`kRampVoiceGain`, `kRampBusGain`), which
is how to start a ramp at an exact frame.

- **Closed form, evaluated per sample from the absolute frame.** Frame
  `start` has the starting value; frame `start + duration` has the target
  exactly; in between the value is linear. Because it depends only on the
  absolute frame, the result does not depend on how the block was segmented.
- **A ramp starts at the frame the command is applied:** the target frame for a
  scheduled ramp, the next callback start for an immediate one.
- **A replacement ramp starts from the exact current value** at the moment it
  is applied, with no jump.
- **`SetGain()` cancels any ramp** and steps to the new value.
- **`duration_frames = 0` is a step.**
- **Constant gain takes the old code path** and is bit-identical to Phase 3
  and Phase 5 mixing; the ramped path is used only while a ramp is in flight.
- Targets must be finite and `>= 0`; the same validation as `SetGain()`.

Pan, frequency, and graph-node parameters are stepped, not ramped.

## Threading contract

- **Control-side calls must be externally serialized.** `ScheduleAtFrame()`,
  `Submit()`, and the `Voice` / `Bus` / `GraphNodeHandle` setters feed a
  single-producer queue. They may be called from different threads over time,
  but never concurrently; use your own mutex or a single control thread. The
  concurrency tests exercise exactly this: several threads taking turns
  through one lock while the audio thread renders.
- **Safe from any thread at any time:** `frame_position()`, `stats()`,
  `is_running()`.
- **The audio thread** touches the heap, the sequence counter, and the epoch
  it has adopted; no other thread does.

## Diagnostics

`RuntimeStats` gained:

| Field | Meaning |
| --- | --- |
| `scheduled_events_accepted` | Calls to `ScheduleAtFrame()` that returned ok |
| `scheduled_events_executed` | Events dispatched (even if the target id was stale) |
| `scheduled_events_late` | Events applied after their target frame |
| `scheduled_events_discarded` | Events dropped by an epoch change (Shutdown/restart) |
| `scheduled_events_dropped` | Events that reached the audio thread but found the heap full |
| `scheduled_events_capacity_failures` | Calls rejected with `kResourceExhausted` |
| `scheduled_events_pending` | Reservations currently outstanding |
| `scheduled_events_max_lateness_frames` | Largest observed lateness |

In a quiescent snapshot (no call in flight, audio thread idle):

```
accepted == executed + discarded + dropped + pending
```

While the audio thread is running, a snapshot can be momentarily
inconsistent, because the counters are separate atomics: an event can be
executed before the control thread has recorded it as accepted. Do not assert
the identity on a live snapshot; use it after the runtime has settled.

`dropped` should stay at zero: the heap has exactly `max_scheduled_events`
slots, and every accepted event holds a reservation for one.

## Choosing a lead time

Schedule far enough ahead that the audio thread drains the event before its
frame arrives. As guidance (not a measured limit): at least two device
callback periods, plus however long your control thread can be delayed. At a
512-frame buffer and 48 kHz one callback is about 10.7 ms. Tens of
milliseconds is comfortable for a responsive application; a sequencer should
schedule a fraction of a second ahead and refill continuously.

If `scheduled_events_late` is non-zero, the lead is too short or the control
thread is stalling; `scheduled_events_max_lateness_frames` says by how much.

## Real-time safety and verification

The scheduled and ramp paths perform no allocation, locking, or blocking on
the audio thread: the heap and queue are preallocated, reservations are
atomic counters, and ramp evaluation is arithmetic.

What is checked, and how:

- **Timing and ordering:** unit tests on the scheduler and on event timing
  across segment boundaries, using a deterministic fake device.
- **Bit-exact equivalence:** for every schedulable operation, a scheduled
  render equals "pump to frame T, submit the same command immediately, pump the
  rest", bit for bit, across several block layouts.
- **No allocation:** the scheduled and ramp paths run under `AllocationGuard`
  with zero allocations, including a full heap, a burst of mixed events, ramps,
  node events across a plan swap, and a Shutdown discard on the audio thread.
- **Concurrency:** tests with a real render thread cover racing producers,
  exact capacity exhaustion, Stop/Start cycles while scheduling, and
  Shutdown/restart discard and capacity return. One of them records every
  rendered block and asserts that a scheduled start lands on exactly the
  expected sample.
- **Hardware:** hardware-gated tests run the same flow against the real Core
  Audio device. They check the clock, event execution, lateness, and the
  lifecycle; they cannot check audio content.

## Public vs internal types

Public:

- `FramePosition`, `FrameCount` and helpers (`lavanda/core/time.h`)
- `AudioRuntime::ScheduleAtFrame()`, `AudioRuntime::frame_position()`
- `CommandType::kRampVoiceGain`, `kRampBusGain`; `Command::duration_frames`
- `Voice::RampGain()`, `Bus::RampGain()`
- `RuntimeConfig::max_scheduled_events`
- the new `RuntimeStats` fields

Internal: `QueuedCommand`, `EventScheduler`, `EventReservations`, `GainRamp`,
the segment loop, the session epoch.

`Command` gained `duration_frames` after `asset_id`; designated initializers
must follow that order.

## Known limitations

- **No cancel.** A scheduled event cannot be withdrawn once accepted. To undo
  one, schedule a counteracting event, or `Shutdown()` and restart.
- **Ramps are linear and gain-only.** Voice and bus gain only. `GainNode`
  parameters, pan, frequency, and cutoff have no ramp; node gain ramps are
  deferred.
- **`kSetVoiceBus` is not schedulable.**
- **Events are not renderable offline.** The timeline is the live device clock.
- **`DelayNode` maximum.** A `DelayNode` writes the input sample and then reads
  at `(write + max - delay) % max`; with `delay == max` it reads the sample it
  just wrote, so the effective maximum delay is `max - 1` frames and the
  clamp in `SetDelayFrames` is one frame too generous. This predates Phase 6
  and applies equally to immediate and scheduled changes.
- **Oversized callbacks** (above `max_frames_per_block`) are not split, so
  events inside them are late by up to one callback.
- **Core Audio buffer size.** `AudioDevice::buffer_size_frames()` reports the
  requested size, not necessarily the size the device uses; the scheduler does
  not depend on it.
- Phase 5 limitations carry over unchanged (see
  [`resources_and_assets.md`](resources_and_assets.md#known-limitations)).

## Out of scope

Looping and pausing, seeking within assets, tempo maps and musical time,
cancelling or rescheduling events, offline rendering, crossfading between graph
plans, ramps on node parameters, automation curves other than linear,
multi-producer control APIs, streaming (Phase 8), MIDI, plugins, and
additional backends.
