# Audio Resources and Assets

Phase 5 adds audio assets: WAV files decoded once on the control side into
immutable, resident sample data, stored in a bounded, generation-checked
store, and played by voices and graph nodes without any file I/O, decoding,
allocation, or unsafe lifetime behavior on the audio thread.

Streaming is **not** part of this phase. It is deferred to Phase 8; see
[Out of scope](#out-of-scope).

## Contents

- [Design summary](#design-summary)
- [Public API](#public-api)
- [Loading pipeline](#loading-pipeline)
- [Asset identity](#asset-identity)
- [Resampling](#resampling)
- [The asset store](#the-asset-store)
- [Lifetime protocol](#lifetime-protocol)
- [Asset-backed voices](#asset-backed-voices)
- [Asset nodes in the graph](#asset-nodes-in-the-graph)
- [Real-time safety](#real-time-safety)
- [Verification](#verification)
- [Known limitations](#known-limitations)
- [Out of scope](#out-of-scope)

## Design summary

```
 Control thread                                   Audio thread
 --------------                                   ------------
 LoadAudioAsset(path)
   read file -> decode -> resample to device rate
   -> insert into ResourceStore
                                  AudioAssetId
 CreateVoice(asset)  --- Pin ---> kCreateVoice(asset_id) ---> ResolveForAudio
 CompileAndStageGraph --- Pin --> plan holds AssetPins        read-only sample data
                                                              render voices / plan
 ReleaseAudioAsset(asset)  -> Retiring (deferred)
 ReclaimAssets()           <---- Unpin (kDestroyVoice, plan retirement)
   free memory when pins == 0
```

The rules the rest of this document elaborates:

1. **Decode once, on the control side.** The audio thread only ever sees
   immutable, already-resampled sample data.
2. **Pins, not ownership transfer.** Anything the audio thread might touch
   (a voice, a staged or active plan) holds a *pin* on its asset. An asset's
   memory is freed only when it is Retiring *and* its pin count is zero.
3. **The control thread pins; the audio thread unpins once.** The audio
   thread never allocates, frees, or takes a lock to do either.
4. **Plans and asset memory are destroyed only on the control thread.**

## Public API

All on `AudioRuntime` unless noted. Control-thread only.

| Call | Behavior |
| ---- | -------- |
| `LoadAudioAsset(path)` | Decode, resample to the open device's rate, and store. Returns an `AudioAssetId`. Requires an open device. Loading a path that is already resident returns the existing id. |
| `GetAudioAssetInfo(id)` | Metadata for a live asset. |
| `ReleaseAudioAsset(id)` | Marks the asset Retiring. New pins and lookups are refused; memory is freed once the last pin is gone (see [Lifetime protocol](#lifetime-protocol)). |
| `ReclaimAssets()` | Frees Retiring assets whose pins are zero and destroys retired graph plans. Also runs automatically at the reclaim points below. |
| `CreateVoice(AudioAssetId)` | An asset-backed voice. The asset is validated and pinned before the create command is submitted. `CreateVoice()` with no asset still creates an oscillator voice. |
| `AddAssetSourceNode(AudioAssetId)` | Adds an `AudioAssetSourceNode` to the graph. |
| `Voice::IsPlaying()` | Control-side view of the voice's published state. Lags queued commands. |

`AudioAssetId` is a generation-checked `{index, generation}` handle, like
`VoiceId`, `BusId`, `NodeId`, and `GraphPlanHandle`: a stale id is rejected,
never silently addressing a reused slot.

`RuntimeConfig::resource_config`:

| Field | Default |
| ----- | ------- |
| `max_assets` | 64 |
| `max_total_bytes` | 256 MiB |
| `max_frames_per_asset` | 48000 x 120 (two minutes at 48 kHz) |
| `max_file_bytes` | 128 MiB |

`RuntimeStats` additions: `resident_asset_count`, `retiring_asset_count`,
`resident_asset_bytes`, `asset_load_failures`. `stats()` stays `const` and
free of side effects. The asset counters are read from the store, so
`stats()` is a **control-thread** call.

## Loading pipeline

`LoadAudioAsset` runs, in order:

1. Validate arguments.
2. Build the asset key (see [Asset identity](#asset-identity)).
3. If the key is already resident, return the existing id.
4. Reclaim retired assets, then check that a free slot exists.
5. Decode the file.
6. Resample to the device rate if it differs.
7. Check the resulting frame count against `max_frames_per_asset`.
8. Insert into the store, which enforces the slot and byte budgets.

Format limits:

- WAV only. Channels: 1 or 2. Maximum source sample rate: 768000 Hz.
- [confirm: list the exact WAV encodings `wav_decoder.cc` accepts; the
  committed fixtures cover 8-bit and 16-bit PCM.]
- Samples are stored as **interleaved 32-bit float**, matching `AudioBuffer`.
  [confirm: record this as the deliberate deviation from the spec's layout
  wording.]

Every failure returns a `Status` and increments `asset_load_failures`.
Nothing is left half-inserted: the slot is taken only at step 8.

## Asset identity

The asset key is **the canonical path, `@`, and the target sample rate**.
Consequences, all deliberate:

- Identity is by *path*, not content. Two paths to identical bytes are two
  assets. On a case-insensitive filesystem, two spellings that canonicalize
  differently may also load twice.
- The same file loaded for two different device rates is two assets, since
  the resampled data differs.
- A file edited on disk after loading is not re-read; the resident asset is
  returned. Release and reload to pick up changes.
- A Retiring asset is invisible to key lookups, so loading a released path
  creates a fresh asset.

## Resampling

Linear interpolation to the device rate. The output frame count is
`lround(frames * target_rate / source_rate)`. The tail holds the last sample.
There is **no anti-aliasing or low-pass filter**: downsampling can alias, and
content near Nyquist is attenuated by the interpolation. This is acceptable
for the stated goals of Phase 5 and is the main fidelity limitation of the
resampler. Assets at the device rate skip resampling entirely.

## The asset store

`ResourceStore` is a fixed array of slots, sized by `max_assets` when the
runtime is built. Each slot has a state (Free, Resident, Retiring), a
generation, an atomic pin count, and the immutable sample data.

- The control thread alone inserts, releases, pins, and reclaims.
- The audio thread only calls `ResolveForAudio(id)` (which validates the
  generation and returns a read-only pointer) and `Unpin(id)`.
- `ResolveForAudio` is valid for as long as a pin is held, **including for a
  Retiring asset**. This is what lets a voice keep playing after its asset
  is released.
- `Unpin` is a single `fetch_sub` with release ordering, guarded against
  underflow and stale ids.
- The slot's generation is bumped when memory is reclaimed, so old ids can
  never resolve to the next occupant.

## Lifetime protocol

Who pins what:

| Holder | Pinned when | Unpinned when |
| ------ | ----------- | ------------- |
| Asset voice | Control thread, in `CreateVoice(asset)`, **before** the create command is submitted (rolled back if submission fails) | Audio thread, when it applies `kDestroyVoice` (or replaces the voice's asset on a re-create) |
| Graph plan | Control thread, at compile time, for each asset node reachable from the output | Control thread, when the plan is destroyed (`AssetPins` is RAII) |

Release policy is **deferred**: `ReleaseAudioAsset` makes the asset Retiring
and refuses new pins and lookups, but playback already holding a pin
continues from resident memory. Memory and the slot are freed at the next
reclaim once pins reach zero.

Reclaim points (all on the control thread):

- `LoadAudioAsset`
- `ReleaseAudioAsset` (after a successful release)
- `CompileAndStageGraph`
- the explicit `ReclaimAssets()`

Notable cases:

- **Release before the create command is applied.** The voice's pin is taken
  before submission, so releasing the asset right after `CreateVoice` is safe:
  the voice still plays. (Covered by a test.)
- **A finished voice keeps its pin** until `Destroy()`. Replay after the end
  needs the asset, so ending playback does not unpin.
- **`Voice` has no RAII.** Dropping a handle without `Destroy()` leaves its
  pin until the runtime shuts down.
- **Plan pins are exception-safe.** `AssetPins::Pin()` reserves vector
  capacity *before* pinning, so a failed allocation can never lose a pin.
  `GraphExecutionPlan` holds its `AssetPins` as its first member, so it is
  destroyed last, after the nodes that read the asset.
- **Retired plans** are destroyed only by the control thread, at reclaim
  points (`GraphPlanStore::ReclaimRetiredPlans()` frees plans left in `Free`
  slots). Their pins release there.

Destruction order inside `AudioRuntime::Impl` is load-bearing: the device
stops first, then the command queue, render state, clock, and diagnostics;
then the **store and loader**; then the voice pool, bus system, scratch
buffers, graph, and plan store. The store must outlive voices and plans,
which hold pins into it, so members are declared in that order.

## Asset-backed voices

The asset id travels on `kCreateVoice`; an invalid id means an oscillator
voice. The audio thread resolves the id into a pointer once, at create time.

Semantics:

- **Play once.** At the end of the asset the voice resets to frame 0 and its
  state becomes Stopped.
- `Stop()` resets the position to 0 and sets Stopped.
- `Start()` while Playing does **not** restart.
- `Start()` is ignored if the asset failed to resolve on the audio thread.
- **No pause and no loop.** (Not in this phase.)
- `SetFrequency()` on an asset voice is a silent no-op.
- State is published per slot as an atomic `(generation << 8) | state`, read
  by the control side through `Voice::IsPlaying()`. It **lags queued
  commands**: right after `Start()`, `IsPlaying()` may still be false.
- The voice renders only when its source's channel count matches the render
  view; on a mismatch it outputs silence and does not advance.

**Mono assets** use the existing equal-power pan.

**Stereo assets** treat `pan` as *balance*: center is unity on both channels,
hard right silences only the left, and the attenuated side follows
`max(0, cos(|pan| * pi / 2))`. The channels are never summed or cross-mixed.
The law lives in `runtime/stereo_balance.h`. Because center is unity per
channel for stereo but about -3 dB per channel for an equal-power mono pan, a
stereo asset at center plays roughly 3 dB louder per channel than a mono
asset at center. This is intentional and means a stereo recording keeps its
original level at pan 0.

Out-of-range `pan` values are valid input and are clamped by the pan law, as
in Phase 3; only non-finite values are rejected.

## Asset nodes in the graph

`AudioAssetSourceNode(asset_id, channel_count)` has no inputs. `AudioNode`
gained `required_asset()` and `BindAsset(const AudioAsset*)` so the compiler
can find and bind assets without knowing node types.

- Each plan activation plays the asset **once, from frame 0**, then outputs
  silence. There is no loop, restart, or gain on the node; compose it with
  `GainNode` and others.
- `Clone()` copies the asset id and shape, **not** the binding or position, so
  a recompiled plan starts fresh from frame 0.
- The compiler is given a `GraphCompileContext` (the resource store and the
  device rate; a rate of 0 means the device is closed). For each *reachable*
  asset node it checks that the asset is available and that its channel count
  and rate match. **Any failure fails the whole compile**, leaving the active
  plan untouched, like every other compile failure. Unreachable asset nodes
  are not pinned.
- Binding happens at compile time on the control thread; the audio thread
  only reads.

## Real-time safety

The audio thread never reads files, decodes, allocates, or locks on behalf of
assets. Evidence:

- `AllocationGuard` tests render asset voices (mono, stereo with balance, on a
  bus, restarted after the end, destroyed with the asset released) and asset
  graph plans (including a plan swap) with **zero heap allocation**, each
  asserting non-silence so a no-op cannot pass vacuously.
- A counter-test shows `LoadAudioAsset` *does* allocate, so the guard is
  known to be live.
- A test deletes the file after loading and still plays the asset, so
  playback cannot depend on the filesystem. (`AllocationGuard` only sees
  `operator new`, so this test covers the gap for I/O that does not allocate.)
- The audio thread's only operations on assets are a generation-checked
  lookup, read-only access to the sample data, and one atomic `fetch_sub` to
  unpin. ThreadSanitizer is clean on these paths (see below).

## Verification

Test files added in Phase 5, all unit tests unless noted:

| File | Covers |
| ---- | ------ |
| WAV decoder, resampler, store, loader tests | Steps 2-5: decoding, resampling, store limits and generations, loader policy |
| `audio_runtime_assets_test.cc` | Runtime asset API |
| `voice_pool_assets_test.cc` | Pool-level asset voices |
| `stereo_balance_test.cc` | Balance law |
| `audio_runtime_asset_voices_test.cc` | Runtime-level asset voices |
| `audio_asset_source_node_test.cc` | Asset node |
| `graph_asset_lifetime_test.cc` | Plans, pins, reclamation |
| `audio_runtime_asset_graph_test.cc` | Graph + runtime |
| `audio_runtime_asset_lifetime_test.cc` | Release and reclaim policy |
| `realtime_safety_assets_test.cc` | Allocation guard and no-I/O evidence |
| `audio_runtime_asset_concurrency_test.cc` (concurrency) | Voice churn, release/reload under pressure, plan swaps, with a real render thread |
| `audio_runtime_asset_integration_test.cc` (integration) | Hardware-gated: voices, balance, replay, and graph with release-while-playing against Core Audio |

Fixtures (`tests/fixtures/audio/`) are tiny, hand-checkable WAVs produced by a
Python generator; the `AudioFixturesUpToDate` test regenerates and compares
them. They are decoder test vectors, not audible content: the hardware test
and `examples/playback` use generated or user-supplied audio instead.

Sanitizer and hardware runs:

- Debug build with AddressSanitizer + UndefinedBehaviorSanitizer: full suite.
- ThreadSanitizer: `lavanda_concurrency_tests`, including the three asset
  scenarios, race-free on macOS arm64 (RelWithDebInfo, `FakeAudioDevice`
  render thread). This does not cover Core Audio's own callback thread.
- Real hardware: the integration test and `examples/playback`.

[confirm: Release and Linux/GCC CI results before marking the phase done.]

## Known limitations

- Linear resampler with no anti-aliasing.
- No pause, loop, seek, or per-voice playback rate.
- Identity is by path, not content (see [Asset identity](#asset-identity)).
- `Voice` has no destructor; a dropped handle keeps its asset pinned until
  shutdown.
- `SetFrequency()` on an asset voice is a no-op, not an error.
- An asset node plays once per plan activation; it cannot restart without a
  new plan.
- `stats()` is control-thread only, because it reads the store.
- `LoadAudioAsset` requires an open device, since the target rate comes from
  it. Assets are resampled once, at load. They are not re-resampled if the
  device is later reopened at a different rate (the graph compiler rejects an
  asset node whose asset rate does not match).

## Out of scope

Deferred, not forgotten:

- **Streaming** (disk-backed, incrementally loaded playback): Phase 8.
- Compressed codecs, and formats other than WAV.
- Looping, pausing, seeking, and variable-rate playback.
- Anti-aliased or higher-quality resampling.
- Hot reload and file watching.
- Spatial audio, MIDI, plugins, and a sample-accurate scheduler (unchanged
  from earlier phases).
