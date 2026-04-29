#!/usr/bin/env python3
"""Validate Codex workflow configuration stays meaningful and policy-light."""

from __future__ import annotations

import argparse
import re
import shlex
from pathlib import Path
from typing import Any

ALLOWED_TOP_LEVEL_KEYS = {"project", "workflow", "ignore", "agent"}
POLICY_TOP_LEVEL_KEYS = {
    "architecture",
    "architecture_invariants",
    "layering",
    "layers",
    "testing",
    "ci",
    "policy",
    "rules",
    "instructions",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate .codex/config.yaml.")
    parser.add_argument("--root", default=".", help="Repository root path.")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail on any finding; warning mode otherwise.",
    )
    return parser.parse_args()


def parse_scalar(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def parse_simple_yaml(path: Path) -> dict[str, Any]:
    """Parse the small .codex/config.yaml shape without external dependencies."""
    data: dict[str, Any] = {}
    current_key: str | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue

        indent = len(raw_line) - len(raw_line.lstrip(" "))
        line = raw_line.strip()

        if indent == 0:
            if ":" not in line:
                raise ValueError(f"cannot parse top-level line: {raw_line}")
            key, value = line.split(":", 1)
            key = key.strip()
            value = value.strip()
            current_key = key
            if value:
                data[key] = parse_scalar(value)
            else:
                data[key] = {}
            continue

        if current_key is None:
            raise ValueError(f"nested line before a top-level key: {raw_line}")

        if line.startswith("- "):
            if not isinstance(data.get(current_key), list):
                data[current_key] = []
            data[current_key].append(parse_scalar(line[2:]))
            continue

        if ":" not in line:
            raise ValueError(f"cannot parse nested line: {raw_line}")

        if not isinstance(data.get(current_key), dict):
            raise ValueError(f"top-level key cannot mix list/scalar and mapping: {current_key}")
        key, value = line.split(":", 1)
        data[current_key][key.strip()] = parse_scalar(value)

    return data


def command_segments(command: str) -> list[list[str]]:
    segments: list[list[str]] = []
    for segment in re.split(r"\s*&&\s*", command):
        segment = segment.strip()
        if segment:
            segments.append(shlex.split(segment))
    return segments


def has_configure_ci(segments: list[list[str]]) -> bool:
    return any(len(seg) >= 3 and seg[0] == "cmake" and "--preset" in seg and "ci" in seg for seg in segments)


def build_targets(segments: list[list[str]]) -> list[str]:
    targets: list[str] = []
    for seg in segments:
        if len(seg) < 4 or seg[0] != "cmake" or "--build" not in seg:
            continue
        if "--preset" not in seg or "ci" not in seg:
            continue
        if "--target" not in seg:
            targets.append("")
            continue
        target_index = seg.index("--target") + 1
        if target_index >= len(seg):
            targets.append("")
        else:
            targets.append(seg[target_index])
    return targets


def has_ctest(segments: list[list[str]]) -> bool:
    return any(seg and seg[0] == "ctest" for seg in segments)


def main() -> int:
    args = parse_args()
    repo_root = Path(args.root).resolve()
    config_path = repo_root / ".codex" / "config.yaml"
    findings: list[str] = []

    if not config_path.is_file():
        findings.append("missing .codex/config.yaml")
    else:
        try:
            config = parse_simple_yaml(config_path)
        except ValueError as exc:
            config = {}
            findings.append(f"could not parse .codex/config.yaml: {exc}")

        top_keys = set(config.keys())
        for key in sorted(top_keys - ALLOWED_TOP_LEVEL_KEYS):
            findings.append(f"unexpected top-level key in .codex/config.yaml: {key}")
        for key in sorted(top_keys & POLICY_TOP_LEVEL_KEYS):
            findings.append(
                f"duplicate architecture/workflow policy belongs in AGENTS.md, not .codex/config.yaml: {key}"
            )

        project = config.get("project", {})
        if not isinstance(project, dict):
            findings.append("project section must be a mapping")
        elif project.get("standard") != "c++23":
            findings.append("project.standard must be c++23")

        workflow = config.get("workflow", {})
        if not isinstance(workflow, dict):
            findings.append("workflow section must be a mapping")
            verification_command = ""
        else:
            if workflow.get("instruction_file") != "AGENTS.md":
                findings.append("workflow.instruction_file must be AGENTS.md")
            verification_command = str(workflow.get("verification_command", ""))

        if not verification_command:
            findings.append("workflow.verification_command is required")
        else:
            segments = command_segments(verification_command)
            if not has_configure_ci(segments):
                findings.append("verification_command must contain `cmake --preset ci`")

            targets = build_targets(segments)
            if not targets:
                findings.append("verification_command must contain `cmake --build --preset ci --target <target>`")
            elif any(target in {"", "help"} for target in targets):
                findings.append("verification_command must build a real target other than `help`")

            if not has_ctest(segments):
                findings.append("verification_command must run ctest")

    if findings:
        mode = "ERROR" if args.strict else "WARNING"
        for finding in findings:
            print(f"{mode}: {finding}")
        if args.strict:
            return 1

    print(
        f"Codex config check complete. findings={len(findings)} mode={'strict' if args.strict else 'warning'}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

