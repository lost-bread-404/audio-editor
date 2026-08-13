#!/usr/bin/env python3
"""Run the benchmark binary several times and merge the runs.

One run gives you a distribution of operations. Repeating the whole run
gives you a distribution of *distributions*, which is what you need
before you can say a number changed. We report the median across runs:
the mean would let a single interrupted run drag the result around,
which is the exact problem percentiles exist to avoid.
"""
import argparse
import json
import statistics
import subprocess
import sys

METRICS = ["mean_ns", "p50_ns", "p90_ns", "p99_ns", "p999_ns", "max_ns"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--iters", type=int, default=2000)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("-o", "--output")
    args = ap.parse_args()

    runs = []
    for i in range(args.repeat):
        out = subprocess.run([args.binary, str(args.iters)],
                             capture_output=True, text=True, check=True)
        runs.append(json.loads(out.stdout))
        print(f"run {i + 1}/{args.repeat} done", file=sys.stderr)

    merged = {"clock_overhead_ns": statistics.median(
                  r["clock_overhead_ns"] for r in runs),
              "repeat": args.repeat,
              "iters": args.iters,
              "results": []}

    for idx, case in enumerate(runs[0]["results"]):
        entry = {"name": case["name"], "n": case["n"], "iters": case["iters"]}
        for m in METRICS:
            values = [r["results"][idx][m] for r in runs]
            entry[m] = statistics.median(values)
            # Spread across runs. If this is larger than your CI budget,
            # the budget cannot possibly be enforced: you would be
            # gating on noise.
            entry[m.replace("_ns", "_spread_pct")] = (
                (max(values) - min(values)) / min(values) * 100.0
                if min(values) > 0 else 0.0)
        merged["results"].append(entry)

    text = json.dumps(merged, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
    print(text)


if __name__ == "__main__":
    main()
