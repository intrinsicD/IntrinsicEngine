#!/usr/bin/env python3
"""METHOD-077: run plan of the Nystroem landmark-variant experiment for Bayesian CPD.

Runs IntrinsicCoherentPointDriftLandmarkVariants once per cell, one cell at a time, with a hard
timeout per run (a timeout is recorded as unevaluated, not as a failure). Per (fixture, seed) the
exact run goes first; only a successful, complete exact run leaves a reference
(<out>/references/<fixture>_<seed>.bin, its sha256 recorded), and the other cells of that pair
run only against that recorded reference. Each cell writes its own JSON, which is checked
against the cell before it is accepted. Records (with the git commit and the runner's sha256)
are appended to <out>/results.jsonl; recorded cells are skipped, so an interrupted run resumes
without overwriting anything.

  cpd_landmark_variants.py --runner <binary> --out <dir> [--plan] [--only <cell>]
"""
import argparse
import hashlib
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


def plan(points=POINTS):
    """Cells in execution order: (cell id, runner arguments without paths)."""
    cells = []
    for fixture in FIXTURES:
        for seed in SEEDS:
            base = ["--fixture", fixture, "--seed", str(seed), "--points", str(points), "--threads", "16"]
            cells.append((f"{fixture}/{seed}/exact", base + ["--variant", "exact"]))
            for variant in VARIANTS:
                cells.append((f"{fixture}/{seed}/{variant}", base + ["--variant", variant]))
            # Timing repeat of the baseline (within-run timing noise; counts must repeat bitwise).
            cells.append((f"{fixture}/{seed}/points/mixed/random#repeat", base + ["--variant", "points/mixed/random"]))
            # Floor control: Nystroem never accepted, every iteration takes the exact fallback.
            cells.append((f"{fixture}/{seed}/floor_control",
                          base + ["--variant", "points/mixed/random", "--error-limit", "1e-300"]))
            # Negative control: 2 landmarks, no error rejection (only non-positive sums reject).
            cells.append((f"{fixture}/{seed}/negative_control",
                          base + ["--variant", "points/mixed/random", "--landmarks", "2", "--error-limit", "1e300"]))
    return cells


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def argument(arguments, key, fallback):
    return arguments[arguments.index(key) + 1] if key in arguments else fallback


def matches(result, arguments):
    """The cell's JSON describes the cell that was run."""
    return (result.get("fixture") == argument(arguments, "--fixture", None)
            and str(result.get("seed")) == argument(arguments, "--seed", None)
            and result.get("variant") == argument(arguments, "--variant", None)
            and str(result.get("landmarks")) == argument(arguments, "--landmarks", "256")
            and float(result.get("error_limit")) == float(argument(arguments, "--error-limit", "0.001")))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--plan", action="store_true", help="print the cells and exit")
    parser.add_argument("--only", help="run only this cell id (its exact reference must be recorded)")
    parser.add_argument("--points", type=int, default=POINTS, help="technical tests only; the run plan uses the default")
    args = parser.parse_args()
    cells = plan(args.points)
    if args.plan:
        for cell, arguments in cells:
            print(cell, " ".join(arguments))
        print(f"{len(cells)} cells, timeout {TIMEOUT_SECONDS} s each")
        return 0
    out = pathlib.Path(args.out)
    (out / "cells").mkdir(parents=True, exist_ok=True)
    (out / "references").mkdir(parents=True, exist_ok=True)
    results = out / "results.jsonl"
    records = {}
    if results.exists():
        for line in results.read_text().splitlines():
            record = json.loads(line)
            records[record["cell"]] = record
    commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    runner_sha = sha256(args.runner)
    for cell, arguments in cells:
        if (args.only and cell != args.only) or cell in records:
            continue
        fixture, seed = cell.split("/")[:2]
        is_exact = cell.endswith("/exact")
        reference = out / "references" / f"{fixture}_{seed}.bin"
        record = {"cell": cell, "commit": commit, "runner_sha256": runner_sha}
        exact = records.get(f"{fixture}/{seed}/exact")
        if not is_exact:
            usable = (exact is not None and exact.get("outcome") == "completed" and reference.exists()
                      and sha256(reference) == exact.get("reference_sha256"))
            if not usable:
                if args.only:
                    print(cell, "skipped: no recorded exact reference", flush=True)
                    continue
                record.update(outcome="unevaluated", reason="no successful exact reference for this fixture and seed")
                records[cell] = record
                with results.open("a") as stream:
                    stream.write(json.dumps(record) + "\n")
                print(cell, record["outcome"], flush=True)
                continue
            record["reference_sha256"] = exact["reference_sha256"]
        elif reference.exists():
            reference.unlink()  # never compare against a reference this run did not write
        cell_json = out / "cells" / (cell.replace("/", "_").replace("#", "_") + ".json")
        if cell_json.exists():
            cell_json.unlink()
        command = [args.runner, *arguments, "--out", str(cell_json)]
        command += ["--positions", str(reference)] if is_exact else ["--reference", str(reference)]
        try:
            completed = subprocess.run(command, capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
            record["returncode"] = completed.returncode
            result = json.loads(cell_json.read_text()) if cell_json.exists() else None
            if result is None or not matches(result, arguments):
                record.update(outcome="failed", reason="no result or a result of another cell",
                              stderr=completed.stderr[-2000:])
            else:
                record["result"] = result
                record["outcome"] = "completed" if completed.returncode == 0 else "failed"
                if is_exact and completed.returncode == 0:
                    if reference.exists():
                        record["reference_sha256"] = sha256(reference)
                    else:
                        record.update(outcome="failed", reason="exact run left no reference")
        except subprocess.TimeoutExpired:
            record.update(outcome="unevaluated", reason=f"timeout after {TIMEOUT_SECONDS} s")
            if is_exact and reference.exists():
                reference.unlink()
        records[cell] = record
        with results.open("a") as stream:
            stream.write(json.dumps(record) + "\n")
        print(cell, record["outcome"], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
