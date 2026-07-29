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
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifests-root", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--attempt-id", default="attempt-001")
    args = parser.parse_args()

    produced = subprocess.run([args.executable, str(args.output)], check=False)
    result_root = args.output if args.output.suffix == "" else args.output.parent
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
