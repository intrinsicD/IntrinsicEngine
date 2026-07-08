#!/usr/bin/env python3
"""Generate-and-verify sync between canonical agent docs and the skill mirror.

Canonical sources live under ``docs/agent/`` (plus ``tasks/templates/task.md``).
They are mirrored into the physical skill root ``tools/agents/skills/``;
``.claude/skills`` and ``.codex/skills`` are symlinks to that root, so a single
sync covers every consumer. Relative Markdown links are rewritten during the
copy so every link that resolves from the canonical source also resolves from
the mirror location.

Modes:
- ``--write``: regenerate mirror copies in place.
- ``--check`` (default): regenerate in memory and fail listing every mirror
  file that diverges, without touching the tree. Also verifies the
  ``.claude``/``.codex`` symlinks resolve to the physical root.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# Canonical doc source -> mirror destination relative to the skills root.
REFERENCE_MAP = {
    "docs/agent/contract.md": "intrinsicengine-core/references/contract.md",
    "docs/agent/roles.md": "intrinsicengine-core/references/roles.md",
    "docs/agent/prompt/prompt.md": "intrinsicengine-core/references/session-onboarding.md",
    "docs/agent/task-format.md": "intrinsicengine-task-workflow/references/task-format.md",
    "docs/agent/task-maturity.md": "intrinsicengine-task-workflow/references/task-maturity.md",
    "tasks/templates/task.md": "intrinsicengine-task-workflow/references/task-template.md",
    "docs/agent/review-checklist.md": "intrinsicengine-review/references/review-checklist.md",
    "docs/agent/architecture-review-checklist.md": "intrinsicengine-review/references/architecture-review-checklist.md",
    "docs/agent/agent-output-review-checklist.md": "intrinsicengine-review/references/agent-output-review-checklist.md",
    "docs/agent/clean-workshop-review.md": "intrinsicengine-review/references/clean-workshop-review.md",
    "docs/agent/drift-audit-checklist.md": "intrinsicengine-review/references/drift-audit-checklist.md",
    "docs/agent/method-workflow.md": "intrinsicengine-method/references/method-workflow.md",
    "docs/agent/method-review-checklist.md": "intrinsicengine-method/references/method-review-checklist.md",
    "docs/agent/benchmark-workflow.md": "intrinsicengine-benchmark/references/benchmark-workflow.md",
    "docs/agent/benchmark-review-checklist.md": "intrinsicengine-benchmark/references/benchmark-review-checklist.md",
    "docs/agent/docs-sync-policy.md": "intrinsicengine-docs-sync/references/docs-sync-policy.md",
}

SKILLS_ROOT = "tools/agents/skills"

SYMLINK_ROOTS = (".claude/skills", ".codex/skills")

INLINE_LINK_RE = re.compile(r"(\]\()([^)\s]+)(\))")

EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "#", "/")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sync or verify the agent skill mirror against canonical docs."
    )
    parser.add_argument("--root", default=None, help="Repository root path.")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--write", action="store_true", help="Regenerate mirror copies in place."
    )
    mode.add_argument(
        "--check",
        action="store_true",
        help="Verify the mirror matches generated content (default).",
    )
    return parser.parse_args()


def rewrite_links(content: str, src_rel: Path, dst_rel: Path, repo_root: Path) -> str:
    """Rewrite relative Markdown links so they resolve from the mirror location."""
    src_dir = src_rel.parent
    dst_dir = dst_rel.parent

    def replace(match: re.Match[str]) -> str:
        target = match.group(2)
        if target.startswith(EXTERNAL_PREFIXES):
            return match.group(0)
        path_part, sep, anchor = target.partition("#")
        if not path_part:
            return match.group(0)
        resolved = Path(os.path.normpath(src_dir / path_part))
        if not (repo_root / resolved).exists():
            return match.group(0)
        new_target = Path(os.path.relpath(resolved, dst_dir)).as_posix()
        return f"{match.group(1)}{new_target}{sep}{anchor}{match.group(3)}"

    return INLINE_LINK_RE.sub(replace, content)


def generate(repo_root: Path, src_rel: str, skill_rel: str) -> str:
    content = (repo_root / src_rel).read_text(encoding="utf-8")
    dst_rel = Path(SKILLS_ROOT) / skill_rel
    return rewrite_links(content, Path(src_rel), dst_rel, repo_root)


def main() -> int:
    args = parse_args()
    if args.root:
        repo_root = Path(args.root).resolve()
    else:
        repo_root = Path(__file__).resolve().parents[2]

    write = bool(args.write)
    findings: list[str] = []

    missing_sources = [s for s in REFERENCE_MAP if not (repo_root / s).is_file()]
    if missing_sources:
        for src in missing_sources:
            print(f"ERROR: missing canonical source: {src}", file=sys.stderr)
        return 1

    skills_root = repo_root / SKILLS_ROOT
    if not skills_root.is_dir():
        print(f"ERROR: missing skills root: {SKILLS_ROOT}", file=sys.stderr)
        return 1

    for src_rel, skill_rel in sorted(REFERENCE_MAP.items()):
        expected = generate(repo_root, src_rel, skill_rel)
        dst_path = skills_root / skill_rel
        if write:
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            if not dst_path.exists() or dst_path.read_text(encoding="utf-8") != expected:
                dst_path.write_text(expected, encoding="utf-8")
                print(f"  wrote {SKILLS_ROOT}/{skill_rel}")
        else:
            if not dst_path.is_file():
                findings.append(f"missing mirror file: {SKILLS_ROOT}/{skill_rel}")
            elif dst_path.read_text(encoding="utf-8") != expected:
                findings.append(
                    f"stale mirror file: {SKILLS_ROOT}/{skill_rel} (source: {src_rel})"
                )

    for link_rel in SYMLINK_ROOTS:
        link_path = repo_root / link_rel
        if not link_path.exists():
            findings.append(f"missing skills surface: {link_rel}")
        elif not link_path.resolve().samefile(skills_root):
            findings.append(
                f"skills surface does not resolve to {SKILLS_ROOT}: {link_rel}"
            )

    if findings:
        for finding in findings:
            print(f"ERROR: {finding}", file=sys.stderr)
        print(
            "Skill mirror diverges from canonical docs. "
            "Run: python3 tools/agents/sync_skills.py --write",
            file=sys.stderr,
        )
        return 1

    mode = "write" if write else "check"
    print(
        f"Skill mirror sync complete. mode={mode} files={len(REFERENCE_MAP)} "
        f"surfaces={1 + len(SYMLINK_ROOTS)}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
