#!/usr/bin/env python3
"""METHOD-077: run plan and frozen analysis of the Nystroem landmark-variant experiment.

Run: executes IntrinsicCoherentPointDriftLandmarkVariants once per cell, one cell at a time,
pinned to logical CPUs 0-15 (one per physical core on the 16-core host), with a hard timeout per
cell (a timeout records the cell as unevaluated). Per (fixture, seed) pair: the exact run, the
timed arms in pass 1, the floor and negative controls, then the timed arms again in reverse order
(pass 2). Only a successful, complete exact run leaves a reference of exactly N double3
(<out>/references/<fixture>_<seed>.bin, sha256 recorded); the pair's other cells run only against
that recorded reference. Each cell writes its own JSON, checked against the cell. Records carry
the git commit, the runner's sha256 and the point count; a restart refuses records of another
commit, runner or size, and skips recorded cells. Exit 0 when every cell completed, else 1.

Analyze (--analyze): the decision rule and gates of Layer B, fixed before the run, applied to
<out>/results.jsonl; writes <out>/analysis.json and prints the tables.

  cpd_landmark_variants.py --runner <binary> --out <dir> [--plan] [--only <cell>]
  cpd_landmark_variants.py --out <dir> --analyze
"""
import argparse
import hashlib
import json
import math
import os
import pathlib
import shutil
import subprocess
import sys

FIXTURES = ["full", "partial"]
SEEDS = [1, 2, 3]
POINTS = 100_000
TIMEOUT_SECONDS = 600
BASELINE = "points/mixed/random"
VARIANTS = [
    "points/mixed/random", "points/fixed/random", "points/resampled/random",
    "box/mixed/random", "box/fixed/random", "box/resampled/random",
    "box/mixed/halton", "box/fixed/halton", "box/resampled/halton",
]
PERTURBATION = "perturbation_control"  # the baseline with error limit 1e-5
EQUIVALENT = 1e-3     # rms_to_exact, world units
FLOOR_LIMIT = 2e-4    # floor control rms_to_exact
TRUTH_LIMIT = 0.02    # exact rms_to_truth on the full fixture
FASTER = 0.9          # mean seconds relative to the baseline
SLOWER = 1.1


def arm_arguments(arm):
    if arm == PERTURBATION:
        return ["--variant", BASELINE, "--error-limit", "1e-5"]
    if arm == "floor_control":
        return ["--variant", BASELINE, "--error-limit", "1e-300"]
    if arm == "negative_control":
        return ["--variant", BASELINE, "--landmarks", "2", "--error-limit", "1e300"]
    if arm == "exact":
        return ["--variant", "exact"]
    return ["--variant", arm]


def plan(points=POINTS):
    """Cells in execution order: (cell id, runner arguments without paths)."""
    timed = VARIANTS + [PERTURBATION]
    cells = []
    for fixture in FIXTURES:
        for seed in SEEDS:
            base = ["--fixture", fixture, "--seed", str(seed), "--points", str(points), "--threads", "16"]
            order = [("exact", None)] + [(arm, 1) for arm in timed] + [("floor_control", None), ("negative_control", None)]
            order += [(arm, 2) for arm in reversed(timed)]
            for arm, run in order:
                cell = f"{fixture}/{seed}/{arm}" + (f"@{run}" if run else "")
                cells.append((cell, base + arm_arguments(arm)))
    return cells


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def argument(arguments, key, fallback):
    return arguments[arguments.index(key) + 1] if key in arguments else fallback


def matches(result, arguments):
    """The cell's JSON describes the cell that was run."""
    return (result.get("fixture") == argument(arguments, "--fixture", None)
            and str(result.get("seed")) == argument(arguments, "--seed", None)
            and str(result.get("points")) == argument(arguments, "--points", None)
            and str(result.get("threads")) == argument(arguments, "--threads", None)
            and result.get("variant") == argument(arguments, "--variant", None)
            and str(result.get("landmarks")) == argument(arguments, "--landmarks", "256")
            and float(result.get("error_limit")) == float(argument(arguments, "--error-limit", "0.001")))


def load(results):
    records = {}
    if results.exists():
        for line in results.read_text().splitlines():
            record = json.loads(line)
            records[record["cell"]] = record
    return records


def run(args):
    cells = plan(args.points)
    if args.only and args.only not in {cell for cell, _ in cells}:
        print(f"unknown cell {args.only}", file=sys.stderr)
        return 2
    out = pathlib.Path(args.out)
    (out / "cells").mkdir(parents=True, exist_ok=True)
    (out / "references").mkdir(parents=True, exist_ok=True)
    lock = out / "driver.lock"
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        print(f"{lock} exists: another driver runs, or remove it after checking", file=sys.stderr)
        return 2
    os.close(descriptor)
    try:
        return execute(args, cells, out)
    finally:
        lock.unlink()


def execute(args, cells, out):
    results = out / "results.jsonl"
    records = load(results)
    git = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
    commit = git.stdout.strip()
    if git.returncode != 0 or not commit:
        print("cannot read the git commit", file=sys.stderr)
        return 2
    runner_sha = sha256(args.runner)
    foreign = [c for c, r in records.items()
               if (r.get("commit"), r.get("runner_sha256"), r.get("points")) != (commit, runner_sha, args.points)]
    if foreign:
        print(f"{len(foreign)} records of another commit, runner or size (e.g. {foreign[0]}); refusing to resume",
              file=sys.stderr)
        return 2
    pin = ["taskset", "-c", "0-15"] if shutil.which("taskset") else []
    for cell, arguments in cells:
        if (args.only and cell != args.only) or cell in records:
            continue
        fixture, seed = cell.split("/")[:2]
        is_exact = cell.endswith("/exact")
        reference = out / "references" / f"{fixture}_{seed}.bin"
        record = {"cell": cell, "commit": commit, "runner_sha256": runner_sha, "points": args.points, "pinned": bool(pin)}
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
        cell_json = out / "cells" / (cell.replace("/", "_").replace("@", "_") + ".json")
        if cell_json.exists():
            cell_json.unlink()
        command = pin + [args.runner, *arguments, "--out", str(cell_json)]
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
                    if reference.exists() and reference.stat().st_size == args.points * 24:
                        record["reference_sha256"] = sha256(reference)
                    else:
                        record.update(outcome="failed", reason="exact run left no complete reference")
        except subprocess.TimeoutExpired:
            record.update(outcome="unevaluated", reason=f"timeout after {TIMEOUT_SECONDS} s")
            if is_exact and reference.exists():
                reference.unlink()
        records[cell] = record
        with results.open("a") as stream:
            stream.write(json.dumps(record) + "\n")
        print(cell, record["outcome"], flush=True)
    planned = [records.get(cell) for cell, _ in cells if not args.only or cell == args.only]
    return 0 if all(r is not None and r.get("outcome") == "completed" for r in planned) else 1


# ---- frozen analysis (Layer B) -------------------------------------------------------------

def result_of(records, cell):
    record = records.get(cell)
    return record["result"] if record and record.get("outcome") == "completed" else None


def same_counts(a, b):
    keys = ("iterations", "nystrom_iterations", "kernel_evaluations", "rms_to_exact", "max_to_exact", "sigma2")
    return all(a.get(k) == b.get(k) for k in keys)


def analyze(args):
    out = pathlib.Path(args.out)
    records = load(out / "results.jsonl")
    pairs, report = [], {"pairs": {}, "variants": {}, "predictions": {}}
    for fixture in FIXTURES:
        for seed in SEEDS:
            pair = f"{fixture}/{seed}"
            get = lambda arm: result_of(records, f"{pair}/{arm}")
            gates = {}
            exact = get("exact")
            gates["exact"] = exact is not None and (fixture != "full" or exact["rms_to_truth"] <= TRUTH_LIMIT)
            arms = {}
            deterministic = True
            for arm in VARIANTS + [PERTURBATION]:
                first, second = get(f"{arm}@1"), get(f"{arm}@2")
                if first and second:
                    deterministic &= same_counts(first, second)
                    arms[arm] = {"first": first, "second": second,
                                 "seconds": (first["seconds"] + second["seconds"]) / 2.0,
                                 "spread": abs(first["seconds"] - second["seconds"]) / ((first["seconds"] + second["seconds"]) / 2.0)}
                else:
                    arms[arm] = None
            gates["determinism"] = deterministic
            base = arms[BASELINE]
            gates["baseline"] = (base is not None and base["first"]["nystrom_iterations"] > 0
                                 and base["first"]["rms_to_exact"] <= EQUIVALENT)
            floor = get("floor_control")
            gates["floor"] = (floor is not None and floor["nystrom_iterations"] == 0 and floor["rms_to_exact"] <= FLOOR_LIMIT)
            negative = get("negative_control")
            gates["negative"] = (negative is not None and negative["nystrom_iterations"] > 0
                                 and negative["rms_to_exact"] > EQUIVALENT)
            perturbation = arms[PERTURBATION]
            gates["perturbation"] = perturbation is not None and perturbation["first"]["rms_to_exact"] <= EQUIVALENT
            decided = all(gates.values())
            noise = max((a["spread"] for a in arms.values() if a), default=math.inf)
            report["pairs"][pair] = {"gates": gates, "decided": decided, "timing_noise": noise}
            pairs.append((pair, decided, arms, noise, negative))
    decided_pairs = [p for p in pairs if p[1]]
    ranking = len(decided_pairs) >= 4  # more than 2 of 6 undecided: no ranking
    report["ranking"] = ranking
    for variant in VARIANTS[1:]:
        rows, equivalent_faster, bad = [], 0, 0
        for pair, decided, arms, noise, _ in decided_pairs:
            arm, base = arms[variant], arms[BASELINE]
            equivalent = arm is not None and arm["first"]["rms_to_exact"] <= EQUIVALENT
            ratio = arm["seconds"] / base["seconds"] if arm else math.inf
            faster = arm is not None and ratio <= FASTER and (1.0 - ratio) > 2.0 * noise
            equivalent_faster += equivalent and faster
            bad += (not equivalent) or ratio > SLOWER
            rows.append({"pair": pair, "equivalent": equivalent, "seconds_ratio": ratio,
                         "nystrom_iterations": arm["first"]["nystrom_iterations"] if arm else None,
                         "rms_to_exact": arm["first"]["rms_to_exact"] if arm else None})
        k = len(decided_pairs)
        if not ranking:
            decision = "no ranking"
        elif equivalent_faster == k:
            decision = "beats baseline"
        elif bad >= math.ceil(2 * k / 3):
            decision = "worse than baseline"
        else:
            decision = "no decision"
        report["variants"][variant] = {"decision": decision, "pairs": rows}

    def all_decided(predicate):
        return bool(decided_pairs) and all(predicate(p) for p in decided_pairs)

    box = [v for v in VARIANTS if v.startswith("box/")]
    report["predictions"] = {
        "P1": all_decided(lambda p: all(p[2][v] and p[2][v]["first"]["nystrom_iterations"]
                                        < p[2][BASELINE]["first"]["nystrom_iterations"] for v in box))
              and all(report["variants"][v]["decision"] != "beats baseline" for v in box),
        "P2": all_decided(lambda p: p[2]["points/fixed/random"] is not None
                          and abs(p[2]["points/fixed/random"]["seconds"] / p[2][BASELINE]["seconds"] - 1.0) <= 0.1
                          and p[2]["points/fixed/random"]["first"]["rms_to_exact"] <= EQUIVALENT),
        "P3": report["variants"]["points/resampled/random"]["decision"] != "beats baseline",
        "P4": None,  # filled below
        "P5": all_decided(lambda p: p[4]["rms_to_exact"] >= 10.0 * max(
            p[2][v]["first"]["rms_to_exact"] for v in VARIANTS if p[2][v])),
        "P6": all(p[2][BASELINE] is not None and p[2][BASELINE]["first"]["nystrom_iterations"] > 0
                  and p[2][BASELINE]["first"]["rms_to_exact"] <= EQUIVALENT for p in pairs),
        "P7": all(report["pairs"][p[0]]["gates"]["floor"] for p in pairs),
    }
    signs = set()
    for _, _, arms, _, _ in decided_pairs:
        for schedule in ("mixed", "fixed", "resampled"):
            h, r = arms[f"box/{schedule}/halton"], arms[f"box/{schedule}/random"]
            if h and r and h["seconds"] != r["seconds"]:
                signs.add(h["seconds"] < r["seconds"])
    report["predictions"]["P4"] = len(signs) != 1  # no consistent sign
    (out / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"ranking": ranking, "pairs": {p: v["decided"] for p, v in report["pairs"].items()},
                      "decisions": {v: d["decision"] for v, d in report["variants"].items()},
                      "predictions": report["predictions"]}, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runner")
    parser.add_argument("--out", required=True)
    parser.add_argument("--plan", action="store_true", help="print the cells and exit")
    parser.add_argument("--analyze", action="store_true", help="apply the frozen analysis to the recorded results")
    parser.add_argument("--only", help="run only this cell id (its exact reference must be recorded)")
    parser.add_argument("--points", type=int, default=POINTS, help="technical tests only; the run plan uses the default")
    args = parser.parse_args()
    if args.plan:
        cells = plan(args.points)
        for cell, arguments in cells:
            print(cell, " ".join(arguments))
        print(f"{len(cells)} cells, timeout {TIMEOUT_SECONDS} s each")
        return 0
    if args.analyze:
        return analyze(args)
    if not args.runner:
        parser.error("--runner is required to run")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
