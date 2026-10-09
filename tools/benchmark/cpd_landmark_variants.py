#!/usr/bin/env python3
"""METHOD-077: run plan of the Nystroem landmark-variant experiment for Bayesian CPD.

Runs IntrinsicCoherentPointDriftLandmarkVariants once per cell, one cell at a time, with a hard
timeout per run (a timeout is recorded as unevaluated, not as a failure). Per (fixture, seed) the
exact run goes first and writes the reference positions the other cells are compared with.
Results are appended to <out>/results.jsonl; cells already recorded there are skipped, so an
interrupted run resumes without overwriting anything.

  cpd_landmark_variants.py --runner <binary> --out <dir> --positions <dir> [--plan] [--only <cell>]
"""
import argparse
import json
import pathlib
import subprocess
import sys

FIXTURES = ["full", "partial"]
SEEDS = [1, 2, 3]
POINTS = 100_000
TIMEOUT_SECONDS = 600
VARIANTS = [
    "points/mixed/random", "points/fixed/random", "points/resampled/random",
    "box/mixed/random", "box/fixed/random", "box/resampled/random",
    "box/mixed/halton", "box/fixed/halton", "box/resampled/halton",
]


def plan():
    """Cells in execution order: (cell id, runner arguments without paths, writes reference)."""
    cells = []
    for fixture in FIXTURES:
        for seed in SEEDS:
            base = ["--fixture", fixture, "--seed", str(seed), "--points", str(POINTS), "--threads", "16"]
            cells.append((f"{fixture}/{seed}/exact", base + ["--variant", "exact"], True))
            for variant in VARIANTS:
                cells.append((f"{fixture}/{seed}/{variant}", base + ["--variant", variant], False))
            # Timing repeat of the baseline (within-run timing noise).
            cells.append((f"{fixture}/{seed}/points/mixed/random#repeat", base + ["--variant", "points/mixed/random"], False))
            # Negative control: 2 landmarks, every approximation accepted.
            cells.append((f"{fixture}/{seed}/negative_control",
                          base + ["--variant", "points/mixed/random", "--landmarks", "2", "--error-limit", "1.0"], False))
    return cells


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--positions", required=True, help="directory for the exact runs' reference positions")
    parser.add_argument("--plan", action="store_true", help="print the cells and exit")
    parser.add_argument("--only", help="run only this cell id")
    args = parser.parse_args()
    cells = plan()
    if args.plan:
        for cell, arguments, _ in cells:
            print(cell, " ".join(arguments))
        print(f"{len(cells)} cells, timeout {TIMEOUT_SECONDS} s each")
        return 0
    out = pathlib.Path(args.out)
    positions = pathlib.Path(args.positions)
    out.mkdir(parents=True, exist_ok=True)
    positions.mkdir(parents=True, exist_ok=True)
    results = out / "results.jsonl"
    done = set()
    if results.exists():
        for line in results.read_text().splitlines():
            done.add(json.loads(line)["cell"])
    for cell, arguments, writes_reference in cells:
        if (args.only and cell != args.only) or cell in done:
            continue
        fixture, seed = cell.split("/")[:2]
        reference = positions / f"{fixture}_{seed}_exact.bin"
        command = [args.runner, *arguments, "--out", str(out / "last.json")]
        if writes_reference:
            command += ["--positions", str(reference)]
        elif not reference.exists():
            record = {"cell": cell, "outcome": "unevaluated", "reason": "no exact reference for this fixture and seed"}
            with results.open("a") as stream:
                stream.write(json.dumps(record) + "\n")
            continue
        else:
            command += ["--reference", str(reference)]
        try:
            completed = subprocess.run(command, capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
            record = {"cell": cell, "outcome": "completed" if completed.returncode == 0 else "failed",
                      "returncode": completed.returncode}
            if completed.returncode in (0, 1):
                record["result"] = json.loads((out / "last.json").read_text())
            else:
                record["stderr"] = completed.stderr[-2000:]
        except subprocess.TimeoutExpired:
            record = {"cell": cell, "outcome": "unevaluated", "reason": f"timeout after {TIMEOUT_SECONDS} s"}
        with results.open("a") as stream:
            stream.write(json.dumps(record) + "\n")
        print(cell, record["outcome"], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
