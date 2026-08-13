#!/usr/bin/env python3
"""Compare two benchmark runs and fail if anything got meaningfully slower.

Exit code 1 means "regression". That is what makes CI go red.
"""
import argparse
import json
import sys


def load(path):
    with open(path) as f:
        data = json.load(f)
    return {r["name"]: r for r in data["results"]}, data.get("clock_overhead_ns", 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("current")
    # p50 catches "the code got slower everywhere".
    # p99 is noisier, so it gets a looser budget; it catches "the code
    # grew a new occasional slow path" (an extra allocation, a lock).
    ap.add_argument("--p50-budget", type=float, default=5.0, help="percent")
    ap.add_argument("--p99-budget", type=float, default=15.0, help="percent")
    args = ap.parse_args()

    base, base_clock = load(args.baseline)
    curr, curr_clock = load(args.current)

    failures = []
    print(f"{'benchmark':<20} {'metric':<6} {'base':>10} {'curr':>10} {'delta':>8}")
    print("-" * 60)

    for name, c in curr.items():
        b = base.get(name)
        if b is None:
            print(f"{name:<20} {'NEW':<6}")
            continue
        for metric, budget in (("p50_ns", args.p50_budget),
                               ("p99_ns", args.p99_budget)):
            bv, cv = b[metric], c[metric]

            # A measurement close to the cost of reading the clock is
            # noise. Comparing noise to noise produces random failures,
            # which teach everyone to ignore the benchmark job.
            if bv < 3 * max(base_clock, curr_clock):
                verdict = "noise"
                delta = 0.0
            else:
                delta = (cv - bv) / bv * 100.0
                if delta > budget:
                    verdict = "SLOWER"
                    failures.append(f"{name}/{metric}: +{delta:.1f}% "
                                    f"(budget {budget}%)")
                elif delta < -budget:
                    verdict = "faster"
                else:
                    verdict = "ok"
            print(f"{name:<20} {metric[:-3]:<6} {bv:>10.0f} {cv:>10.0f} "
                  f"{delta:>+7.1f}% {verdict}")

    if failures:
        print("\nPerformance regressions:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nNo regressions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
