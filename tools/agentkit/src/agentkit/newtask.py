"""Scaffold a new task file in tasks/backlog/ from a template."""
from __future__ import annotations

import re
from pathlib import Path

from . import config as cfgmod

_TEMPLATE_BY_KIND = {"task": "task.md", "bug": "bug-task.md", "review": "review-task.md"}


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.strip().lower()).strip("-") or "task"


def create_task(target: Path, task_id: str, title: str, kind: str) -> tuple[Path, list[str]]:
    target = Path(target)
    warnings: list[str] = []
    template_name = _TEMPLATE_BY_KIND.get(kind)
    if template_name is None:
        raise ValueError(f"unknown task kind '{kind}' (choose from {sorted(_TEMPLATE_BY_KIND)})")

    try:
        cfg = cfgmod.load(target)
        id_regex = cfgmod.get(cfg, "tasks.id_regex", r"^[A-Z][A-Z0-9]*-\d+[A-Za-z0-9]*$")
    except FileNotFoundError:
        id_regex = r"^[A-Z][A-Z0-9]*-\d+[A-Za-z0-9]*$"
    if not re.match(id_regex, task_id):
        warnings.append(f"task id '{task_id}' does not match {id_regex!r}")

    template_path = target / "tasks" / "templates" / template_name
    if not template_path.is_file():
        raise FileNotFoundError(f"missing task template: {template_path}")
    body = template_path.read_text(encoding="utf-8")
    # Replace the first markdown H1 (the placeholder title line).
    body = re.sub(r"^# .*$", f"# {task_id} — {title}", body, count=1, flags=re.MULTILINE)

    dest = target / "tasks" / "backlog" / f"{task_id}-{_slug(title)}.md"
    if dest.exists():
        raise FileExistsError(f"task already exists: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(body, encoding="utf-8")
    return dest, warnings
