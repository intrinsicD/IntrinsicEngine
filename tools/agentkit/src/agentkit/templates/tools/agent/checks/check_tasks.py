#!/usr/bin/env python3
"""Validate the tasks/ system: structure, task shape, and lifecycle integrity.

Driven by ``agentkit.toml`` ([tasks]). Checks:
  * required lifecycle directories exist
  * no legacy root planning files linger
  * each task file has a valid ``# <ID> — <title>`` heading and required sections
  * actionable sections use checkbox todos
  * tasks/done/ files carry completion metadata (date + PR/commit reference)
  * a task id does not appear in more than one lifecycle directory
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import _common as c

TOOL = "check_tasks"
SKIP_FILENAMES = {"README.md", "index.md", ".gitkeep"}
DATE_RE = re.compile(r"\b\d{4}-\d{2}-\d{2}\b")
REF_RE = re.compile(r"(#\d+|PR\s*\d+|[0-9a-f]{7,40}|commit)", re.IGNORECASE)


def _task_id(heading: str, id_regex: str) -> str | None:
    # Heading looks like: "# ABC-123 — Title" ; capture the first token.
    body = heading[1:].strip()
    token = re.split(r"[\s—-]", body, maxsplit=1)[0] if body else ""
    # Re-extract a full id (prefix-number) for the regex test.
    match = re.match(r"[A-Za-z][A-Za-z0-9]*-\d+[A-Za-z0-9]*", body)
    candidate = match.group(0) if match else token
    return candidate if re.match(id_regex, candidate) else None


def main() -> int:
    args = c.base_parser(__doc__ or TOOL).parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, args.strict)

    dirs = c.cfg_get(cfg, "tasks.dirs", ["backlog", "active", "done", "templates"])
    lifecycle = [d for d in dirs if d != "templates"]
    required_sections = c.cfg_get(
        cfg,
        "tasks.required_sections",
        ["Goal", "Context", "Required changes", "Tests", "Docs", "Acceptance criteria"],
    )
    actionable = c.cfg_get(cfg, "tasks.actionable_sections", ["Required changes", "Tests", "Docs"])
    id_regex = c.cfg_get(cfg, "tasks.id_regex", r"^[A-Z][A-Z0-9]*-\d+[A-Za-z0-9]*$")
    legacy_files = c.cfg_get(cfg, "tasks.legacy_root_files", [])

    tasks_root = root / "tasks"
    if not tasks_root.is_dir():
        rep.error("tasks/ directory is missing")
        return rep.finish("tasks system OK")

    for d in dirs:
        if not (tasks_root / d).is_dir():
            rep.error(f"missing required task directory: tasks/{d}/")

    for legacy in legacy_files:
        if (root / legacy).is_file():
            rep.error(f"legacy root planning file should move into tasks/: {legacy}")

    seen_ids: dict[str, str] = {}
    for life in lifecycle:
        life_dir = tasks_root / life
        if not life_dir.is_dir():
            continue
        for path in sorted(life_dir.rglob("*.md")):
            if path.name in SKIP_FILENAMES:
                continue
            rel = path.relative_to(root)
            text = path.read_text(encoding="utf-8")
            lines = text.splitlines()
            heading = next((ln for ln in lines if ln.startswith("# ")), "")
            if not heading:
                rep.error(f"{rel}: missing '# <ID> — <title>' heading")
                continue
            task_id = _task_id(heading, id_regex)
            if task_id is None:
                rep.error(f"{rel}: heading id does not match {id_regex!r}: {heading!r}")
            else:
                if task_id in seen_ids:
                    rep.error(f"duplicate task id {task_id}: {seen_ids[task_id]} and {rel}")
                else:
                    seen_ids[task_id] = str(rel)

            sections = c.split_sections(text)
            for section in required_sections:
                if section not in sections:
                    rep.error(f"{rel}: missing required section '## {section}'")
            for section in actionable:
                body = sections.get(section, "")
                if section in sections and "- [" not in body:
                    rep.warn(f"{rel}: actionable section '## {section}' has no checkbox todos")

            if life == "done":
                if not DATE_RE.search(text):
                    rep.error(f"{rel}: tasks/done/ entry needs a completion date (YYYY-MM-DD)")
                if not REF_RE.search(text):
                    rep.warn(f"{rel}: tasks/done/ entry should reference a PR or commit")
                if "- [ ]" in text:
                    rep.warn(f"{rel}: tasks/done/ entry still has unchecked todos")

    return rep.finish(f"tasks system OK ({len(seen_ids)} task(s) validated)")


if __name__ == "__main__":
    sys.exit(main())
