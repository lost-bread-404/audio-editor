# Benchmarks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOUND_SEG_BENCH=ON
cmake --build build -j

# one run
./build/benchmarks/bench 5000

# several runs, merged (this is what CI uses)
python3 benchmarks/run.py ./build/benchmarks/bench --iters 5000 --repeat 7 -o current.json
python3 benchmarks/compare.py baseline.json current.json

# how cost grows with input size
./build/benchmarks/bench_scaling
```

## Why percentiles instead of an average

Sort every measured operation from fastest to slowest. p50 is the middle
one, p99 is the one 99% of operations beat.

An average hides the tail. If 99 operations take 2 us and one takes
2 ms, the average is 22 us, which describes no operation that actually
happened. The one slow call is what a user notices, and averaging makes
it disappear. p99 is designed to keep it visible.

What each number is for:

- **p50** — did the code get slower for everyone? Stable and cheap to
  measure. This is the number to gate CI on.
- **p99** — did the code grow a new occasional slow path? An extra
  allocation, a lock, a cache miss shows up here first. Needs many more
  samples to be stable.
- **max** — a single sample. Almost always the OS scheduler, not your
  code. Record it, never gate on it.

## Measurement rules this harness follows

1. **Setup and teardown run outside the timer.** Most of these
   operations are destructive, so each sample needs a fresh track.
   Building it costs more than the operation itself.
2. **Warmup runs first.** The first calls pay for page faults, allocator
   arena setup, and a cold instruction cache. Real costs, but not what
   is being measured.
3. **A fresh context per sample.** State from a previous iteration would
   change the next one's cost.
4. **Clock overhead is reported.** If an operation's p50 is close to the
   cost of reading the clock, the measurement is mostly noise.
   `track_length_64k` on an idle laptop sits near that line.
5. **`-O2` is forced** even in a Debug build. Unoptimised timings
   measure nothing anyone will ever run.
6. **The whole run is repeated and the median taken.** One run gives a
   distribution of operations; repeating gives a distribution of
   distributions, which is what you need before claiming a number moved.

## Known scaling behaviour

Measured with `bench_scaling`, both linear:

- `ss_track_read` costs about **4 ns per node** in the track,
  independent of how many samples are read. `tr_find` walks the list
  from the head. A track split into 8192 nodes takes ~29 us to read its
  last chunk, versus ~230 ns at 32 nodes.
- `ss_track_delete` costs about **4 ns per track in the context**,
  because `tr_find_child` scans every track for aliases. Deleting from
  one track in a context holding 512 unrelated tracks takes ~2.1 us
  versus ~130 ns in an empty context.

Neither is a bug. Both are consequences of the linked-list design, and
both are fine at the sizes the library is tested at. They are recorded
here so the limits are known rather than discovered by a user.
