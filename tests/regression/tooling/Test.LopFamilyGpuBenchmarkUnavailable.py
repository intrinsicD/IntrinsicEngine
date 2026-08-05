#!/usr/bin/env python3
"""Prove the LOP Vulkan benchmark fails closed without window-system access."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)
    environment.pop("GLFW_PLATFORM", None)
    # The promoted Vulkan preset enables LeakSanitizer, but BUG-118 owns the
    # process-static GLFW/X11 leak contract. This regression needs the child
    # process to reach its nonzero exit so it can inspect the retained payload.
    asan_options = [
        option
        for option in environment.get("ASAN_OPTIONS", "").split(":")
        if option and not option.startswith("detect_leaks=")
    ]
    asan_options.append("detect_leaks=0")
    environment["ASAN_OPTIONS"] = ":".join(asan_options)
    completed = subprocess.run(
        [str(args.executable), str(args.output)],
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    errors: list[str] = []
    if completed.returncode == 0:
        errors.append("unavailable benchmark returned zero")
    if not args.output.is_file():
        errors.append("unavailable benchmark did not retain a raw result")
        payload: dict = {}
    else:
        try:
            payload = json.loads(args.output.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError) as exc:
            errors.append(f"raw result is not valid JSON: {exc}")
            payload = {}

    diagnostics = payload.get("diagnostics", {})
    expected = {
        "status": "skipped",
        "requested_backend": "gpu_vulkan_compute",
        "actual_backend": "none",
        "execution_observed": False,
        "fallback_observed": None,
        "gpu_requests_accepted": 0,
        "gpu_completions": 0,
        "gpu_fallbacks": 0,
    }
    observed = {
        "status": payload.get("status"),
        "requested_backend": diagnostics.get("requested_backend"),
        "actual_backend": diagnostics.get("actual_backend"),
        "execution_observed": diagnostics.get("execution_observed"),
        "fallback_observed": diagnostics.get("fallback_observed"),
        "gpu_requests_accepted": diagnostics.get("gpu_requests_accepted"),
        "gpu_completions": diagnostics.get("gpu_completions"),
        "gpu_fallbacks": diagnostics.get("gpu_fallbacks"),
    }
    for key, value in expected.items():
        if observed[key] != value:
            errors.append(f"{key}: expected {value!r}, got {observed[key]!r}")

    if errors:
        print("LOP Vulkan unavailable-environment regression FAILED:")
        for error in errors:
            print(f" - {error}")
        if completed.stdout:
            print(completed.stdout)
        return 1
    print(
        "LOP Vulkan unavailable-environment regression passed "
        f"(runner exit {completed.returncode})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
