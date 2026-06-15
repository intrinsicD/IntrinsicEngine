#!/usr/bin/env python3
"""Run a command, stream output, and report elapsed wall-clock time."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def _strip_command_separator(command: list[str]) -> list[str]:
    if command and command[0] == "--":
        return command[1:]
    return command


def _parse_cache_hit(value: str | None) -> bool:
    return str(value or "").strip().lower() == "true"


def _emit_github_message(level: str, title: str, message: str) -> None:
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::{level} title={title}::{message}")


def _write_github_outputs(elapsed_seconds: float) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        return
    with Path(output_path).open("a", encoding="utf-8") as output:
        output.write(f"elapsed_seconds={elapsed_seconds:.3f}\n")
        output.write(f"elapsed_ms={int(round(elapsed_seconds * 1000.0))}\n")


def _append_github_summary(label: str, elapsed_seconds: float, cache_hit: str | None) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    cache_value = "n/a" if cache_hit is None else str(cache_hit or "false").lower()
    with Path(summary_path).open("a", encoding="utf-8") as summary:
        summary.write(f"### {label}\n\n")
        summary.write(f"- elapsed: `{elapsed_seconds:.3f} s`\n")
        summary.write(f"- vcpkg cache hit: `{cache_value}`\n\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="command", help="Human-readable label for reports")
    parser.add_argument(
        "--warm-cache-hit",
        help="Cache-hit value from actions/cache. Exact string 'true' enables the warm threshold.",
    )
    parser.add_argument(
        "--max-warm-seconds",
        type=float,
        help="Fail if --warm-cache-hit is true and elapsed time exceeds this value.",
    )
    parser.add_argument("--json-out", type=Path, help="Optional JSON report path")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command to execute after --")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    command = _strip_command_separator(args.command)
    if not command:
        print("ERROR: command is required after --", file=sys.stderr)
        return 2

    start = time.monotonic()
    completed = subprocess.run(command, check=False)
    elapsed_seconds = time.monotonic() - start

    print(f"{args.label} elapsed: {elapsed_seconds:.3f} s")
    _write_github_outputs(elapsed_seconds)
    _append_github_summary(args.label, elapsed_seconds, args.warm_cache_hit)

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(
                {
                    "label": args.label,
                    "command": command,
                    "elapsed_seconds": round(elapsed_seconds, 3),
                    "warm_cache_hit": _parse_cache_hit(args.warm_cache_hit),
                    "returncode": completed.returncode,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    if completed.returncode != 0:
        return int(completed.returncode)

    if (
        args.max_warm_seconds is not None
        and _parse_cache_hit(args.warm_cache_hit)
        and elapsed_seconds > args.max_warm_seconds
    ):
        message = (
            f"{args.label} took {elapsed_seconds:.3f} s with an exact vcpkg cache hit; "
            f"limit is {args.max_warm_seconds:.3f} s."
        )
        _emit_github_message("error", "Warm configure budget exceeded", message)
        print(f"ERROR: {message}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
