#!/usr/bin/env python3
"""Validate that a shader compilation output tree contains SPIR-V files."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dir",
        required=True,
        type=Path,
        help="Shader output directory to inspect, typically build/<preset>/bin/shaders.",
    )
    parser.add_argument(
        "--require",
        action="append",
        default=[],
        help="Relative .spv path that must exist. May be repeated.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    shader_dir = args.dir
    if not shader_dir.is_dir():
        raise SystemExit(f"shader output directory does not exist: {shader_dir}")

    spv_files = sorted(path for path in shader_dir.rglob("*.spv") if path.is_file())
    if not spv_files:
        raise SystemExit(f"no .spv files found under shader output directory: {shader_dir}")

    missing = [relative for relative in args.require if not (shader_dir / relative).is_file()]
    if missing:
        formatted = "\n".join(f"  - {relative}" for relative in missing)
        raise SystemExit(f"required shader outputs are missing under {shader_dir}:\n{formatted}")

    print(f"Found {len(spv_files)} SPIR-V shader outputs under {shader_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

