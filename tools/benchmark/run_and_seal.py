#!/usr/bin/env python3
"""Run a benchmark producer and seal every emitted JSON result as schema v2."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Producer output: a directory, sealed exactly (a dotted name must exist before "
        "the run, e.g. mktemp -d), or a .json file whose parent directory is sealed and must "
        "hold only this run's results.",
    )
    parser.add_argument("--manifests-root", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--attempt-id", default="attempt-001")
    args = parser.parse_args()

    produced = subprocess.run([args.executable, str(args.output)], check=False)
    # Classify what the producer wrote, never by suffix: a dotted directory is still a directory.
    if args.output.is_dir():
        result_root = args.output
    elif args.output.is_file() and args.output.suffix == ".json":
        result_root = args.output.parent  # explicit-file producers write sibling results beside it
    else:
        print(
            f"ERROR: {args.output} is neither a result directory nor a .json result file; "
            "refusing to guess a seal root (create a dotted output directory before the run)",
            file=sys.stderr,
        )
        return produced.returncode or 2
    sealer = Path(__file__).resolve().with_name("seal_benchmark_results.py")
    sealed = subprocess.run(
        [
            sys.executable,
            str(sealer),
            "--root",
            str(result_root),
            "--manifests-root",
            str(args.manifests_root),
            "--run-id",
            args.run_id,
            "--attempt-id",
            args.attempt_id,
            "--replace",
        ],
        check=False,
    )
    if sealed.returncode != 0:
        return sealed.returncode
    return produced.returncode


if __name__ == "__main__":
    raise SystemExit(main())
