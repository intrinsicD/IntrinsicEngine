"""Report what the agentic workflow expects vs. what is present on disk."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from . import config as cfgmod


def _expected_paths(cfg: dict[str, Any]) -> list[str]:
    slug = cfgmod.get(cfg, "project.slug", "project")
    harness = cfg.get("harness", {})
    paths: list[str] = [
        cfgmod.CONFIG_FILENAME,
        cfgmod.get(cfg, "project.contract_file", "AGENTS.md"),
        f"{cfgmod.TOOLS_DIR}/README.md",
        f"{cfgmod.TOOLS_DIR}/resync_skills.sh",
        cfgmod.get(cfg, "docs_sync.rules_file", f"{cfgmod.TOOLS_DIR}/docs_sync_rules.toml"),
        ".github/workflows/pr-fast.yml",
        ".github/workflows/ci-docs.yml",
        ".github/pull_request_template.md",
        "tasks/README.md",
    ]
    paths += [f"docs/agent/{doc}" for doc in cfgmod.AGENT_DOCS]
    paths += [f"tasks/templates/{tmpl}" for tmpl in cfgmod.TASK_TEMPLATES]
    paths += [f"tasks/{d}" for d in cfgmod.get(cfg, "tasks.dirs", [])]
    paths += [f"{cfgmod.CHECKS_DIR}/{check}" for check in cfgmod.CHECK_FILES]
    paths += [f"{cfgmod.SKILLS_DIR}/{slug}-{d}/SKILL.md" for d in cfgmod.SKILL_DIRS]
    if harness.get("claude", True):
        paths.append("CLAUDE.md")
        if harness.get("setup_hook", True):
            paths += [".claude/settings.json", ".claude/setup.sh"]
    if harness.get("codex", True):
        paths.append(".codex/config.yaml")
    if harness.get("copilot", True):
        paths.append(".github/copilot-instructions.md")
    return paths


def diagnose(target: Path, cfg: dict[str, Any]) -> tuple[list[str], list[str], list[str]]:
    """Return (present, missing, drifted-references)."""
    target = Path(target)
    present: list[str] = []
    missing: list[str] = []
    for rel in _expected_paths(cfg):
        (present if (target / rel).exists() else missing).append(rel)

    drift: list[str] = []
    slug = cfgmod.get(cfg, "project.slug", "project")
    for src_rel, skill_dir, ref_name in cfgmod.RESYNC_MAP:
        src = target / src_rel
        ref = target / f"{cfgmod.SKILLS_DIR}/{slug}-{skill_dir}/references/{ref_name}"
        if src.is_file() and ref.is_file():
            if src.read_text(encoding="utf-8") != ref.read_text(encoding="utf-8"):
                drift.append(str(ref.relative_to(target)))
    return present, missing, drift
