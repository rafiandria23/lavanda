# Contributing to Lavanda

## Code style

Lavanda follows Google's C++ style guide as a baseline, with two
project-specific naming conventions layered on top:

- Headers use `.h`, implementation files use `.cc` (not `.cpp`/`.hpp`).
- Include guards, not `#pragma once`, following the pattern
  `LAVANDA_<PATH>_<FILE>_H_` (e.g. `LAVANDA_DEVICE_AUDIO_DEVICE_H_`).

Run `clang-format` (config in `.clang-format`) before sending a change, and
check `clang-tidy` (config in `.clang-tidy`) against anything you touch
under `include/` or `src/`.

## Structural conventions

- Public headers live under `include/lavanda/`; nothing platform-specific
  is allowed to appear there. Platform backends live under
  `src/lavanda/platform/<backend>/` and are reached only through the
  public abstraction (see `docs/architecture/audio_device.md`).
- Prefer RAII and explicit ownership. Avoid raw owning pointers.
- Errors from ordinary (non-real-time) operations are reported as
  `lavanda::Status` / `lavanda::StatusOr<T>`, not exceptions -- see
  `include/lavanda/core/status.h` for the rationale.
- Nothing on the real-time render path (anything reachable from a
  `RenderCallback`) may allocate, perform I/O, log synchronously, sleep,
  or take an unbounded-time lock. See the class comment on
  `RenderCallback` in `include/lavanda/device/audio_device.h`.
- No `utils/` directory and no catch-all helper classes -- if you're
  reaching for one, the abstraction it belongs to is probably missing.

## Building and testing

The Core Audio backend is opt-in via a CMake option (see the root
`README.md` for why):

```sh
cmake --preset debug -DLAVANDA_BUILD_COREAUDIO_BACKEND=ON
cmake --build --preset debug
ctest --preset debug
```

The same commands work with `release` in place of `debug`. To exercise the
one test that actually starts real audio hardware, set
`LAVANDA_RUN_HARDWARE_TESTS=1`; without it, that test skips itself. To wipe
a build directory entirely (e.g. after switching CMake generators), run
`cmake -P cmake/Clean.cmake` instead of deleting `build/` by hand.

## Commit messages

This project uses a lightweight Conventional-Commits-style prefix
(`feat:`, `fix:`, `test:`, `docs:`, `chore:`, ...) to keep history
skimmable. Keep commits scoped to one logical change.

## Scope discipline

Lavanda is being built in phases (see `docs/architecture/audio_device.md`
for the current phase and what's deferred). Please don't introduce
concepts from a later phase -- a DSP graph, a scheduler, a resource
manager, additional backends -- ahead of the phase that actually needs
them; open an issue to discuss instead.
