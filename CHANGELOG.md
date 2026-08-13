# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html):
while the major version is 0, the minor version is bumped for breaking
changes.

## [Unreleased]

## [0.1.0]

First release as a library. The assignment-era API was not published, so
everything below is described as new rather than changed.

### Added
- Opaque `ss_track` handle. The struct body is private, so the internal
  layout can change without breaking user builds.
- `ss_status` error codes returned from every fallible function, plus
  `ss_strerror`. Replaces silent no-ops and crashes on bad input.
- `ss_ctx` context object owning a set of tracks. Removes the
  process-global registry, so independent users of the library in one
  process cannot disturb each other.
- `SS_ERR_WRONG_CONTEXT`, returned when an operation would alias tracks
  across a context boundary.
- CMake build with `find_package` support and a version file.
- Unit tests run through CTest, including an error-path suite.
- Benchmark harness reporting percentiles, and a scaling harness
  measuring cost against node and track count.
- libFuzzer target.
- CI: matrix build, sanitizers, `-Werror`, coverage, install
  verification, benchmark regression gate.

### Fixed
Found while adding argument validation; all reachable from the public
API as it was written.
- `pos + len` overflowed `size_t` in the range checks, so requests with
  a very large `pos` passed validation and read out of bounds.
- `tr_identify` computed `target_len - ad_len` unsigned. An ad longer
  than the target wrapped the result and read far past the buffer.
- `tr_delete_range` did not validate its range and walked off the end of
  the list.
- `wav_load` did not check `fopen`, dereferencing NULL when the file was
  missing. Its signature also gave the caller no way to know how large a
  buffer to supply; it now allocates and reports the length.
- `tr_destroy` freed the whole registry array on the first call and set
  a flag that was never cleared, so a later `tr_init` reallocated freed
  memory.
- Allocation failures in node creation were unchecked.
- Signed/unsigned comparison against the `len` bitfield.

### Known limitations
- Thread-compatible, not thread-safe. A context must not be shared
  across threads without external locking.
- Read cost is linear in node count; delete cost is linear in the number
  of tracks in the context. Measured in `benchmarks/README.md`.

[Unreleased]: https://github.com/OWNER/sound_seg/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/OWNER/sound_seg/releases/tag/v0.1.0
