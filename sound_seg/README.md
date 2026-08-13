# libsound_seg

A C library for editing 16-bit PCM audio without copying sample data.

Segments are held in a linked structure where a region can reference
another region's samples instead of duplicating them, so splitting and
rearranging audio costs pointer work rather than memory. Each node is
24 bytes.

Originally a university assignment, rebuilt as a library: opaque
handles, error codes, explicit contexts, a test suite, benchmarks, and
CI.

## Status

Version 0.1.0. The API may change; the major version is 0 for that
reason. Thread-compatible, not thread-safe (see
[Concurrency](#concurrency)).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

Requires CMake 3.16+ and a C11 compiler. No external dependencies.

### Options

| Option | Default | Effect |
|---|---|---|
| `SOUND_SEG_BUILD_TESTS` | `ON` | Unit tests, run via `ctest` |
| `SOUND_SEG_ASAN` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `SOUND_SEG_BENCH` | `OFF` | Benchmark binaries |
| `SOUND_SEG_FUZZ` | `OFF` | libFuzzer target (clang only) |

## Install and use

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
cmake --install build
```

In the consuming project:

```cmake
find_package(sound_seg 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE sound_seg::sound_seg)
```

A complete working example is in [`examples/consumer/`](examples/consumer),
which is built against the installed library on every CI run.

## Usage

```c
#include <sound_seg/sound_seg.h>

ss_ctx *ctx;
if (ss_ctx_create(&ctx) != SS_OK) {
    return 1;
}

ss_track *track;
ss_track_create(ctx, &track);

const int16_t samples[4] = {10, 20, 30, 40};
ss_track_write(track, 0, 4, samples);

ss_status rc = ss_track_delete(track, 1, 2);
if (rc != SS_OK) {
    fprintf(stderr, "delete failed: %s\n", ss_strerror(rc));
}

ss_ctx_destroy(ctx);   /* frees every track in the context */
```

### Contexts

A context owns a set of tracks. Two contexts are fully independent: a
track in one is invisible to the other, and aliasing checks never cross
the boundary. Mixing tracks from different contexts returns
`SS_ERR_WRONG_CONTEXT` rather than silently corrupting either side.

Create one context per subsystem, or one per test, so unrelated parts of
a program cannot disturb each other.

Only `ss_track_create` takes a context. Every other function finds it
from the track, which makes passing the wrong one impossible.

### Ownership

- `ss_ctx_destroy` frees the context and every track still alive in it.
- `ss_track_destroy` frees one track early. Optional.
- Buffers the library returns (`ss_track_identify`, `ss_wav_load`) are
  released with `ss_free`, not `free`.

### Errors

Every fallible function returns `ss_status`. `SS_OK` is 0; errors are
negative, so they can never be confused with a valid count. Codes are
fixed permanently and new ones are only appended.

`ss_strerror` maps any code to a string.

## Concurrency

**Thread-compatible, not thread-safe.** One context per thread is safe.
Sharing a context across threads is not: the track registry is mutated
without locking by `ss_track_create` and `ss_track_destroy`, and
`ss_track_delete` scans it.

If you need a shared context, guard it externally. Internal locking is
not implemented.

## Performance

`ss_track_read` costs about 4 ns per node in the track, independent of
how many samples are read, because the lookup walks the list from the
head. `ss_track_delete` costs about 4 ns per track in the context,
because the alias check scans every track.

Both are linear and measured, not estimated. See
[`benchmarks/README.md`](benchmarks/README.md) for the methodology, the
scaling curves, and why the numbers are percentiles rather than
averages.

## Development

CI runs on every pull request:

| Job | Checks |
|---|---|
| `build` | 5 combinations of Linux/macOS/Windows and gcc/clang |
| `sanitizers` | ASan + UBSan over the test suite |
| `warnings` | `-Werror` |
| `install` | Installs, then builds an external project against it |
| `coverage` | Line coverage report |
| `benchmark` | Compares against the merge base, fails on regression |

Fuzzing runs nightly, not per-PR, because it is slow:

```bash
cmake -S . -B build-fuzz -DSOUND_SEG_FUZZ=ON -DCMAKE_C_COMPILER=clang
cmake --build build-fuzz -j
./build-fuzz/fuzz/fuzz_track corpus/ -max_total_time=60
```

When fuzzing finds a crash, minimise it with `-minimize_crash=1`, turn
it into a test in `tests/`, and only then fix the bug. A crash file is a
temporary artifact; a test is permanent.

## Layout

```
include/sound_seg/   public header -- the only thing users include
src/                 implementation and internal headers
tests/               unit tests, run by ctest
benchmarks/          latency and scaling harnesses, comparison scripts
fuzz/                libFuzzer entry point
examples/consumer/   standalone project that consumes the installed lib
cmake/               package config template for find_package
```

## Licence

MIT. See [LICENSE](LICENSE).
