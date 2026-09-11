# Voices and Mixing

This document describes Lavanda's Phase 3 voice/bus mixer: how voices are
created and identified, how control and audio ownership stay separated,
how mixing and bus routing work, and what's deliberately deferred to
later phases.

## Voice lifecycle

A voice moves through three states, tracked entirely on the audio thread:

```
kInactive -> (CreateVoice) -> kStopped -> (StartVoice) -> kPlaying
                                  ^                            |
                                  +--------- (StopVoice) -------+
                                  |
                             (DestroyVoice)
                                  |
                                  v
                             kInactive
```

`CreateVoice` always lands in `kStopped`, never `kPlaying` -- a caller
must explicitly `Start()` a voice, matching the pattern
`AudioDevice`/`AudioRuntime` themselves already use (`Open()` doesn't
imply `Start()` either). `StartVoice`/`StopVoice` are idempotent: calling
either while already in that state is a no-op, not an error.
`DestroyVoice` is valid from any state -- including `kPlaying`, which is
required, not just tolerated: an application stopping a sound abruptly
(the user closes a menu, a projectile is destroyed) needs to destroy a
still-playing voice without an intermediate stop step. Any command
targeting an already-`kInactive` (or never-created, or stale -- see
below) slot is silently ignored, including a duplicate `DestroyVoice`.

## Handle identity

A `VoiceId` is `{index, generation}` -- never a pointer, never a raw
array index alone, never the address of audio-thread state. `index`
selects a slot in `VoicePool`'s fixed-capacity array; `generation` is
incremented every time that slot is reserved for a new voice. This is
what makes a handle captured before a slot was destroyed and reused
structurally incapable of controlling whatever now occupies that slot --
the audio thread compares the command's `generation` against the slot's
current one and rejects any mismatch, rather than trusting the index
alone. `BusId` is the identical shape, independently, for
`BusSystem`'s pool -- a distinct type so a voice handle and a bus handle
can never be accidentally interchanged at a call site.

**Why this is race-free without a round-trip acknowledgment.** The
control side (`VoicePool::ReserveSlot`) and the audio side
(`VoicePool::ApplyCommand`'s `kCreateVoice` handler) each keep their own
independent generation counter for a given index, and never read each
other's. The only thing that makes them agree is one fact already
established in Phase 2: `CommandQueue` is strictly FIFO. A caller always
submits `kDestroyVoice` before it can ever submit a `kCreateVoice` that
reuses that index (`ReleaseSlot` only frees an index for reuse after
`Destroy()`'s submit succeeds -- see "Voice destruction and queue
pressure" below), so the audio thread is guaranteed to process them in
that same order. The audio side's generation, once adopted from a
`kCreateVoice` command, can therefore never diverge from what the control
side predicted it would be. No lock, no shared array, no acknowledgment
message -- just ordering plus a number.

**The one deliberate exception to strict generation matching:**
`kCreateVoice` itself is accepted *without* checking the slot's current
generation against anything -- see the comment in
`VoicePool::ApplyCommand`. A create is never "stale" by construction: the
control side always sends a strictly larger generation than whatever was
last used at that index, so there is no meaningful sense in which a
`kCreateVoice` command could be "the wrong one" for its target slot.

## Control/audio ownership split

Exactly the same shape Phase 2 established for `RenderState`, applied
independently to both `VoicePool` and `BusSystem`:

```
VoicePool                          BusSystem
  control_slots_ (control thread)    control_slots_ (control thread)
  audio_slots_   (audio thread)      audio_slots_   (audio thread)
```

Each class keeps two separate arrays, never touched by the "wrong"
thread. `ReserveSlot()`/`ReleaseSlot()` (control-thread-only methods)
read and write only `control_slots_`. `ApplyCommand()` and every
render-path method (audio-thread-only) read and write only
`audio_slots_`. The only channel between the two sides, for both classes,
is a `VoiceId`/`BusId` riding inside a `Command` through `CommandQueue` --
there is no other path, and in particular no shared array either side
reads directly.

## Voice source

Phase 3's only sound source is a fixed internal sine oscillator, one per
voice slot (`VoicePool::AudioSlot::phase`/`frequency_hz`). This is
explicitly temporary -- `VoicePool::RenderVoiceSource`'s own doc comment
says so -- existing solely to prove that multiple independently
controlled voices can coexist and be mixed correctly. Phase 5's
`AudioAsset`/`AudioDecoder`/`AudioResource`/`AudioStream` will replace
this outright, not extend it: a voice's relationship to its source is
already the seam Phase 5 needs (`Audio Source -> Voice -> Bus -> Master
Bus`, per the Phase 3 spec's own architectural principle), it just isn't
built out yet.

## Voice gain and pan

Gain is a plain linear multiplier: `output = source * gain`. `0.0`,
`1.0`, and values above `1.0` are all valid and unclamped inside
`VoicePool`/`BusSystem` themselves -- silent normalization would make a
caller's gain value mean something other than what they set it to.
Rejection of clearly invalid values (negative, non-finite) happens at the
control boundary, in `Voice::SetGain`/`Bus::SetGain`, before a command is
ever submitted -- never inside `VoicePool`/`BusSystem`'s command
handlers, which apply whatever value arrives without further validation
(see "Real-time safety" below for why that split matters).

## Panning law

Equal-power panning, not linear. A naive linear pan (`left = 1-p, right =
p`) has `left^2 + right^2 = 0.5` at center versus `1.0` at either hard
extreme -- an audible loudness dip as a sound pans through the middle.
Equal-power panning (`left = cos(theta), right = sin(theta)`, `theta`
mapped from `[-1, 1]` to `[0, pi/2]`) keeps `left^2 + right^2 = 1.0` at
every pan position; `mixer_math_test.cc`'s
`TotalPowerIsConstantAcrossPanPositions` test asserts this invariant
directly, not just at the three named positions (hard left/center/hard
right). `EqualPowerPan` clamps out-of-range `pan` values to `[-1, 1]` as
a last-resort safety net -- the primary rejection of non-finite pan
values happens earlier, at `Voice::SetPan`'s control boundary.

## Channel model

Phase 3 supports exactly one configuration: each voice's oscillator
renders mono, and the mixer accumulates into stereo output
(`AccumulateMonoToStereo`, `mixer_math.h`). The mixer requires
`output.channel_count() == 2` to run at all -- a runtime opened with a
non-stereo device configuration doesn't error, it simply has its Phase 3
voice/bus layer sit out every render block (the Phase 2 legacy tone is
unaffected either way; see "Coexistence with the Phase 2 tone" below).
Arbitrary multichannel routing was never attempted -- it doesn't fall
naturally out of the current `AudioBuffer`/`ChannelLayout` design, and
the spec explicitly scopes Phase 3 to this one combination.

## Mixer semantics

Accumulation is a plain sum: `mixed = voice_a + voice_b + voice_c + ...`,
with gain multiplication happening at each stage as samples flow
upward. The full order, matching `mixer_math_test.cc`'s worked
examples (which mirror the Phase 3 spec's own 0.25+0.50=0.75 and
0.5-gain/1.0-gain=0.625 examples exactly):

```
voice source * voice gain * pan law
    -> accumulated into target bus
target bus's accumulated contents * bus gain
    -> accumulated into master
master's accumulated contents * master gain
    -> summed into the render callback's output buffer
```

Clipping/limiting is deliberately absent. Master output stays
floating-point and can exceed `[-1.0, 1.0]` if enough loud voices
accumulate -- an explicit contract, not an oversight, and specifically
not something to work around with an ad hoc limiter (the spec is
explicit that a limiter is later DSP work, not a Phase 3 patch). What
happens to an out-of-range sample from there is the device conversion
path's concern, outside this document's scope.

## Bus hierarchy and routing

Exactly two levels: `voice -> (a user bus, or master directly) ->
master`. No bus routes to another non-master bus -- there is no
nesting beyond this one layer. This makes the hierarchy acyclic by
construction: with only one possible non-master destination for any
bus's output (master itself), there is no path along which a cycle could
ever be expressed, so there is nothing to validate against and no graph
traversal of any kind in the render path.

The master bus (`kMasterBusId`, index 0) always exists, is created once
in `BusSystem`'s constructor, and can never be created, destroyed, or
have its reservation released via any command or control-side call --
`ReserveSlot()` skips index 0 by construction (it starts permanently
`in_use`), and `ApplyCommand`'s `kCreateBus`/`kDestroyBus` handlers
reject master's index outright. `Bus::Destroy()` also rejects a
master-targeting call before ever submitting a command -- defense in
depth: the control-side check gives immediate feedback, the audio-side
check protects against any other path a `kDestroyBus` targeting master
could arrive by.

## Bus destruction and voice reassignment

If a bus is destroyed while voices still target it, those voices are not
explicitly walked and reassigned at the moment of destruction. Instead,
every voice's `target_bus` is resolved fresh, every render block, via
`BusSystem::ResolveBusIndex` -- which falls back to master's index
whenever the requested bus isn't currently active, for any reason
(destroyed, or a `BusId` that was never valid to begin with). This
achieves the same observable behavior a special-cased reassignment step
would (a voice targeting a gone bus continues into master, not silence)
without any cross-coupling between `VoicePool` and `BusSystem`, and
without an extra bounded walk over the voice pool triggered specifically
by `DestroyBus`.

**This lenient resolution is used for exactly one purpose: routing a
voice's audio.** It must never be used to validate a direct bus-mutation
command. `SetBusGain` uses a separate, strict check
(`BusSystem::IsValidTarget` -- exact generation match or reject) for
precisely this reason: if a stale/garbage `BusId` on a `SetBusGain`
command were resolved through the lenient path, it would silently fall
through to mutating *master's* gain -- exactly the "stale handle
accidentally controls the wrong object" failure this whole handle design
exists to prevent. `ResolveBusIndex` and `IsValidTarget` are kept as two
distinctly named functions specifically so this distinction can never be
blurred by accident at a future call site.

## Buffer strategy

Every buffer the render path touches is allocated exactly once, at
construction time, never resized or reallocated afterward:

- Each bus's accumulation buffer (`BusSystem::AudioSlot::accumulator`) is
  sized to `max_frames_per_block` (from `RuntimeConfig`) in `BusSystem`'s
  constructor. `kCreateBus`/`kDestroyBus` touch only that slot's scalar
  fields (`active`, `generation`, `gain`) -- never the buffer itself.
  This was a real bug caught during design, not a hypothetical: an
  earlier draft reset a reused bus slot with `slot = AudioSlot{}`, which
  move-assigns a freshly-default-constructed (empty) `AudioBuffer` into a
  slot whose buffer was already correctly sized -- deallocating real heap
  memory, on the audio thread, inside a command handler.
- The mono per-voice scratch buffer (`AudioRuntime::Impl::mix_scratch_`)
  is similarly sized once, at `AudioRuntime` construction.
- `AudioBuffer::View(frame_count)` (a Phase 3 addition to Phase 1's
  `AudioBuffer`, purely additive) lets a buffer preallocated to its
  maximum size be viewed at whatever smaller size an individual render
  callback actually needs, without allocating a differently-sized buffer
  per callback.

If an actual render block's frame count ever exceeds
`max_frames_per_block`, the mixer sits out that block entirely (same
"Phase 2 tone unaffected" behavior as the non-stereo case above) rather
than writing past a preallocated buffer.

## Render order

Once per block, in order: clear every active bus's accumulator -> render
the Phase 2 legacy tone directly into the output buffer -> for each
active voice, render its raw mono source, compute its pan-law gains, and
accumulate into its resolved target bus -> accumulate every active
non-master bus's contents (scaled by that bus's gain) into master ->
apply master's own gain -> sum master's contents into the output buffer
(which already holds the legacy tone). Because the bus hierarchy has only
one level below master, this fixed two-pass order (voices into buses,
then buses into master) is sufficient -- there is no deeper hierarchy
that would require a topological sort or any other graph-ordering logic.

## Coexistence with the Phase 2 tone

Phase 2's single global test tone (`RenderState`, driven by
`kStartTone`/`kStopTone`/`kSetFrequency`/`kSetGain`) was left completely
unmodified by Phase 3 -- not refactored into "just another voice," even
though that would arguably be a tidier design. The reason is
backward compatibility, concretely: `render_state_test.cc` constructs
`lavanda::RenderState` directly and calls its methods independent of any
runtime, and the Phase 3 spec's own completion criteria require "existing
Phase 2 tests still pass." Restructuring `RenderState` into the voice
system would have broken those tests at the source level, not just
behaviorally.

The practical consequence: this engine now has two structurally
independent ways to produce sound in the same render callback, whose
outputs are summed together in the final buffer -- the legacy tone
renders (overwriting `output`) strictly before the voice/bus mixer's
master-bus result is accumulated (added) on top. Getting that order
backwards would silently erase the tone; it's called out explicitly here,
and in a code comment at the call site, specifically so it's never
"fixed" by someone who doesn't know why the order matters.

## Voice destruction and queue pressure

`Voice::Destroy()` submits `kDestroyVoice` and, **only if that submit
succeeds**, releases the voice's control-side pool reservation and
invalidates the handle (`is_valid()` becomes `false` immediately). If
`Submit()` fails (`kQueueFull`), the reservation is deliberately left in
place and the handle remains valid -- so a caller can safely retry
`Destroy()` once queue pressure clears, rather than risking a slot being
marked "free" for reuse while its old occupant might still be audible on
the audio thread (the reservation and the actual audio-thread state are
two different things, and the reservation must never outrun the command
that would make the audio thread agree the slot is free).

One direct consequence, confirmed by a real bug caught while writing
tests for this: because the handle invalidates the instant `Destroy()`
succeeds, calling any other method on that same `Voice` object
afterward (`Stop()`, `SetGain()`, etc.) is rejected immediately by
`Voice`'s own `is_valid()` check -- the call never reaches the audio
thread's generation check at all. Proving the audio thread's own
stale-generation rejection (as opposed to `Voice`'s client-side guard)
requires bypassing `Voice` and submitting a raw `Command` carrying a
captured, pre-destroy `VoiceId` directly -- see
`AudioRuntimeMixingTest.DestroyedVoiceStaleHandleCannotAffectNewVoice` and
the hardware integration test's step 11 for exactly this pattern.

## Diagnostics

`RuntimeStats` gained five Phase 3 fields: `active_voice_count`,
`active_bus_count`, `voice_creation_failures`, `bus_creation_failures`,
`command_failures`. The first two are written only by the audio thread
(as a free byproduct of loops the render path already runs -- counting
active voices/buses costs nothing extra), the latter three only by the
control thread (inside `CreateVoice`/`CreateBus`/`Submit`'s failure
paths) -- every individual field still has exactly one writer, which is
what keeps plain relaxed atomics sufficient even though `RenderDiagnostics`
as a whole now has writers on both threads. One accepted, minor
limitation: `active_voice_count`/`active_bus_count` only update when the
mixer actually runs for a given block -- if it ever sits out (non-stereo
output, oversized frame count), these two fields simply hold their last
value rather than reporting zero or stale-but-marked-as-such.

A `CreateVoice()` failure caused by a full command queue increments both
`command_failures` (the raw "a `Submit()` call failed" counter) and
`voice_creation_failures` (the business-level "`CreateVoice()` failed"
counter) -- both are simultaneously true statements about what happened,
so both counters incrementing together is correct, not a double-count
bug.

## Real-time safety

Every new render-path method obeys the rules Phase 2 established:
`VoicePool::ApplyCommand`/`RenderVoiceSource`, `BusSystem::ApplyCommand`
and its accumulator/clear methods, and every function in `mixer_math.h`
perform no allocation, no blocking, no I/O, no synchronous logging, and
complete in bounded time regardless of how many voices/buses are
active versus how many are configured as the pool's maximum capacity --
skipping an inactive slot costs one branch, not a wasted full render.

This is checked, not just documented: `RealtimeSafetyTest` was extended
with `PopulatedMixerRenderPerformsNoHeapAllocation` (four active voices,
mixed pan positions, one routed through a user bus),
`DrainingVoiceAndBusCommandsPerformsNoHeapAllocation`, and
`RepeatedRenderBlocksWithActiveVoicesStayAllocationFree` (fifty
consecutive render blocks under one `AllocationGuard` scope) -- all
passing, meaning the populated Phase 3 mixer genuinely allocates zero
heap memory on the audio thread, not merely "was designed to."

Validation of gain/pan/frequency values happens exclusively at the
control boundary (`Voice`/`Bus`'s methods in `mixing.cc`) -- rejecting a
negative gain there costs a branch and an early return, no allocation.
`VoicePool`/`BusSystem`'s command handlers never validate; they apply
whatever value already passed that boundary, which is what keeps the
audio-thread side of every command handler trivially bounded.

## Concurrency

Verified under real contention, not just reasoned about: unit tests plus
`tests/concurrency/audio_runtime_concurrency_test.cc`'s three stress
tests (rapid voice create/destroy against a real concurrent render
thread, rapid gain/pan changes on persistent voices, and a deliberate
queue-saturation-then-recovery cycle), all passing under both
AddressSanitizer/UndefinedBehaviorSanitizer (the standard `debug` preset)
and a dedicated ThreadSanitizer build -- zero data races reported across
the full suite. Every one of these tests preserves `CommandQueue`'s
single-producer/single-consumer contract: exactly one thread ever
submits commands, exactly one thread ever renders, for the duration of
any given test -- see the file's own header comment for why that's a
structural requirement of the test design, not an incidental choice.

## Known limitations

- The Phase 2 legacy tone and the Phase 3 voice/bus mixer are two
  independent sound sources whose outputs are summed -- see "Coexistence
  with the Phase 2 tone" above.
- `active_voice_count`/`active_bus_count` only update when the mixer
  actually runs for a given block.
- No clipping/limiting on master output -- floating-point values outside
  `[-1.0, 1.0]` are a valid, unhandled outcome of enough loud voices
  accumulating.
- No sample-accurate command scheduling, unchanged from Phase 2 --
  voice/bus commands still apply at whichever render block they're
  drained in.
- Exactly one sound source (a fixed sine oscillator) and exactly one
  channel configuration (mono voice into stereo output) -- both
  deliberate Phase 3 scope limits, not oversights.

## What remains out of scope

Per the Phase 3 specification: a general DSP graph or `AudioNode`
hierarchy, arbitrary DSP effects, filters, reverb, compressors, an asset
manager or resource cache, filesystem-backed playback, streaming, a
sample-accurate scheduler, spatial audio, HRTF, plugins, MIDI, SIMD
optimization, a custom allocator, and additional hardware backends. The
bus hierarchy stays exactly two levels deep (no arbitrary graph, no
cycles, no dynamic graph compilation) and voice capacity stays a fixed
pool established before rendering begins (no voice stealing, no runtime
container resizing) -- both deliberate Phase 3 boundaries, left for
whichever future phase actually needs to cross them.
