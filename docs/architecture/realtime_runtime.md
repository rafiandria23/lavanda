# Real-Time Runtime

This document describes Lavanda's Phase 2 real-time runtime: the
orchestration layer built on top of Phase 1's `AudioDevice` boundary that
lets ordinary application code control audio rendering safely from outside
the audio thread.

## Why a separate execution domain

The central principle of this phase: **control code and real-time audio
processing are separate execution domains**, with fundamentally different
rules.

The **control domain** (ordinarily the application's own thread, or
whichever thread calls into `AudioRuntime`) may allocate, lock, do I/O,
log, and call arbitrary code -- the same freedoms any normal application
code has.

The **real-time domain** is whatever thread the platform backend invokes
`RenderCallback` on (see "Thread ownership" below -- Lavanda does not spawn
this thread itself). It operates under strict, non-negotiable constraints:
no dynamic allocation, no deallocation, no filesystem or network I/O, no
synchronous logging, no blocking mutexes, no condition-variable waits, no
sleeping, no unbounded-time operations, and no calls into arbitrary
application code. These aren't style preferences -- audio hardware expects
a new buffer of samples on a fixed schedule (e.g. every ~10ms for a
512-frame buffer at 48kHz), and any of the above can make a callback miss
that schedule, which is audible as a click, pop, or dropout.

**Why not just let the control thread write directly into state the audio
thread reads?** Because "usually works" is not the same guarantee as
"provably correct." A frequency field written by one thread and read by
another, with no synchronization, is a data race -- undefined behavior in
C++, not just a theoretical concern; under contention it can produce torn
reads (a value that was never actually set by either thread) or, on some
platforms/optimization levels, be reordered or elided by the compiler
entirely. Reaching for `std::atomic<float>` everywhere sidesteps torn
reads, but doesn't solve the *ordering* problem: if a caller sets frequency
and then gain, expecting the audio thread to observe both together, plain
independent atomics offer no guarantee it sees them as a pair rather than
frequency-updated-but-not-yet-gain, or vice versa. That's why Lavanda's
default mechanism is command transfer through `CommandQueue`, not shared
atomics: a `Command` carries one complete, ordered intent, applied
atomically-in-effect by the single thread that owns `RenderState`. Atomics
are used exactly twice in this codebase (`AudioClock`'s frame counter,
`AudioRuntime::Impl::is_active_`), and both are deliberate, narrow
exceptions -- single independent scalars where "eventually consistent, no
compound invariant to preserve" is genuinely sufficient. See "Real-time
state ownership" below.

## Thread ownership

Phase 1 already established that `CoreAudioDevice` does not spawn its own
thread -- Core Audio owns the real-time thread and invokes whatever
`RenderCallback` was installed via `AudioDevice::Open()`. Phase 2 does not
change this. There is no `std::thread` anywhere in `AudioRuntime`; "the
audio thread" in this document means, concretely, whichever thread the
active backend calls `RenderCallback` on.

This matters for how `AudioRuntime::Stop()`/`Shutdown()` map onto Phase
1's lifecycle: they don't need to signal and join a Lavanda-owned thread,
because there isn't one. They rely on the guarantee Phase 1's own
architecture doc already establishes: after `AudioDevice::Stop()` returns,
no further callbacks will fire. See "Shutdown sequence" below.

## Command flow

```
Application / control thread
        |
        | AudioRuntime::Submit(Command)
        v
   CommandQueue::TryPush()   -- bounded, may fail (see below)
        |
        v
   [ audio thread's next callback ]
        |
        | CommandQueue::TryPop(), drained in a loop
        v
   RenderState::ApplyCommand()
        |
        v
   RenderState::Render()  -- writes the output buffer
```

`Submit()` never blocks and never allocates on the control side beyond
what `Command`'s value semantics already imply (nothing -- it's a
trivially copyable struct). Every command in the queue at the start of a
render callback is drained in a single bounded loop before rendering that
block, so command effects always apply before the audio they affect --
this is also how "multiple commands in one render block" (spec-required
test case) is naturally satisfied: the queue has no per-callback limit on
how many commands it will drain, only a fixed total capacity.

## Queue saturation policy

`CommandQueue::TryPush()` returns `false`, with no side effects, if the
queue is at capacity. `AudioRuntime::Submit()` translates that into
`Status(ErrorCode::kQueueFull, ...)`.

**The control thread never blocks waiting for room, and the audio thread
is never made to wait on the control thread.** This was a deliberate
choice among the options the Phase 2 spec allows (reject-and-report, drop
silently, or block the control thread): silently dropping a command would
hide saturation from a caller who might reasonably want to know their
command didn't take effect, and blocking the control thread trades one
kind of unresponsiveness for another without actually protecting the
real-time thread (which is the one guarantee that cannot be compromised).
Reject-and-report keeps saturation *observable* -- a caller can check the
returned `Status`, and `AudioRuntime::stats()` exposes queue-adjacent
diagnostics for monitoring -- without ever risking the real-time
guarantee.

Default queue capacity is 64 commands (`AudioRuntime`'s constructor
parameter, overridable per-instance). At typical buffer sizes (e.g. 512
frames at 48kHz, ~10.7ms per callback), 64 commands is a large margin
above what a normal control flow would submit between two callbacks --
saturation in practice signals either a caller submitting in a tight loop
without checking results, or a genuinely stalled audio thread, both worth
surfacing rather than masking.

## Real-time state ownership

```
Control thread            Audio thread
     |                         |
     | owns: nothing           | owns: RenderState (frequency,
     |   persistent --         |   gain, is_playing, oscillator
     |   Submit() is            |   phase), AudioClock,
     |   stateless from the    |   RenderDiagnostics
     |   caller's side          |
     |                         |
     +---- CommandQueue -------+
        (transfers intent,
         never mutates
         RenderState directly)
```

`RenderState` is constructed once, inside `AudioRuntime::Impl`, and is
only ever touched from within `Render()` -- by definition, only from the
audio thread, since `Render()` is the function installed as
`RenderCallback`. The control thread has no pointer, reference, or other
access path to `RenderState` at all; it can only submit `Command` values
through `CommandQueue`. This is what makes `RenderState`'s fields safe to
leave as plain, unsynchronized `float`/`bool`/`double` members (see
`src/lavanda/runtime/render_state.h`) rather than atomics -- there is
structurally only ever one thread that can observe or mutate them.

The two places this codebase *does* use `std::atomic` are narrow,
deliberate exceptions, not a default policy:
- `AudioClock`'s frame counter -- a single independent monotonic counter,
  written only by the audio thread, optionally read by the control thread
  for diagnostics. No compound invariant across multiple fields to
  preserve.
- `AudioRuntime::Impl::is_active_` -- a single boolean gate checked at the
  top of every `Render()` call, so that a `Stop()`/`Shutdown()` racing
  against an in-flight callback is guaranteed to be observed by the
  *next* callback even if the backend's own `Stop()` hasn't fully
  silenced the thread yet. Deliberately checked *and set* before the
  underlying `AudioDevice::Stop()` call, not after -- see "Shutdown
  sequence".

## `RenderContext`

A small, stack-constructed value built fresh inside every `Render()` call
(`src/lavanda/runtime/render_context.h`): the output buffer view, sample
rate, current clock frame, and a non-owning pointer to `RenderState`.
Never heap-allocated, never retained past the callback that constructs it.
Deliberately minimal -- it is not, and must never become, an
"everything object" that every future real-time component reaches into;
new real-time-path responsibilities should get their own owner, not a new
field bolted onto this struct.

## `AudioClock`

Frame-based, not wall-clock: `current_frame()` increases by exactly
`frame_count` on every `AudioClock::Advance()` call, which `Render()`
calls once per callback with that callback's actual frame count.
`std::chrono` is never the audio timeline's source of truth -- it *is*
used, correctly, in exactly one other place: `RenderDiagnostics` measures
each callback's *wall-clock duration* (for underrun/deadline accounting,
see below), which is a genuinely different question from "where are we in
the audio stream" and appropriately answered with a different clock.

`AudioClock::SetSampleRate()` is called once, from the control thread,
immediately after a successful `Open()` and strictly before the audio
thread can be invoked -- it updates only the rate used by
`ElapsedSeconds()`; `current_frame()` itself is never reset, including
across a `Shutdown()` followed by a new `Start()`.

## Render loop

Each `Render()` invocation, in order:

1. Check `is_active_`. If false (a `Stop()`/`Shutdown()` is in flight or
   has completed), clear the output buffer and return immediately --
   nothing else below executes.
2. Record the callback's start time (`std::chrono::steady_clock`, for
   diagnostics only).
3. Drain every currently-available command from `CommandQueue` in a
   bounded loop, applying each via `RenderState::ApplyCommand()`.
4. Build a `RenderContext` and call `RenderState::Render()`, which writes
   the deterministic test signal (or silence, if not playing) into the
   output buffer.
5. Advance `AudioClock` by the callback's frame count.
6. Record the callback's duration into `RenderDiagnostics`.

Every step is bounded: the command-drain loop terminates as soon as the
queue reports empty (capacity is finite, so this cannot run unboundedly
even under sustained submission), and every other step is a fixed-cost
operation over a fixed-size buffer.

## Shutdown sequence

```
control thread: AudioRuntime::Shutdown()
        |
        v
  is_active_.store(false)         -- observed by the very next callback,
        |                             even one already in flight
        v
  AudioDevice::Stop()             -- per Phase 1's own contract: no further
        |                             callbacks fire once this returns
        v
  AudioDevice::Close()            -- native resources released
        |
        v
  (safe to destroy RenderState, CommandQueue, AudioClock,
   RenderDiagnostics -- nothing can be observing them)
```

`is_active_` is set to `false` *before* `AudioDevice::Stop()` is called,
not after -- so a callback that races in during the `Stop()` call itself
observes the flag and bails out via step 1 of the render loop, rather than
potentially reading `RenderState`/`AudioClock` mid-teardown. This is
belt-and-suspenders on top of Phase 1's own guarantee (no callbacks after
`Stop()` returns), not a substitute for it -- both hold simultaneously.

`AudioRuntime`'s destructor calls `Shutdown()` unconditionally, so a
runtime that goes out of scope while running still shuts down safely
rather than leaking device resources or leaving a dangling callback
pointer.

## Callback lifetime

The lambda installed as `RenderCallback` in `Impl::Start()` captures
`this` (an `Impl*`) by value. Its lifetime safety rests on one invariant,
enforced structurally rather than by convention: **`AudioRuntime` is
non-copyable and non-movable**, and `Impl` lives on the heap behind a
`std::unique_ptr` owned by the outer `AudioRuntime`. Because the
`unique_ptr` can never be copied, moved elsewhere, or have its pointee
relocated, `Impl`'s address is stable for exactly as long as the owning
`AudioRuntime` exists -- and `AudioRuntime`'s destructor calls
`Shutdown()`, which (per the sequence above) guarantees no callback can
be in flight or fire again, *before* `impl_` itself is destroyed as part
of ordinary member teardown. There is no window in which the installed
callback can execute against a destroyed `Impl`.

`RenderState`, `CommandQueue`, `AudioClock`, and `RenderDiagnostics` are
all plain members of `Impl` (not separately heap-allocated, not
separately owned) -- their lifetime is exactly `Impl`'s lifetime, so the
same argument covers all of them without needing a separate case for
each.

## Diagnostics

`RenderDiagnostics` (`src/lavanda/runtime/render_diagnostics.h`, internal)
accumulates render count, missed-deadline count, last/max render
duration, and last callback frame count -- all via relaxed atomics with
exactly one writer (the audio thread, via `RecordRender()`, called once
per callback) and potentially many readers (`AudioRuntime::stats()`,
callable from any thread). A render counts as "missed" when its measured
wall-clock duration exceeds its frame budget (`frame_count /
sample_rate_hz`) -- an approximate signal that a callback may have risked
an underrun, not a guarantee one occurred (the platform backend, not
Lavanda, is the true authority on whether an underrun actually reached the
hardware).

`RecordRender()` performs no logging and no I/O -- it only ever writes to
fixed-size atomic counters. This is why diagnostics are *reportable*
(`RuntimeStats` is plain, copyable data the control thread can log, print,
or forward however it likes) without the *recording* step itself ever
crossing the real-time-safety line.

## Real-time safety verification

"No heap allocation in the render path" is a testable claim in this
codebase, not only a documented rule: `tests/test_support/allocation_guard.h`
overrides the process's global `operator new`/`operator delete` to count
allocations on the current thread, and
`tests/unit/runtime/realtime_safety_test.cc` exercises a real
`AudioRuntime`'s render callback inside that guard, asserting zero
allocations. This is test-only code, linked into `lavanda_unit_tests`
alone -- never into the `lavanda` library itself, an example, or
production code of any kind.

## Known limitations

- **`SetFrequency` applies with no smoothing.** A frequency change takes
  effect on the very next rendered sample, which can produce an audible
  discontinuity (a click) at the transition. Deliberate for Phase 2, whose
  goal is proving the command/state architecture works, not producing a
  polished signal -- parameter smoothing/anti-click is out of scope here.
- **Missed-deadline accounting is approximate.** It measures this
  process's own wall-clock time in the render path, not whether the
  platform backend actually underran. A callback can measure "fast" and
  still be delivered late by the OS scheduler between callbacks, or vice
  versa.
- **The allocation guard only intercepts unaligned `operator new`/
  `delete`**, not the C++17 aligned-allocation overloads. Not currently a
  gap for anything in Lavanda (nothing needs over-aligned storage), but
  worth remembering if that ever changes.
- **No sample-accurate scheduling.** Commands take effect at the start of
  whichever render block they're drained in, not at a specific sample
  offset within a block. An event scheduler with that precision is
  explicitly Phase 6+ territory per the Phase 2 spec.

## What remains out of scope

Per the Phase 2 specification: mixer, voices, an audio asset/resource
manager, a DSP graph or general node framework, filters, effects, spatial
audio/HRTF, streaming, MIDI, plugin hosting, offline rendering, a
scheduling system or sample-accurate event scheduler, voice
virtualization, a custom allocator, SIMD optimization, additional
hardware backends, a device enumeration system, and game-engine
integration. The only "audio processing" Phase 2 implements is the
minimal deterministic test signal needed to prove the runtime works.
