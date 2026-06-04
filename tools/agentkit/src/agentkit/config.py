"""Configuration model for agentkit.

``agentkit.toml`` at the target repository root is the single source of truth
that drives both the generator (this tool) and the shipped, config-driven
validators (``tools/agent/checks/*.py``). This module builds the default
config, loads an existing one, and flattens it into the ``{{ UPPER }}`` render
context consumed by :mod:`agentkit.render`.

Standard library only.
"""
from __future__ import annotations

import datetime as _dt
import re
import tomllib
from pathlib import Path
from typing import Any

from . import __version__

CONFIG_FILENAME = "agentkit.toml"

# --- Layout constants (where the workflow lives in the target repo) ---------
TOOLS_DIR = "tools/agent"
CHECKS_DIR = f"{TOOLS_DIR}/checks"
SKILLS_DIR = f"{TOOLS_DIR}/skills"
DOCS_AGENT_DIR = "docs/agent"

# --- The lean-core skill set (template dir name -> behaviour) ---------------
# Each skill is published as ``<slug>-<dir>``. ``core`` is the always-on router;
# ``diagnose`` and ``handoff`` are self-contained (no mirrored references).
SKILL_DIRS = ["core", "task-workflow", "review", "docs-sync", "diagnose", "handoff"]

# Generic process docs written under docs/agent/.
AGENT_DOCS = [
    "task-format.md",
    "review-checklist.md",
    "architecture-review-checklist.md",
    "agent-output-review-checklist.md",
    "docs-sync-policy.md",
    "roles.md",
    "task-maturity.md",
    "prompt/prompt.md",
]

# Task templates copied verbatim into tasks/templates/.
TASK_TEMPLATES = ["task.md", "bug-task.md", "review-task.md"]

# Shipped, config-driven validators copied verbatim into tools/agent/checks/.
CHECK_FILES = [
    "_common.py",
    "check_tasks.py",
    "check_doc_links.py",
    "check_docs_sync.py",
    "check_pr_contract.py",
    "check_root_hygiene.py",
    "check_workflow_names.py",
    "check_agent_config.py",
    "check_prereqs.py",
]

# Source doc (relative to target root) -> (skill template dir, reference filename).
# Mirrors the repo's resync model: skill references are verbatim copies of the
# authoritative docs. The contract entry is added by build_resync_map() so it
# honors the configured contract filename. Kept in sync with resync_skills.sh.
RESYNC_DOC_MAP: list[tuple[str, str, str]] = [
    ("docs/agent/roles.md", "core", "roles.md"),
    ("docs/agent/prompt/prompt.md", "core", "session-onboarding.md"),
    ("docs/agent/task-format.md", "task-workflow", "task-format.md"),
    ("docs/agent/task-maturity.md", "task-workflow", "task-maturity.md"),
    ("tasks/templates/task.md", "task-workflow", "task-template.md"),
    ("docs/agent/review-checklist.md", "review", "review-checklist.md"),
    ("docs/agent/architecture-review-checklist.md", "review", "architecture-review-checklist.md"),
    ("docs/agent/agent-output-review-checklist.md", "review", "agent-output-review-checklist.md"),
    ("docs/agent/docs-sync-policy.md", "docs-sync", "docs-sync-policy.md"),
]


def build_resync_map(contract_file: str) -> list[tuple[str, str, str]]:
    """Full doc->reference map, with the contract mirrored from *contract_file*."""
    return [(contract_file, "core", "contract.md"), *RESYNC_DOC_MAP]

DEFAULT_TASK_PREFIXES = ["FEAT", "FIX", "DOCS", "CHORE", "REFAC", "TEST", "CI", "ARCH"]
DEFAULT_TASK_SECTIONS = [
    "Goal",
    "Non-goals",
    "Context",
    "Required changes",
    "Tests",
    "Docs",
    "Acceptance criteria",
    "Verification",
    "Forbidden changes",
]
DEFAULT_ACTIONABLE_SECTIONS = ["Required changes", "Tests", "Docs", "Acceptance criteria"]
DEFAULT_PR_SECTIONS = ["Summary", "Type", "Tests", "Docs", "Self-review", "Temporary shims"]
DEFAULT_ALLOWED_WORKFLOWS = ["pr-fast.yml", "ci-docs.yml"]
DEFAULT_IGNORE_GLOBS = [
    ".git",
    "build",
    "build-*",
    "cmake-build-*",
    "dist",
    "out",
    "node_modules",
    ".venv",
    "venv",
    "__pycache__",
    ".mypy_cache",
    ".pytest_cache",
    "target",
    "vendor",
    "third_party",
    "external",
]
DEFAULT_ALLOWED_ROOT_MARKDOWN = ["README.md", "AGENTS.md", "CLAUDE.md", "CHANGELOG.md"]
PLACEHOLDER_CMD = "<TODO: fill in your project's command>"


def slugify(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", name.strip().lower()).strip("-")
    return slug or "project"


def _allowed_root_markdown(contract_file: str) -> list[str]:
    # Always allow the configured contract file at the root (it may not be AGENTS.md).
    ordered = ["README.md", contract_file, "CLAUDE.md", "CHANGELOG.md"]
    seen: list[str] = []
    for name in ordered:
        if name not in seen:
            seen.append(name)
    return seen


def default_config(
    *,
    name: str,
    slug: str | None = None,
    description: str = "",
    language: str = "your project's language",
    contract_file: str = "AGENTS.md",
    harness: dict[str, bool] | None = None,
) -> dict[str, Any]:
    slug = slug or slugify(name)
    harness = harness or {}
    return {
        "project": {
            "name": name,
            "slug": slug,
            "description": description or f"{name} — agentic development workflow.",
            "language": language,
            "contract_file": contract_file,
        },
        "commands": {
            "configure": PLACEHOLDER_CMD,
            "build": PLACEHOLDER_CMD,
            "test": PLACEHOLDER_CMD,
            "lint": "",
        },
        "tasks": {
            "dirs": ["backlog", "active", "done", "templates"],
            "id_prefixes": list(DEFAULT_TASK_PREFIXES),
            "id_regex": r"^[A-Z][A-Z0-9]*-\d+[A-Za-z0-9]*$",
            "required_sections": list(DEFAULT_TASK_SECTIONS),
            "actionable_sections": list(DEFAULT_ACTIONABLE_SECTIONS),
            "legacy_root_files": ["TODO.md", "PLAN.md", "ROADMAP.md", "BUGS.md", "ACTIVE.md"],
        },
        "pr": {"required_sections": list(DEFAULT_PR_SECTIONS)},
        "workflows": {
            "allowed": list(DEFAULT_ALLOWED_WORKFLOWS),
            "required": list(DEFAULT_ALLOWED_WORKFLOWS),
        },
        "hygiene": {
            "allowed_root_markdown": _allowed_root_markdown(contract_file),
            "ignore_globs": list(DEFAULT_IGNORE_GLOBS),
            "expected_top_level": [],
        },
        "docs_sync": {"rules_file": f"{TOOLS_DIR}/docs_sync_rules.toml"},
        "harness": {
            "claude": harness.get("claude", True),
            "codex": harness.get("codex", True),
            "copilot": harness.get("copilot", True),
            "setup_hook": harness.get("setup_hook", True),
        },
    }


def load(root: Path) -> dict[str, Any]:
    """Load ``agentkit.toml`` from *root*."""
    path = Path(root) / CONFIG_FILENAME
    if not path.is_file():
        raise FileNotFoundError(f"no {CONFIG_FILENAME} found at {path}")
    with path.open("rb") as handle:
        return tomllib.load(handle)


def get(cfg: dict[str, Any], dotted: str, default: Any = None) -> Any:
    node: Any = cfg
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return default
        node = node[part]
    return node


def _toml_list(values: list[str]) -> str:
    return "[" + ", ".join(f'"{v}"' for v in values) + "]"


def build_context(cfg: dict[str, Any]) -> dict[str, object]:
    """Flatten *cfg* into the render context used by templates."""
    slug = get(cfg, "project.slug", "project")
    prefixes = get(cfg, "tasks.id_prefixes", DEFAULT_TASK_PREFIXES)
    example_prefix = prefixes[0] if prefixes else "FEAT"

    ctx: dict[str, object] = {
        "PROJECT_NAME": get(cfg, "project.name", "Project"),
        "PROJECT_SLUG": slug,
        "PROJECT_DESC": get(cfg, "project.description", ""),
        "LANGUAGE": get(cfg, "project.language", "your project's language"),
        "CONTRACT_FILE": get(cfg, "project.contract_file", "AGENTS.md"),
        "CONFIGURE_CMD": get(cfg, "commands.configure", PLACEHOLDER_CMD),
        "BUILD_CMD": get(cfg, "commands.build", PLACEHOLDER_CMD),
        "TEST_CMD": get(cfg, "commands.test", PLACEHOLDER_CMD),
        "LINT_CMD": get(cfg, "commands.lint", "") or PLACEHOLDER_CMD,
        "TOOLS_DIR": TOOLS_DIR,
        "CHECKS_DIR": CHECKS_DIR,
        "SKILLS_DIR": SKILLS_DIR,
        "DOCS_AGENT_DIR": DOCS_AGENT_DIR,
        "MARKER_PREFIX": slug,
        "DATE": _dt.date.today().isoformat(),
        "GENERATOR_VERSION": __version__,
        "TASK_ID_EXAMPLE": f"{example_prefix}-001",
        "TASK_PREFIXES_BLOCK": "\n".join(f"- `{p}-` — <describe ownership/area>." for p in prefixes),
        "TASK_DIRS_LIST": ", ".join(f"`tasks/{d}/`" for d in get(cfg, "tasks.dirs", [])),
        "RULES_FILE": get(cfg, "docs_sync.rules_file", f"{TOOLS_DIR}/docs_sync_rules.toml"),
        "ALLOWED_WORKFLOWS_TOML": _toml_list(get(cfg, "workflows.allowed", DEFAULT_ALLOWED_WORKFLOWS)),
    }
    # Per-skill name keys, e.g. SKILL_CORE, SKILL_TASK_WORKFLOW.
    for skill_dir in SKILL_DIRS:
        key = "SKILL_" + skill_dir.replace("-", "_").upper()
        ctx[key] = f"{slug}-{skill_dir}"
    return ctx
