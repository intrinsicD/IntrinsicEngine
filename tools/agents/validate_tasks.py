#!/usr/bin/env python3
"""Validate structured task files under tasks/.

Default mode emits warnings and exits 0 for validation findings.
Use --strict to fail with exit code 1 when findings are present.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

TASK_ID_RE = re.compile(r"^#\s+([A-Z]+-\d+[A-Z0-9-]*)\s+—\s+.+$")
SECTION_RE = re.compile(r"^##\s+(.+?)\s*$")
DATE_RE = re.compile(r"\b\d{4}-\d{2}-\d{2}\b")

REQUIRED_SECTIONS = [
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

ACTIONABLE_TODO_SECTIONS = [
    "Required changes",
    "Tests",
    "Docs",
    "Acceptance criteria",
]

# Micro tasks (`template: micro` in the front-matter, seeded from
# tasks/templates/task-micro.md) are single-slice mechanical work; they
# carry a reduced section set. Retirement rules (closed todos, completion
# date, commit/PR reference) apply unchanged.
MICRO_TEMPLATE_RE = re.compile(r"^template:\s*micro\s*$", re.MULTILINE)
WORKFLOW_SCHEMA_VERSION = 1
WORKFLOW_PROFILES = {
    "micro",
    "standard",
    "high-risk",
    "claim-grade",
    "protected",
}
WORKFLOW_EVIDENCE_VALUES = {"required", "not_applicable"}
WORKFLOW_LEGACY_INVENTORY = (
    Path(__file__).resolve().with_name("workflow_legacy_tasks.json")
)
CONTRACT_SCHEMA_VERSION = 1
CONTRACT_CATALOG = (
    Path(__file__).resolve().parents[2] / "docs" / "architecture" / "contract-catalog.yaml"
)
CONTRACT_LEGACY_INVENTORY = (
    Path(__file__).resolve().with_name("contract_legacy_tasks.json")
)
CONTRACT_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$")
METHOD_INTEGRATION_CONTRACT = "method.engine-integration"
METHOD_INTEGRATION_FIELDS = (
    "Least-structured input",
    "Compatible entity sources",
    "RuntimeModule",
    "Config/agent",
    "UI",
    "Publication",
    "End-to-end tests",
)

REQUIRED_SECTIONS_MICRO = [
    "Goal",
    "Acceptance criteria",
    "Verification",
]

ACTIONABLE_TODO_SECTIONS_MICRO = [
    "Acceptance criteria",
]

TODO_RE = re.compile(r"^\s*- \[[ xX]\]\s+.+", re.MULTILINE)
OPEN_TODO_RE = re.compile(r"^\s*- \[ \]\s+.+", re.MULTILINE)

SKIP_FILENAMES = {
    "README.md",
    "index.md",
    "legacy-todo.md",
    "0000-repo-reorganization-tracker.md",
    "RENDERING-CLEANUP-TASK-PACK.md",
    "RETIREMENT-LOG.md",
}

# Queue/index meta-files (e.g. `tasks/active/task-NN-<slug>.md`) are
# ordering trackers for a series of structured tasks rather than canonical
# tasks themselves; the structured tasks they describe live elsewhere
# under `tasks/backlog|done` with proper task IDs.
SKIP_PATTERNS = [
    re.compile(r"^task-\d+-.*\.md$"),
]

# Historical task-ID collisions frozen as-is: the files are link targets across
# docs, reports, and the retirement record, so renaming them would damage the
# audit trail for no behavioral gain. Each ID is allowed exactly the listed
# tasks/-relative paths; any file claiming one of these IDs from any other
# path is a violation, as is any new collision on an ID not listed here. Do
# not extend this allowlist for collisions created after 2026-06-09 (PROC-002).
GRANDFATHERED_DUPLICATE_IDS: dict[str, frozenset[str]] = {
    # Two sandbox bug records opened concurrently under the same number.
    "BUG-021": frozenset(
        {
            "archive/BUG-021-sandbox-camera-scene-table-shader-wiring.md",
            "archive/BUG-021-sandbox-drop-import-blocks-platform-poll.md",
        }
    ),
    "BUG-022": frozenset(
        {
            "archive/BUG-022-sandbox-nonmanifold-obj-import.md",
            "archive/BUG-022-sandbox-reference-triangle-camera-frustum-visibility.md",
        }
    ),
    # Three HARDEN streams (ECS parity, sandbox boundary, task policy) each
    # took the next free number without cross-checking the other directories.
    "HARDEN-065": frozenset(
        {
            "archive/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md",
            "archive/HARDEN-065-sandbox-runtime-boundary.md",
            "archive/HARDEN-065-task-checkbox-todo-policy.md",
        }
    ),
    "HARDEN-066": frozenset(
        {
            "archive/HARDEN-066-ecs-render-sync-export-policy.md",
            "archive/HARDEN-066-fix-halfedge-property-test-source-name.md",
        }
    ),
    "HARDEN-067": frozenset(
        {
            "archive/HARDEN-067-ecs-bounds-propagation-system.md",
            "archive/HARDEN-067-remove-stale-platform-linuxglfwvulkan.md",
        }
    ),
}


@dataclass
class Finding:
    level: str
    path: Path
    message: str


@dataclass
class ParsedTask:
    path: Path
    task_id: str | None
    sections: set[str]
    section_bodies: dict[str, str]
    content: str
    front_matter: str | None = None


def split_front_matter(content: str) -> tuple[str | None, str]:
    """Split a leading YAML front-matter block from the body, if present."""
    if not content.startswith("---\n"):
        return None, content
    end = content.find("\n---\n", len("---\n"))
    if end == -1:
        return None, content
    block = content[len("---\n") : end]
    body = content[end + len("\n---\n") :]
    return block, body


def parse_task(path: Path) -> ParsedTask:
    content = path.read_text(encoding="utf-8")
    front_matter, body = split_front_matter(content)
    lines = body.splitlines()

    task_id = None
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        m = TASK_ID_RE.match(stripped)
        if m:
            task_id = m.group(1)
        break

    sections: set[str] = set()
    section_bodies: dict[str, str] = {}
    current_section: str | None = None
    current_body: list[str] = []

    for line in lines:
        m = SECTION_RE.match(line.strip())
        if m:
            if current_section is not None:
                section_bodies[current_section] = "\n".join(current_body)
            current_section = m.group(1)
            current_body = []
            sections.add(current_section)
            continue
        if current_section is not None:
            current_body.append(line)

    if current_section is not None:
        section_bodies[current_section] = "\n".join(current_body)

    return ParsedTask(
        path=path,
        task_id=task_id,
        sections=sections,
        section_bodies=section_bodies,
        content=content,
        front_matter=front_matter,
    )


def find_markdown_files(root: Path) -> list[Path]:
    task_roots = [root / "active", root / "backlog", root / "done"]
    files: list[Path] = []
    for task_root in task_roots:
        if not task_root.exists():
            continue
        for path in sorted(task_root.rglob("*.md")):
            if path.name in SKIP_FILENAMES:
                continue
            if any(pattern.match(path.name) for pattern in SKIP_PATTERNS):
                continue
            files.append(path)
    return files


def find_archive_files(root: Path) -> list[Path]:
    """Archived (swept) retired tasks under tasks/archive/.

    Archived tasks are frozen history: they are exempt from per-file format
    validation (format rules may evolve past them), but their IDs stay
    authoritative — they participate in duplicate-ID detection and resolve
    `depends_on` references so retired dependencies keep unblocking backlog
    tasks after the sweep.
    """
    archive_root = root / "archive"
    if not archive_root.exists():
        return []
    files: list[Path] = []
    for path in sorted(archive_root.rglob("*.md")):
        if path.name in SKIP_FILENAMES:
            continue
        if any(pattern.match(path.name) for pattern in SKIP_PATTERNS):
            continue
        files.append(path)
    return files


def is_micro_task(parsed: ParsedTask) -> bool:
    return bool(parsed.front_matter and MICRO_TEMPLATE_RE.search(parsed.front_matter))


def validate_task(parsed: ParsedTask, mode: str) -> list[Finding]:
    findings: list[Finding] = []
    rel_path = parsed.path

    micro = is_micro_task(parsed)
    required_sections = REQUIRED_SECTIONS_MICRO if micro else REQUIRED_SECTIONS
    actionable_sections = (
        ACTIONABLE_TODO_SECTIONS_MICRO if micro else ACTIONABLE_TODO_SECTIONS
    )

    if not parsed.task_id:
        findings.append(
            Finding(
                "error", rel_path, "missing task header with ID (`# <ID> — <title>`)."
            )
        )

    missing_sections = [
        name for name in required_sections if name not in parsed.sections
    ]
    if missing_sections:
        findings.append(
            Finding(
                "error",
                rel_path,
                "missing required section(s): " + ", ".join(missing_sections),
            )
        )

    path_parts = {p.lower() for p in rel_path.parts}
    is_active = "active" in path_parts
    is_done = "done" in path_parts

    if is_active and "Acceptance criteria" not in parsed.sections:
        findings.append(
            Finding(
                "error", rel_path, "active task must include `## Acceptance criteria`."
            )
        )

    for section in actionable_sections:
        body = parsed.section_bodies.get(section, "")
        if section in parsed.sections and not TODO_RE.search(body):
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    f"`## {section}` must contain markable checkbox todo(s) (`- [ ]` or `- [x]`).",
                )
            )

    if is_done:
        for section in actionable_sections:
            body = parsed.section_bodies.get(section, "")
            if OPEN_TODO_RE.search(body):
                findings.append(
                    Finding(
                        "error",
                        rel_path,
                        f"done task has unchecked todo(s) in `## {section}`; move open work to a follow-up task.",
                    )
                )

    if is_done:
        has_completion_date = bool(DATE_RE.search(parsed.content))
        has_pr_or_commit_ref = any(
            needle in parsed.content.lower()
            for needle in ("pr:", "pull request", "commit:", "sha:", "commit reference")
        )

        if not has_completion_date:
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    "done task must include completion date (YYYY-MM-DD).",
                )
            )

        if not has_pr_or_commit_ref:
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    "done task must include PR/commit reference placeholder or value.",
                )
            )

    if mode == "warning":
        for finding in findings:
            finding.level = "warning"

    return findings


def validate_front_matter(
    parsed_tasks: list[ParsedTask],
    parsed_archive: list[ParsedTask] | None = None,
) -> list[Finding]:
    """Open (active/backlog) tasks must carry resolvable YAML front-matter.

    Schema: required `id` (equals the title-line ID), `theme` (non-empty
    string), `depends_on` (list of task IDs that exist somewhere under
    tasks/, including swept tasks under tasks/archive/); optional
    `maturity_target`. Done tasks are exempt — the brief generator only
    needs their existence.
    """
    findings: list[Finding] = []
    known_ids = {parsed.task_id for parsed in parsed_tasks if parsed.task_id}
    for parsed in parsed_archive or []:
        if parsed.task_id:
            known_ids.add(parsed.task_id)

    try:
        import yaml
    except ImportError:
        findings.append(
            Finding(
                "error",
                parsed_tasks[0].path if parsed_tasks else Path("tasks"),
                "pyyaml is required to validate task front-matter (pip install pyyaml).",
            )
        )
        return findings

    class UniqueKeyLoader(yaml.SafeLoader):
        """Safe YAML loader which rejects duplicate mapping keys."""

    def construct_unique_mapping(loader, node, deep=False):
        mapping = {}
        for key_node, value_node in node.value:
            key = loader.construct_object(key_node, deep=deep)
            if key in mapping:
                raise yaml.constructor.ConstructorError(
                    "while constructing a mapping",
                    node.start_mark,
                    f"found duplicate key {key!r}",
                    key_node.start_mark,
                )
            mapping[key] = loader.construct_object(value_node, deep=deep)
        return mapping

    UniqueKeyLoader.add_constructor(
        yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
        construct_unique_mapping,
    )

    legacy_hashes: dict[str, str] = {}
    if WORKFLOW_LEGACY_INVENTORY.is_file():
        try:
            inventory = json.loads(
                WORKFLOW_LEGACY_INVENTORY.read_text(encoding="utf-8")
            )
            if (
                isinstance(inventory, dict)
                and inventory.get("schema_version") == 1
                and isinstance(inventory.get("tasks"), dict)
            ):
                legacy_hashes = inventory["tasks"]
            else:
                raise ValueError("expected schema_version 1 and a tasks mapping")
        except (json.JSONDecodeError, OSError, ValueError) as exc:
            findings.append(
                Finding(
                    "error",
                    WORKFLOW_LEGACY_INVENTORY,
                    f"workflow legacy inventory is invalid: {exc}",
                )
            )

    contract_legacy_hashes: dict[str, str] = {}
    if CONTRACT_LEGACY_INVENTORY.is_file():
        try:
            inventory = json.loads(
                CONTRACT_LEGACY_INVENTORY.read_text(encoding="utf-8")
            )
            if (
                isinstance(inventory, dict)
                and inventory.get("schema_version") == CONTRACT_SCHEMA_VERSION
                and isinstance(inventory.get("tasks"), dict)
            ):
                contract_legacy_hashes = inventory["tasks"]
            else:
                raise ValueError(
                    f"expected schema_version {CONTRACT_SCHEMA_VERSION} and a tasks mapping"
                )
        except (json.JSONDecodeError, OSError, ValueError) as exc:
            findings.append(
                Finding(
                    "error",
                    CONTRACT_LEGACY_INVENTORY,
                    f"contract legacy inventory is invalid: {exc}",
                )
            )

    contract_ids: set[str] = set()
    try:
        catalog = yaml.load(
            CONTRACT_CATALOG.read_text(encoding="utf-8"), Loader=UniqueKeyLoader
        )
        if not isinstance(catalog, dict):
            raise ValueError("catalog root must be a YAML mapping")
        if catalog.get("schema_version") != CONTRACT_SCHEMA_VERSION:
            raise ValueError(
                f"schema_version must equal {CONTRACT_SCHEMA_VERSION}"
            )
        contracts = catalog.get("contracts")
        if not isinstance(contracts, dict) or not contracts:
            raise ValueError("contracts must be a non-empty mapping")
        for contract_id, contract in contracts.items():
            if not isinstance(contract_id, str) or not CONTRACT_ID_RE.fullmatch(
                contract_id
            ):
                raise ValueError(f"invalid stable contract ID `{contract_id}`")
            if not isinstance(contract, dict):
                raise ValueError(f"contract `{contract_id}` must be a mapping")
            owner = contract.get("owner")
            if not isinstance(owner, str) or not owner.strip():
                raise ValueError(f"contract `{contract_id}` needs a non-empty owner")
            source = contract.get("source")
            if not isinstance(source, str) or not source.strip():
                raise ValueError(f"contract `{contract_id}` needs a source path")
            source_path = CONTRACT_CATALOG.parents[2] / source
            if not source_path.is_file():
                raise ValueError(
                    f"contract `{contract_id}` source does not exist: {source}"
                )
            for field in ("applies_when", "proofs"):
                values = contract.get(field)
                if (
                    not isinstance(values, list)
                    or not values
                    or any(not isinstance(value, str) or not value.strip() for value in values)
                ):
                    raise ValueError(
                        f"contract `{contract_id}` needs a non-empty string list `{field}`"
                    )
            for proof in contract["proofs"]:
                proof_path = CONTRACT_CATALOG.parents[2] / proof
                if not proof_path.is_file():
                    raise ValueError(
                        f"contract `{contract_id}` proof does not exist: {proof}"
                    )
            contract_ids.add(contract_id)
    except (OSError, ValueError, yaml.YAMLError) as exc:
        findings.append(
            Finding("error", CONTRACT_CATALOG, f"contract catalog is invalid: {exc}")
        )

    tasks_root = _tasks_root(parsed_tasks)

    for parsed in parsed_tasks:
        path_parts = {p.lower() for p in parsed.path.parts}
        micro = is_micro_task(parsed)

        if parsed.front_matter is None:
            if "done" in path_parts:
                continue
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "open task is missing YAML front-matter (id/theme/depends_on); "
                    "see docs/agent/task-format.md.",
                )
            )
            continue

        try:
            data = yaml.load(parsed.front_matter, Loader=UniqueKeyLoader)
        except yaml.YAMLError as exc:
            findings.append(
                Finding("error", parsed.path, f"front-matter is not valid YAML: {exc}")
            )
            continue
        if not isinstance(data, dict):
            findings.append(
                Finding("error", parsed.path, "front-matter must be a YAML mapping.")
            )
            continue

        fm_id = data.get("id")
        if not isinstance(fm_id, str) or fm_id != parsed.task_id:
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    f"front-matter id `{fm_id}` must equal the title-line ID `{parsed.task_id}`.",
                )
            )

        if "done" in path_parts and "workflow_schema" not in data:
            if "contract_schema" not in data:
                continue

        theme = data.get("theme")
        if not isinstance(theme, str) or not theme.strip():
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "front-matter `theme` must be a non-empty string.",
                )
            )

        depends_on = data.get("depends_on")
        if not isinstance(depends_on, list):
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "front-matter `depends_on` must be a list (may be empty).",
                )
            )
        else:
            for dep in depends_on:
                if not isinstance(dep, str) or dep not in known_ids:
                    findings.append(
                        Finding(
                            "error",
                            parsed.path,
                            f"front-matter dependency `{dep}` does not resolve to any task "
                            "under tasks/active|backlog|done|archive.",
                        )
                    )

        contract_schema = data.get("contract_schema")
        if contract_schema is None:
            if "done" not in path_parts:
                rel = _task_inventory_path(parsed.path, tasks_root)
                expected_hash = contract_legacy_hashes.get(rel)
                actual_hash = hashlib.sha256(parsed.path.read_bytes()).hexdigest()
                if expected_hash != actual_hash:
                    findings.append(
                        Finding(
                            "error",
                            parsed.path,
                            "new or changed open task is outside the prospective "
                            "contract inventory and must declare `contract_schema: 1`; "
                            "untouched historical tasks alone are grandfathered.",
                        )
                    )
        elif contract_schema != CONTRACT_SCHEMA_VERSION:
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    f"front-matter `contract_schema` must equal "
                    f"{CONTRACT_SCHEMA_VERSION}.",
                )
            )
        else:
            declared_contracts = data.get("contracts")
            if not isinstance(declared_contracts, list):
                findings.append(
                    Finding(
                        "error",
                        parsed.path,
                        "front-matter `contracts` must be a list (may be empty).",
                    )
                )
            else:
                seen_contracts: set[str] = set()
                for contract_id in declared_contracts:
                    if not isinstance(contract_id, str) or contract_id not in contract_ids:
                        findings.append(
                            Finding(
                                "error",
                                parsed.path,
                                f"front-matter contract `{contract_id}` is not a known ID "
                                "in docs/architecture/contract-catalog.yaml.",
                            )
                        )
                    elif contract_id in seen_contracts:
                        findings.append(
                            Finding(
                                "error",
                                parsed.path,
                                f"front-matter contract `{contract_id}` is duplicated.",
                            )
                        )
                    seen_contracts.add(contract_id)

                contract_review = data.get("contract_review")
                if not declared_contracts and (
                    not isinstance(contract_review, str) or not contract_review.strip()
                ):
                    findings.append(
                        Finding(
                            "error",
                            parsed.path,
                            "empty `contracts` requires a non-empty `contract_review` "
                            "explaining why no catalog contract applies.",
                        )
                    )

                if METHOD_INTEGRATION_CONTRACT in seen_contracts:
                    integration = parsed.section_bodies.get("Engine integration")
                    if integration is None:
                        findings.append(
                            Finding(
                                "error",
                                parsed.path,
                                "`method.engine-integration` requires an "
                                "`## Engine integration` section.",
                            )
                        )
                    else:
                        missing_fields = [
                            field
                            for field in METHOD_INTEGRATION_FIELDS
                            if field not in integration
                        ]
                        if missing_fields:
                            findings.append(
                                Finding(
                                    "error",
                                    parsed.path,
                                    "`## Engine integration` is missing field(s): "
                                    + ", ".join(missing_fields)
                                    + ".",
                                )
                            )

        workflow_schema = data.get("workflow_schema")
        if workflow_schema is None:
            if "done" not in path_parts:
                rel = _task_inventory_path(parsed.path, tasks_root)
                expected_hash = legacy_hashes.get(rel)
                actual_hash = hashlib.sha256(parsed.path.read_bytes()).hexdigest()
                if expected_hash != actual_hash:
                    findings.append(
                        Finding(
                            "error",
                            parsed.path,
                            "new or changed open task is outside the prospective "
                            "workflow inventory and must declare `workflow_schema: 1`; "
                            "untouched historical tasks alone are grandfathered.",
                        )
                    )
            continue

        if workflow_schema != WORKFLOW_SCHEMA_VERSION:
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    f"front-matter `workflow_schema` must equal "
                    f"{WORKFLOW_SCHEMA_VERSION}.",
                )
            )
            continue

        profile = data.get("workflow_profile")
        if profile not in WORKFLOW_PROFILES:
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "front-matter `workflow_profile` must be one of: "
                    + ", ".join(sorted(WORKFLOW_PROFILES))
                    + ".",
                )
            )

        evidence = data.get("evidence")
        if evidence not in WORKFLOW_EVIDENCE_VALUES:
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "front-matter `evidence` must be `required` or `not_applicable`.",
                )
            )

        if micro and profile != "micro":
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "`template: micro` requires `workflow_profile: micro`.",
                )
            )
        if not micro and profile == "micro":
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "`workflow_profile: micro` requires `template: micro`.",
                )
            )

        skip_reason = data.get("evidence_skip_reason")
        if profile == "micro":
            if evidence != "not_applicable":
                findings.append(
                    Finding(
                        "error",
                        parsed.path,
                        "micro tasks must use `evidence: not_applicable`.",
                    )
                )
            if not isinstance(skip_reason, str) or not skip_reason.strip():
                findings.append(
                    Finding(
                        "error",
                        parsed.path,
                        "micro evidence exemption requires a non-empty "
                        "`evidence_skip_reason`.",
                    )
                )
        elif evidence != "required":
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "standard and higher profiles require `evidence: required`.",
                )
            )

        for key in ("owner", "branch", "worktree"):
            value = data.get(key)
            if "active" in path_parts:
                if not isinstance(value, str) or not value.strip():
                    findings.append(
                        Finding(
                            "error",
                            parsed.path,
                            f"active workflow task requires non-empty `{key}`.",
                        )
                    )
            elif value is not None and (
                not isinstance(value, str) or not value.strip()
            ):
                findings.append(
                    Finding(
                        "error",
                        parsed.path,
                        f"front-matter `{key}` must be null or a non-empty string.",
                    )
                )

        claimed_at = data.get("claimed_at")
        if "active" in path_parts:
            if claimed_at is None or not _is_timestamp_like(claimed_at):
                findings.append(
                    Finding(
                        "error",
                        parsed.path,
                        "active workflow task requires ISO-8601 `claimed_at`.",
                    )
                )
        elif claimed_at is not None and not _is_timestamp_like(claimed_at):
            findings.append(
                Finding(
                    "error",
                    parsed.path,
                    "front-matter `claimed_at` must be null or an ISO-8601 timestamp.",
                )
            )

    return findings


def _tasks_root(parsed_tasks: list[ParsedTask]) -> Path:
    if not parsed_tasks:
        return Path("tasks")
    path = parsed_tasks[0].path.resolve()
    for parent in (path, *path.parents):
        if parent.name == "tasks":
            return parent
    return path.parent


def _task_inventory_path(path: Path, tasks_root: Path) -> str:
    try:
        return path.resolve().relative_to(tasks_root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _is_timestamp_like(value: object) -> bool:
    if not isinstance(value, str) or "T" not in value:
        return False
    try:
        from datetime import datetime

        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def validate_id_uniqueness(
    parsed_tasks: list[ParsedTask], tasks_root: Path
) -> list[Finding]:
    findings: list[Finding] = []
    by_id: dict[str, list[ParsedTask]] = {}
    for parsed in parsed_tasks:
        if parsed.task_id:
            by_id.setdefault(parsed.task_id, []).append(parsed)

    def rel_path(owner: ParsedTask) -> str:
        try:
            return owner.path.relative_to(tasks_root).as_posix()
        except ValueError:
            return owner.path.as_posix()

    for task_id, owners in sorted(by_id.items()):
        if len(owners) <= 1:
            continue
        # Compare full tasks/-relative paths, not basenames: a grandfathered
        # file copied into another lifecycle directory keeps its basename but
        # must still be rejected.
        paths = {rel_path(owner) for owner in owners}
        if paths <= GRANDFATHERED_DUPLICATE_IDS.get(task_id, frozenset()):
            continue
        listing = ", ".join(sorted(str(owner.path) for owner in owners))
        findings.append(
            Finding(
                "error",
                owners[0].path,
                f"duplicate task ID `{task_id}` claimed by multiple files: {listing}. "
                "Allocate the next free number (see docs/agent/task-format.md, ID allocation).",
            )
        )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate structured task markdown files."
    )
    parser.add_argument(
        "--root",
        default="tasks",
        help="Path to the tasks root directory (default: tasks).",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit non-zero on findings. Default mode reports warnings and exits 0.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if not root.exists():
        print(f"ERROR: tasks root does not exist: {root}", file=sys.stderr)
        return 2

    files = find_markdown_files(root)
    if not files:
        searched = ", ".join(
            str(root / lifecycle) for lifecycle in ("active", "backlog", "done")
        )
        message = f"No task markdown files found; searched: {searched}."
        if args.strict:
            print(f"ERROR: {message}", file=sys.stderr)
            return 1
        print(f"WARNING: {message}")
        return 0

    mode = "strict" if args.strict else "warning"
    findings: list[Finding] = []

    parsed_tasks = [parse_task(file) for file in files]
    # Archived tasks are frozen history: IDs count (duplicate detection,
    # `depends_on` resolution), per-file format rules do not.
    parsed_archive = [parse_task(file) for file in find_archive_files(root)]

    for parsed in parsed_tasks:
        findings.extend(validate_task(parsed, mode=mode))

    findings.extend(validate_id_uniqueness(parsed_tasks + parsed_archive, root))
    findings.extend(validate_front_matter(parsed_tasks, parsed_archive))

    prefix = root.parent
    for finding in findings:
        try:
            display = finding.path.relative_to(prefix)
        except ValueError:
            display = finding.path
        print(f"[{finding.level.upper()}] {display}: {finding.message}")

    print(
        f"Validated {len(files)} task file(s); findings: {len(findings)}; mode: {mode}."
    )

    if args.strict and findings:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
