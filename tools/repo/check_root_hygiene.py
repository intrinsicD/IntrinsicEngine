#!/usr/bin/env python3
"""Check repository root hygiene (markdown policy + top-level allowlist)."""

from __future__ import annotations

import argparse
import fnmatch
from dataclasses import dataclass
from pathlib import Path

ALLOWED_ROOT_MARKDOWN = {"README.md", "AGENTS.md", "CLAUDE.md"}
DEFAULT_ALLOWLIST_FILE = Path("tools/repo/root_allowlist.yaml")


class RootPolicyError(ValueError):
    """The root-policy file is missing or malformed."""


@dataclass(frozen=True)
class RootPolicy:
    allowed_root_entries: frozenset[str]
    ignored_local_entries: tuple[str, ...]


@dataclass(frozen=True)
class RootScan:
    allowed: tuple[str, ...]
    ignored_local: tuple[str, ...]
    unexpected: tuple[str, ...]
    missing: tuple[str, ...]


def _unquote(value: str) -> str:
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def load_root_policy(path: Path) -> RootPolicy:
    if not path.exists():
        raise RootPolicyError(f"root policy does not exist: {path}")

    sections: dict[str, list[str]] = {
        "allowed_root_entries": [],
        "ignored_local_entries": [],
    }
    known_sections = {*sections, "notes"}
    seen_sections: set[str] = set()
    current: str | None = None
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if not line.startswith("-") and line.endswith(":"):
            current = line[:-1]
            if current not in known_sections:
                raise RootPolicyError(
                    f"unknown policy section `{current}` at {path}:{line_number}"
                )
            if current in seen_sections:
                raise RootPolicyError(
                    f"duplicate policy section `{current}` at {path}:{line_number}"
                )
            seen_sections.add(current)
            continue
        if line.startswith("-") and current is not None:
            value = _unquote(line[1:].strip())
            if not value:
                raise RootPolicyError(
                    f"empty `{current}` entry at {path}:{line_number}"
                )
            if current in sections:
                sections[current].append(value)
            continue
        raise RootPolicyError(f"invalid policy line at {path}:{line_number}")

    allowed = frozenset(sections["allowed_root_entries"])
    ignored = tuple(sections["ignored_local_entries"])
    if not allowed:
        raise RootPolicyError(
            f"root policy has no `allowed_root_entries`: {path}"
        )
    for section, entries in sections.items():
        if len(entries) != len(set(entries)):
            raise RootPolicyError(f"root policy has duplicate `{section}` entries")
    overlap = allowed.intersection(ignored)
    if overlap:
        raise RootPolicyError(
            "root policy entries cannot be both allowed and ignored: "
            + ", ".join(sorted(overlap))
        )
    if any(any(token in entry for token in "*?[") for entry in allowed):
        raise RootPolicyError("allowed root entries must be exact, not patterns")
    for entry in ignored:
        wildcard_count = entry.count("*")
        bounded_suffix_pattern = (
            wildcard_count == 1
            and entry.endswith("*/")
            and bool(entry[:-2])
            and "?" not in entry
            and "[" not in entry
        )
        if wildcard_count or "?" in entry or "[" in entry:
            if not bounded_suffix_pattern:
                raise RootPolicyError(
                    "ignored local patterns must be exact or use one bounded "
                    f"suffix wildcard: {entry}"
                )
    return RootPolicy(allowed, ignored)


def scan_root_entries(root: Path, policy: RootPolicy) -> RootScan:
    allowed: list[str] = []
    ignored_local: list[str] = []
    unexpected: list[str] = []
    for path in sorted(root.iterdir(), key=lambda candidate: candidate.name):
        if path.name == ".git":
            continue
        entry = f"{path.name}/" if path.is_dir() else path.name
        if entry in policy.allowed_root_entries:
            allowed.append(entry)
        elif any(
            fnmatch.fnmatchcase(entry, pattern)
            for pattern in policy.ignored_local_entries
        ):
            ignored_local.append(entry)
        else:
            unexpected.append(entry)
    return RootScan(
        allowed=tuple(allowed),
        ignored_local=tuple(ignored_local),
        unexpected=tuple(unexpected),
        missing=tuple(sorted(policy.allowed_root_entries - set(allowed))),
    )


def _print_entries(title: str, entries: tuple[str, ...]) -> None:
    print(f"[check_root_hygiene] {title}:")
    if not entries:
        print("  - none")
        return
    for entry in entries:
        print(f"  - {entry}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path("."), help="Repository root"
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail on disallowed markdown files or root-policy mismatch",
    )
    parser.add_argument(
        "--allowlist",
        type=Path,
        default=DEFAULT_ALLOWLIST_FILE,
        help="Path to the shared root policy",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    root_md = sorted(
        path.name
        for path in root.iterdir()
        if path.is_file() and path.suffix.lower() == ".md"
    )
    disallowed = [name for name in root_md if name not in ALLOWED_ROOT_MARKDOWN]

    print(f"[check_root_hygiene] Root: {root}")
    if root_md:
        print("[check_root_hygiene] Root markdown files:")
        for name in root_md:
            status = "allowed" if name in ALLOWED_ROOT_MARKDOWN else "disallowed"
            print(f"  - {name}: {status}")
    else:
        print("[check_root_hygiene] No root-level markdown files found.")

    allowlist_path = (
        (root / args.allowlist).resolve()
        if not args.allowlist.is_absolute()
        else args.allowlist
    )
    try:
        policy = load_root_policy(allowlist_path)
    except RootPolicyError as exc:
        print(f"[check_root_hygiene] ERROR: {exc}")
        return 2
    scan = scan_root_entries(root, policy)

    print(f"[check_root_hygiene] Root allowlist: {allowlist_path}")
    _print_entries("Allowed repository entries present", scan.allowed)
    _print_entries("Ignored named local entries", scan.ignored_local)
    if scan.unexpected:
        _print_entries("Unexpected root entries", scan.unexpected)
    if scan.missing:
        _print_entries("Missing expected root entries", scan.missing)

    if disallowed:
        print(
            "[check_root_hygiene] Action: move/archive/delete disallowed "
            "root markdown files."
        )

    mismatch = bool(disallowed or scan.unexpected or scan.missing)
    if args.strict and mismatch:
        print(
            "[check_root_hygiene] STRICT MODE: failing due to "
            "root-policy mismatch."
        )
        return 1
    if mismatch:
        print(
            "[check_root_hygiene] WARNING MODE: root-policy mismatch is "
            "non-fatal."
        )
    else:
        print("[check_root_hygiene] Root entries match repository-owned policy.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
