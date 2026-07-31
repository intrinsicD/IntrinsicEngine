#!/usr/bin/env python3
"""Generate and validate prospective task evidence for workflow schema v1.

The tool intentionally stores plain YAML, JSON, and JSONL under
``tasks/evidence/<TASK-ID>/``.  It is not a task runner: it derives what it
can from the task, Git, command receipts, and referenced files, then fails
closed when a completion assertion cannot be checked.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_tasks import (  # noqa: E402
    find_markdown_files,
    parse_task,
)

SCHEMA_VERSION = 1
PROFILE_ORDER = {
    "micro": 0,
    "standard": 1,
    "high-risk": 2,
    "claim-grade": 3,
    "protected": 4,
}
REPORT_REQUIRED_FIELDS = {
    "schema_version",
    "task_id",
    "workflow_profile",
    "status",
    "generated_by",
    "generated_at",
    "claim_eligible",
    "source",
    "touched",
    "extension_seam",
    "future_change_plan",
    "acceptance",
    "commands",
    "changed_public_contracts",
    "diagnostics_and_previews",
    "benchmark_or_parity_evidence",
    "docs_sync",
    "residual_risks",
    "skipped_checks",
    "artifacts",
    "self_review",
}
REPORT_STATUSES = {"draft", "complete", "blocked"}
ACCEPTANCE_DISPOSITIONS = {
    "satisfied",
    "deferred",
    "not_applicable",
    "pending",
}
REVIEW_VERDICTS = {
    "accepted",
    "revision-requested",
    "rejected",
    "inconclusive",
    "superseded",
}
TERMINAL_REVIEW_VERDICTS = {
    "accepted",
    "rejected",
    "inconclusive",
    "superseded",
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
REVISION_RE = re.compile(r"^(?:[0-9a-f]{40}|worktree:[0-9a-f]{64})$")
TASK_ID_RE = re.compile(r"^[A-Z]+-\d+[A-Z0-9-]*$")
LABEL_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$")
CHECKBOX_RE = re.compile(r"^\s*-\s+\[([ xX])\]\s+(.+?)\s*$", re.MULTILINE)


@dataclass(frozen=True)
class Finding:
    severity: str
    location: str
    message: str


def utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def strict_json_load(path: Path) -> dict[str, Any]:
    def reject_duplicate(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key {key!r}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"non-standard JSON constant {value!r}")

    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(
                handle,
                object_pairs_hook=reject_duplicate,
                parse_constant=reject_constant,
            )
    except (json.JSONDecodeError, OSError, ValueError) as exc:
        raise ValueError(f"{path}: invalid JSON ({exc})") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{path}: JSON root must be an object")
    return value


def strict_yaml_load_text(payload: str, location: str) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise ValueError("pyyaml is required (pip install pyyaml)") from exc

    class UniqueKeyLoader(yaml.SafeLoader):
        pass

    def construct_mapping(loader, node, deep=False):
        mapping: dict[Any, Any] = {}
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
        construct_mapping,
    )
    try:
        value = yaml.load(payload, Loader=UniqueKeyLoader)
    except yaml.YAMLError as exc:
        raise ValueError(f"{location}: invalid YAML ({exc})") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{location}: YAML root must be a mapping")
    return value


def strict_yaml_load(path: Path) -> dict[str, Any]:
    try:
        payload = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"{path}: cannot read YAML ({exc})") from exc
    return strict_yaml_load_text(payload, str(path))


def yaml_dump(data: dict[str, Any]) -> str:
    import yaml

    return yaml.safe_dump(
        data,
        allow_unicode=True,
        default_flow_style=False,
        sort_keys=False,
        width=100,
    )


def repo_root_from(path: Path) -> Path:
    probe = path.resolve()
    if probe.is_file():
        probe = probe.parent
    result = subprocess.run(
        ["git", "-C", str(probe), "rev-parse", "--show-toplevel"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"not inside a Git worktree: {probe}")
    return Path(result.stdout.strip()).resolve()


def git(repo_root: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError(f"git {' '.join(args)} failed: {detail}")
    return result.stdout.strip()


def relative_path(repo_root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError(f"path is outside repository: {path}") from exc


def repository_relative_path(value: object) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError("expected a non-empty repository-relative path")
    candidate = Path(value)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError(f"path must be repository-relative without '..': {value}")
    return candidate.as_posix()


def resolve_repo_path(repo_root: Path, value: object) -> Path:
    relative = repository_relative_path(value)
    resolved = (repo_root / relative).resolve()
    try:
        resolved.relative_to(repo_root.resolve())
    except ValueError as exc:
        raise ValueError(f"path escapes repository: {value}") from exc
    return resolved


def load_task_metadata(task_path: Path) -> tuple[Any, dict[str, Any]]:
    parsed = parse_task(task_path)
    if parsed.front_matter is None:
        raise ValueError(f"{task_path}: missing YAML front-matter")
    metadata = strict_yaml_load_text(parsed.front_matter, str(task_path))
    return parsed, metadata


def find_task(repo_root: Path, task_id: str) -> Path:
    matches: list[Path] = []
    for path in find_markdown_files(repo_root / "tasks"):
        parsed = parse_task(path)
        if parsed.task_id == task_id:
            matches.append(path)
    if len(matches) != 1:
        raise ValueError(
            f"task {task_id} resolves to {len(matches)} open/done task files"
        )
    return matches[0]


def acceptance_items(parsed: Any) -> list[tuple[str, bool]]:
    body = parsed.section_bodies.get("Acceptance criteria", "")
    return [
        (text.strip(), mark.lower() == "x") for mark, text in CHECKBOX_RE.findall(body)
    ]


def resolve_base_revision(repo_root: Path, base_ref: str) -> str:
    try:
        return git(repo_root, "merge-base", "HEAD", base_ref)
    except ValueError:
        return git(repo_root, "rev-parse", base_ref)


def changed_paths(
    repo_root: Path,
    base_revision: str,
    evidence_task_id: str | None = None,
) -> list[str]:
    tracked = {
        line
        for line in git(
            repo_root,
            "diff",
            "--name-only",
            "--diff-filter=ACDMRTUXB",
            base_revision,
            "--",
        ).splitlines()
        if line
    }
    untracked = {
        line
        for line in git(
            repo_root, "ls-files", "--others", "--exclude-standard"
        ).splitlines()
        if line
    }
    paths = tracked | untracked
    if evidence_task_id:
        prefix = f"tasks/evidence/{evidence_task_id}/"
        paths = {path for path in paths if not path.startswith(prefix)}
    return sorted(paths)


def changed_paths_between(
    repo_root: Path,
    base_revision: str,
    head_revision: str,
    evidence_task_id: str | None = None,
) -> list[str]:
    paths = {
        line
        for line in git(
            repo_root,
            "diff",
            "--name-only",
            "--diff-filter=ACDMRTUXB",
            base_revision,
            head_revision,
            "--",
        ).splitlines()
        if line
    }
    if evidence_task_id:
        prefix = f"tasks/evidence/{evidence_task_id}/"
        paths = {path for path in paths if not path.startswith(prefix)}
    return sorted(paths)


def require_commit(repo_root: Path, revision: object, location: str) -> str:
    if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError(f"{location} must be an exact 40-hex commit")
    git(repo_root, "cat-file", "-e", f"{revision}^{{commit}}")
    return revision


def sha256_at_revision(repo_root: Path, revision: str, value: object) -> str | None:
    relative = repository_relative_path(value)
    result = subprocess.run(
        ["git", "-C", str(repo_root), "cat-file", "blob", f"{revision}:{relative}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        return None
    return sha256_bytes(result.stdout)


def sha256_worktree_entry(repo_root: Path, value: object) -> str | None:
    relative = repository_relative_path(value)
    lexical_path = repo_root / relative
    if lexical_path.is_symlink():
        return sha256_bytes(os.readlink(os.fsencode(lexical_path)))
    path = resolve_repo_path(repo_root, value)
    return sha256_file(path) if path.is_file() else None


def surface_entries(repo_root: Path, paths: Iterable[str]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for value in sorted(set(paths)):
        entries.append(
            {
                "path": value,
                "sha256": sha256_worktree_entry(repo_root, value),
            }
        )
    return entries


def surface_digest(entries: list[dict[str, Any]]) -> str:
    canonical = json.dumps(
        entries,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256_bytes(canonical)


def report_exact_revision(report: dict[str, Any]) -> str:
    source = report.get("source")
    if not isinstance(source, dict):
        raise ValueError("report source must be a mapping")
    digest = source.get("content_digest")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError("report source content_digest must be lowercase sha256")
    dirty = source.get("dirty")
    if dirty is False:
        revision = source.get("head_revision")
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise ValueError("clean report source must bind an exact 40-hex commit")
        return revision
    if dirty is True:
        return f"worktree:{digest}"
    raise ValueError("report source dirty state must be a boolean")


def require_current_report_binding(
    repo_root: Path,
    task_id: str,
    revision: str,
    content_digest: str,
) -> None:
    report_path = evidence_root(repo_root, task_id) / "report.yaml"
    if not report_path.is_file():
        raise ValueError("generate report.yaml before appending handoff or review")
    report = strict_yaml_load(report_path)
    if report.get("task_id") != task_id:
        raise ValueError("report task_id does not match appended record")
    expected_revision = report_exact_revision(report)
    expected_digest = report.get("source", {}).get("content_digest")
    if revision != expected_revision:
        raise ValueError(
            "record revision does not match report source revision "
            f"(expected {expected_revision})"
        )
    if content_digest != expected_digest:
        raise ValueError("record content digest does not match report source digest")


def evidence_root(repo_root: Path, task_id: str) -> Path:
    if not TASK_ID_RE.fullmatch(task_id):
        raise ValueError(f"invalid task ID: {task_id}")
    return repo_root / "tasks" / "evidence" / task_id


def _atomic_write(path: Path, payload: bytes, replace: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not replace:
        raise ValueError(f"refusing to overwrite existing file: {path}")
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    fd = os.open(temporary, flags, 0o644)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _append_jsonl(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+", encoding="utf-8") as handle:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        handle.seek(0)
        prior_lines = [line for line in handle.read().splitlines() if line.strip()]
        previous_hash = (
            sha256_bytes(prior_lines[-1].encode("utf-8")) if prior_lines else None
        )
        record["sequence"] = len(prior_lines) + 1
        record["previous_entry_sha256"] = previous_hash
        encoded = json.dumps(
            record, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        )
        handle.seek(0, os.SEEK_END)
        handle.write(encoded + "\n")
        handle.flush()
        os.fsync(handle.fileno())
        fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError(f"{path}: cannot read JSONL ({exc})") from exc
    previous_encoded: str | None = None
    for index, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        try:
            value = json.loads(
                line,
                object_pairs_hook=_reject_json_duplicate,
                parse_constant=_reject_json_constant,
            )
        except (json.JSONDecodeError, ValueError) as exc:
            raise ValueError(f"{path}:{index}: invalid JSON ({exc})") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{index}: JSONL record must be an object")
        expected_sequence = len(records) + 1
        if value.get("sequence") != expected_sequence:
            raise ValueError(f"{path}:{index}: sequence must equal {expected_sequence}")
        expected_previous = (
            sha256_bytes(previous_encoded.encode("utf-8"))
            if previous_encoded is not None
            else None
        )
        if value.get("previous_entry_sha256") != expected_previous:
            raise ValueError(f"{path}:{index}: previous-entry hash chain is broken")
        records.append(value)
        previous_encoded = line
    return records


def _reject_json_duplicate(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-standard JSON constant {value!r}")


def _parse_key_value(values: Iterable[str], option: str) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for value in values:
        if "=" not in value:
            raise ValueError(f"{option} expects NAME=REASON, got: {value}")
        name, reason = value.split("=", 1)
        if not name.strip() or not reason.strip():
            raise ValueError(f"{option} expects non-empty NAME=REASON")
        result.append({"check": name.strip(), "reason": reason.strip()})
    return result


def record_command(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    task_path = find_task(repo_root, args.task_id)
    _, metadata = load_task_metadata(task_path)
    if metadata.get("workflow_schema") != SCHEMA_VERSION:
        raise ValueError(f"{args.task_id} is not enrolled in workflow schema v1")
    if not LABEL_RE.fullmatch(args.label):
        raise ValueError(
            "label must start with an alphanumeric character and contain only "
            "letters, digits, '.', '_' or '-'"
        )
    command = list(args.command)
    if command and command[0] == "--":
        command = command[1:]
    if args.shell:
        if command:
            raise ValueError("use either --shell or an argv command after '--'")
        command = ["/bin/bash", "-o", "pipefail", "-c", args.shell]
    if not command:
        raise ValueError("missing command")

    root = evidence_root(repo_root, args.task_id) / "commands"
    receipt_path = root / f"{args.label}.json"
    stdout_path = root / f"{args.label}.stdout.log"
    stderr_path = root / f"{args.label}.stderr.log"
    for target in (receipt_path, stdout_path, stderr_path):
        if target.exists():
            raise ValueError(f"refusing to overwrite command evidence: {target}")

    cwd = Path(args.cwd).resolve() if args.cwd else repo_root
    try:
        cwd.relative_to(repo_root)
    except ValueError as exc:
        raise ValueError("command cwd must be inside the repository") from exc

    started = utc_now()
    started_monotonic = time.monotonic()
    result = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    duration_ms = round((time.monotonic() - started_monotonic) * 1000)
    finished = utc_now()
    _atomic_write(stdout_path, result.stdout)
    _atomic_write(stderr_path, result.stderr)
    receipt = {
        "schema_version": SCHEMA_VERSION,
        "task_id": args.task_id,
        "label": args.label,
        "command": command,
        "command_display": shlex.join(command),
        "cwd": relative_path(repo_root, cwd) or ".",
        "required": not args.optional,
        "started_at": started,
        "finished_at": finished,
        "duration_ms": duration_ms,
        "exit_code": result.returncode,
        "stdout_path": relative_path(repo_root, stdout_path),
        "stdout_sha256": sha256_bytes(result.stdout),
        "stderr_path": relative_path(repo_root, stderr_path),
        "stderr_sha256": sha256_bytes(result.stderr),
    }
    _atomic_write(
        receipt_path,
        (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(f"Wrote {relative_path(repo_root, receipt_path)} (exit {result.returncode}).")
    return result.returncode


def generate_report(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    task_path = find_task(repo_root, args.task_id)
    parsed, metadata = load_task_metadata(task_path)
    if metadata.get("workflow_schema") != SCHEMA_VERSION:
        raise ValueError(f"{args.task_id} is not enrolled in workflow schema v1")
    profile = metadata.get("workflow_profile")
    if profile not in PROFILE_ORDER:
        raise ValueError(f"{args.task_id} has invalid workflow profile {profile!r}")

    base_revision = resolve_base_revision(repo_root, args.base_ref)
    paths = changed_paths(repo_root, base_revision, args.task_id)
    entries = surface_entries(repo_root, paths)
    receipts = sorted(
        relative_path(repo_root, path)
        for path in (evidence_root(repo_root, args.task_id) / "commands").glob("*.json")
    )
    acceptance = [
        {
            "criterion": text,
            "task_checked": checked,
            "disposition": "satisfied" if checked else "pending",
            "evidence": [],
            "reason": None,
        }
        for text, checked in acceptance_items(parsed)
    ]
    artifacts: list[dict[str, Any]] = []
    for value in args.artifact:
        path = resolve_repo_path(repo_root, value)
        if not path.is_file():
            raise ValueError(f"artifact is not a file: {value}")
        artifacts.append(
            {
                "path": value,
                "sha256": sha256_worktree_entry(repo_root, value),
                "kind": "file",
            }
        )

    status = "complete" if args.complete else "draft"
    report = {
        "schema_version": SCHEMA_VERSION,
        "task_id": args.task_id,
        "workflow_profile": profile,
        "status": status,
        "generated_by": "tools/agents/workflow_evidence.py",
        "generated_at": utc_now(),
        "claim_eligible": bool(args.claim_eligible),
        "source": {
            "base_ref": args.base_ref,
            "base_revision": base_revision,
            "head_revision": git(repo_root, "rev-parse", "HEAD"),
            "branch": git(repo_root, "branch", "--show-current"),
            "worktree": str(repo_root),
            "dirty": bool(git(repo_root, "status", "--porcelain")),
            "surface": entries,
            "content_digest": surface_digest(entries),
        },
        "touched": {
            "paths": paths,
            "layers": sorted(set(args.layer)),
            "modules": sorted(set(args.module)),
        },
        "extension_seam": args.extension_seam,
        "future_change_plan": args.future_change_plan,
        "acceptance": acceptance,
        "commands": [{"receipt": value} for value in receipts],
        "changed_public_contracts": list(args.public_contract),
        "diagnostics_and_previews": list(args.diagnostic),
        "benchmark_or_parity_evidence": list(args.benchmark_evidence),
        "docs_sync": {
            "task_file": relative_path(repo_root, task_path),
            "session_brief_present": (repo_root / "tasks/SESSION-BRIEF.md").is_file(),
            "canonical_docs_changed": any(path.startswith("docs/") for path in paths),
            "skill_mirrors_checked": bool(args.skill_mirrors_checked),
        },
        "residual_risks": list(args.risk),
        "skipped_checks": _parse_key_value(args.skip, "--skip"),
        "artifacts": artifacts,
        "self_review": {
            "completed": bool(args.self_review_complete),
            "scope_matches_task": bool(args.self_review_complete),
            "layering_preserved": bool(args.self_review_complete),
            "tests_and_docs_accounted_for": bool(args.self_review_complete),
            "mechanical_and_semantic_changes_separated": bool(
                args.self_review_complete
            ),
        },
    }
    if PROFILE_ORDER[profile] >= PROFILE_ORDER["high-risk"]:
        report["handoff"] = f"tasks/evidence/{args.task_id}/handoff.jsonl"
        report["review"] = f"tasks/evidence/{args.task_id}/reviews.jsonl"
    if PROFILE_ORDER[profile] >= PROFILE_ORDER["claim-grade"]:
        report["experiment_root"] = f"tasks/evidence/{args.task_id}/experiment"

    out = evidence_root(repo_root, args.task_id) / "report.yaml"
    _atomic_write(
        out,
        yaml_dump(report).encode("utf-8"),
        replace=args.replace,
    )
    print(f"Wrote {relative_path(repo_root, out)} ({status}).")
    return 0


def append_handoff(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    task_path = find_task(repo_root, args.task_id)
    _, metadata = load_task_metadata(task_path)
    profile = metadata.get("workflow_profile")
    if profile not in PROFILE_ORDER or PROFILE_ORDER[profile] < 2:
        raise ValueError("handoffs are required only for high-risk and higher profiles")
    if not REVISION_RE.fullmatch(args.revision):
        raise ValueError("revision must be a 40-hex commit or worktree:<sha256>")
    if not SHA256_RE.fullmatch(args.content_digest):
        raise ValueError("content digest must be lowercase sha256")
    require_current_report_binding(
        repo_root,
        args.task_id,
        args.revision,
        args.content_digest,
    )
    record = {
        "schema_version": SCHEMA_VERSION,
        "task_id": args.task_id,
        "recorded_at": utc_now(),
        "driver_label": args.driver,
        "revision": args.revision,
        "content_digest": args.content_digest,
        "summary": args.summary,
        "changed_surface": list(args.changed_surface),
        "evidence": list(args.evidence),
        "assumptions": list(args.assumption),
        "failed_hypotheses": list(args.failed_hypothesis),
        "known_traps": list(args.known_trap),
        "untested_surfaces": list(args.untested_surface),
        "human_decisions": list(args.human_decision),
        "disagreements": list(args.disagreement),
        "next_action": args.next_action,
    }
    path = evidence_root(repo_root, args.task_id) / "handoff.jsonl"
    _append_jsonl(path, record)
    print(f"Appended handoff to {relative_path(repo_root, path)}.")
    return 0


def append_review(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    task_path = find_task(repo_root, args.task_id)
    _, metadata = load_task_metadata(task_path)
    profile = metadata.get("workflow_profile")
    if profile not in PROFILE_ORDER or PROFILE_ORDER[profile] < 2:
        raise ValueError("reviews are required only for high-risk and higher profiles")
    if args.verdict not in REVIEW_VERDICTS:
        raise ValueError(f"invalid review verdict: {args.verdict}")
    if not REVISION_RE.fullmatch(args.revision):
        raise ValueError("revision must be a 40-hex commit or worktree:<sha256>")
    if not SHA256_RE.fullmatch(args.content_digest):
        raise ValueError("content digest must be lowercase sha256")
    require_current_report_binding(
        repo_root,
        args.task_id,
        args.revision,
        args.content_digest,
    )
    review_kind = "self" if args.self_review else "independent"
    if review_kind == "self" and args.verdict == "accepted":
        raise ValueError("self-review is provisional and cannot accept a change")
    if review_kind == "independent" and args.reviewer == args.driver:
        raise ValueError("independent reviewer label must differ from driver label")
    if args.revision_count > 3 and not args.escalation:
        raise ValueError("revision count above three requires an escalation record")
    record = {
        "schema_version": SCHEMA_VERSION,
        "task_id": args.task_id,
        "recorded_at": utc_now(),
        "driver_label": args.driver,
        "reviewer_label": args.reviewer,
        "review_kind": review_kind,
        "identity_assurance": "cooperative-label-only",
        "revision": args.revision,
        "content_digest": args.content_digest,
        "revision_count": args.revision_count,
        "verdict": args.verdict,
        "findings": list(args.finding),
        "untested_surfaces": list(args.untested_surface),
        "disagreements": list(args.disagreement),
        "escalation": args.escalation,
    }
    path = evidence_root(repo_root, args.task_id) / "reviews.jsonl"
    _append_jsonl(path, record)
    print(f"Appended {review_kind} review to {relative_path(repo_root, path)}.")
    return 0


def _require_mapping(
    value: object, location: str, findings: list[Finding]
) -> dict[str, Any]:
    if not isinstance(value, dict):
        findings.append(Finding("error", location, "must be a mapping"))
        return {}
    return value


def _require_list(value: object, location: str, findings: list[Finding]) -> list[Any]:
    if not isinstance(value, list):
        findings.append(Finding("error", location, "must be a list"))
        return []
    return value


def validate_receipt(
    repo_root: Path, task_id: str, receipt_value: object
) -> tuple[dict[str, Any] | None, list[Finding]]:
    findings: list[Finding] = []
    try:
        path = resolve_repo_path(repo_root, receipt_value)
    except ValueError as exc:
        return None, [Finding("error", "commands", str(exc))]
    try:
        data = strict_json_load(path)
    except ValueError as exc:
        return None, [Finding("error", str(path), str(exc))]
    required = {
        "schema_version",
        "task_id",
        "label",
        "command",
        "command_display",
        "cwd",
        "required",
        "started_at",
        "finished_at",
        "duration_ms",
        "exit_code",
        "stdout_path",
        "stdout_sha256",
        "stderr_path",
        "stderr_sha256",
    }
    missing = sorted(required - data.keys())
    if missing:
        findings.append(
            Finding("error", str(path), f"missing fields: {', '.join(missing)}")
        )
    if data.get("schema_version") != SCHEMA_VERSION:
        findings.append(Finding("error", str(path), "schema_version must equal 1"))
    if data.get("task_id") != task_id:
        findings.append(Finding("error", str(path), "task_id does not match report"))
    if not isinstance(data.get("command"), list) or not data.get("command"):
        findings.append(
            Finding("error", str(path), "command must be a non-empty argv list")
        )
    if type(data.get("exit_code")) is not int:
        findings.append(Finding("error", str(path), "exit_code must be an integer"))
    if type(data.get("required")) is not bool:
        findings.append(Finding("error", str(path), "required must be a boolean"))
    for stream in ("stdout", "stderr"):
        try:
            log_path = resolve_repo_path(repo_root, data.get(f"{stream}_path"))
            expected = data.get(f"{stream}_sha256")
            if not log_path.is_file():
                raise ValueError(f"missing {stream} log: {log_path}")
            actual = sha256_file(log_path)
            if actual != expected:
                raise ValueError(
                    f"{stream} log hash mismatch: expected {expected}, got {actual}"
                )
        except ValueError as exc:
            findings.append(Finding("error", str(path), str(exc)))
    return data, findings


def validate_report(
    repo_root: Path,
    task_path: Path,
    metadata: dict[str, Any],
    report_path: Path,
    require_complete: bool,
) -> list[Finding]:
    findings: list[Finding] = []
    parsed = parse_task(task_path)
    try:
        report = strict_yaml_load(report_path)
    except ValueError as exc:
        return [Finding("error", str(report_path), str(exc))]

    missing = sorted(REPORT_REQUIRED_FIELDS - report.keys())
    if missing:
        findings.append(
            Finding(
                "error",
                str(report_path),
                f"missing required fields: {', '.join(missing)}",
            )
        )
    task_id = parsed.task_id or ""
    if report.get("schema_version") != SCHEMA_VERSION:
        findings.append(
            Finding("error", str(report_path), "schema_version must equal 1")
        )
    if report.get("task_id") != task_id:
        findings.append(
            Finding("error", str(report_path), "task_id does not match task")
        )
    profile = metadata.get("workflow_profile")
    if report.get("workflow_profile") != profile:
        findings.append(
            Finding("error", str(report_path), "workflow_profile does not match task")
        )
    status = report.get("status")
    if status not in REPORT_STATUSES:
        findings.append(Finding("error", str(report_path), "invalid report status"))
    if require_complete and status != "complete":
        findings.append(Finding("error", str(report_path), "report must be complete"))
    if report.get("generated_by") != "tools/agents/workflow_evidence.py":
        findings.append(
            Finding("error", str(report_path), "report is not generator-attributed")
        )
    if type(report.get("claim_eligible")) is not bool:
        findings.append(
            Finding("error", str(report_path), "claim_eligible must be a boolean")
        )
    elif report["claim_eligible"] and (
        profile not in PROFILE_ORDER or PROFILE_ORDER[profile] < 3
    ):
        findings.append(
            Finding(
                "error",
                str(report_path),
                "claim eligibility requires claim-grade or protected profile",
            )
        )

    source = _require_mapping(report.get("source"), "source", findings)
    entries = _require_list(source.get("surface"), "source.surface", findings)
    dirty = source.get("dirty")
    if type(dirty) is not bool:
        findings.append(Finding("error", "source.dirty", "must be a boolean"))
    clean_head: str | None = None
    if dirty is False:
        try:
            clean_head = require_commit(
                repo_root, source.get("head_revision"), "source.head_revision"
            )
        except ValueError as exc:
            findings.append(Finding("error", "source.head_revision", str(exc)))
    checked_entries: list[dict[str, Any]] = []
    for index, entry_value in enumerate(entries):
        entry = _require_mapping(entry_value, f"source.surface[{index}]", findings)
        try:
            expected_hash = entry.get("sha256")
            if clean_head is not None:
                actual_hash = sha256_at_revision(
                    repo_root, clean_head, entry.get("path")
                )
            elif dirty is True:
                actual_hash = sha256_worktree_entry(repo_root, entry.get("path"))
            else:
                actual_hash = expected_hash
            if actual_hash != expected_hash:
                findings.append(
                    Finding(
                        "error",
                        f"source.surface[{index}]",
                        f"content hash mismatch for {entry.get('path')}",
                    )
                )
            checked_entries.append({"path": entry.get("path"), "sha256": expected_hash})
        except ValueError as exc:
            findings.append(Finding("error", f"source.surface[{index}]", str(exc)))
    if surface_digest(checked_entries) != source.get("content_digest"):
        findings.append(
            Finding("error", "source.content_digest", "surface digest mismatch")
        )
    base_revision = source.get("base_revision")
    if isinstance(base_revision, str) and base_revision:
        try:
            require_commit(repo_root, base_revision, "source.base_revision")
            if clean_head is not None:
                observed = changed_paths_between(
                    repo_root, base_revision, clean_head, task_id
                )
                mismatch_detail = (
                    "recorded changed-path surface does not match fixed revision diff"
                )
            elif dirty is True:
                observed = changed_paths(repo_root, base_revision, task_id)
                mismatch_detail = (
                    "recorded changed-path surface does not match current worktree diff"
                )
            else:
                observed = []
                mismatch_detail = "source revision state is invalid"
            recorded = sorted(
                entry.get("path")
                for entry in checked_entries
                if isinstance(entry.get("path"), str)
            )
            if observed != recorded:
                findings.append(
                    Finding(
                        "error",
                        "source.surface",
                        mismatch_detail,
                    )
                )
        except ValueError as exc:
            findings.append(Finding("error", "source.base_revision", str(exc)))
    if report.get("claim_eligible"):
        head = source.get("head_revision")
        if not isinstance(head, str) or not re.fullmatch(r"[0-9a-f]{40}", head):
            findings.append(
                Finding(
                    "error",
                    "source.head_revision",
                    "claim source must be an exact commit",
                )
            )
        if source.get("dirty") is not False:
            findings.append(
                Finding("error", "source.dirty", "claim source must be clean")
            )

    expected_acceptance = {text: checked for text, checked in acceptance_items(parsed)}
    acceptance = _require_list(report.get("acceptance"), "acceptance", findings)
    observed_acceptance: dict[str, dict[str, Any]] = {}
    for index, value in enumerate(acceptance):
        entry = _require_mapping(value, f"acceptance[{index}]", findings)
        criterion = entry.get("criterion")
        if not isinstance(criterion, str) or criterion in observed_acceptance:
            findings.append(
                Finding(
                    "error", f"acceptance[{index}]", "criterion must be unique text"
                )
            )
            continue
        observed_acceptance[criterion] = entry
        disposition = entry.get("disposition")
        if disposition not in ACCEPTANCE_DISPOSITIONS:
            findings.append(
                Finding("error", f"acceptance[{index}]", "invalid disposition")
            )
        reason = entry.get("reason")
        if disposition in {"deferred", "not_applicable"} and (
            not isinstance(reason, str) or not reason.strip()
        ):
            findings.append(
                Finding(
                    "error",
                    f"acceptance[{index}]",
                    f"{disposition} requires a reason",
                )
            )
    if set(observed_acceptance) != set(expected_acceptance):
        findings.append(
            Finding(
                "error",
                "acceptance",
                "report criteria do not exactly match task acceptance criteria",
            )
        )
    for criterion, checked in expected_acceptance.items():
        entry = observed_acceptance.get(criterion, {})
        if entry.get("task_checked") is not checked:
            findings.append(
                Finding("error", "acceptance", f"checkbox state mismatch: {criterion}")
            )
        if status == "complete" and (
            not checked
            or entry.get("disposition")
            not in {
                "satisfied",
                "deferred",
                "not_applicable",
            }
        ):
            findings.append(
                Finding(
                    "error",
                    "acceptance",
                    f"complete report leaves criterion unresolved: {criterion}",
                )
            )

    commands = _require_list(report.get("commands"), "commands", findings)
    required_receipt_count = 0
    for index, value in enumerate(commands):
        entry = _require_mapping(value, f"commands[{index}]", findings)
        receipt, receipt_findings = validate_receipt(
            repo_root, task_id, entry.get("receipt")
        )
        findings.extend(receipt_findings)
        if receipt and receipt.get("required") is True:
            required_receipt_count += 1
            if receipt.get("exit_code") != 0:
                findings.append(
                    Finding(
                        "error",
                        f"commands[{index}]",
                        f"required command failed with exit {receipt.get('exit_code')}",
                    )
                )
    if status == "complete" and required_receipt_count == 0:
        findings.append(
            Finding(
                "error", "commands", "complete report needs a required command receipt"
            )
        )

    skipped = _require_list(report.get("skipped_checks"), "skipped_checks", findings)
    for index, value in enumerate(skipped):
        entry = _require_mapping(value, f"skipped_checks[{index}]", findings)
        if not all(
            isinstance(entry.get(key), str) and entry[key].strip()
            for key in ("check", "reason")
        ):
            findings.append(
                Finding(
                    "error",
                    f"skipped_checks[{index}]",
                    "skipped check requires check and reason",
                )
            )
        else:
            findings.append(
                Finding(
                    "warning",
                    f"skipped_checks[{index}]",
                    f"explicitly skipped: {entry['check']} ({entry['reason']})",
                )
            )

    artifacts = _require_list(report.get("artifacts"), "artifacts", findings)
    for index, value in enumerate(artifacts):
        entry = _require_mapping(value, f"artifacts[{index}]", findings)
        try:
            if clean_head is not None:
                actual_hash = sha256_at_revision(
                    repo_root, clean_head, entry.get("path")
                )
                missing_detail = (
                    f"artifact is missing at fixed revision: {entry.get('path')}"
                )
            elif dirty is True:
                actual_hash = sha256_worktree_entry(repo_root, entry.get("path"))
                missing_detail = f"artifact is missing: {entry.get('path')}"
            else:
                actual_hash = entry.get("sha256")
                missing_detail = (
                    f"artifact revision state is invalid: {entry.get('path')}"
                )
            if actual_hash is None:
                raise ValueError(missing_detail)
            if actual_hash != entry.get("sha256"):
                raise ValueError(f"artifact hash mismatch: {entry.get('path')}")
        except ValueError as exc:
            findings.append(Finding("error", f"artifacts[{index}]", str(exc)))

    for key in (
        "touched",
        "docs_sync",
        "self_review",
    ):
        _require_mapping(report.get(key), key, findings)
    for key in (
        "changed_public_contracts",
        "diagnostics_and_previews",
        "benchmark_or_parity_evidence",
        "residual_risks",
    ):
        _require_list(report.get(key), key, findings)
    if status == "complete":
        for key in ("extension_seam", "future_change_plan"):
            value = report.get(key)
            if not isinstance(value, str) or not value.strip():
                findings.append(Finding("error", key, "must be non-empty"))
        self_review = report.get("self_review")
        if not isinstance(self_review, dict) or not all(
            self_review.get(key) is True
            for key in (
                "completed",
                "scope_matches_task",
                "layering_preserved",
                "tests_and_docs_accounted_for",
                "mechanical_and_semantic_changes_separated",
            )
        ):
            findings.append(
                Finding(
                    "error",
                    "self_review",
                    "complete report needs all self-review answers",
                )
            )

    completion_required = require_complete or status == "complete"
    if profile in PROFILE_ORDER and PROFILE_ORDER[profile] >= 2:
        findings.extend(
            validate_high_risk_records(
                repo_root,
                task_id,
                report,
                require_complete=completion_required,
            )
        )
    if profile in PROFILE_ORDER and PROFILE_ORDER[profile] >= 3:
        findings.extend(
            validate_claim_grade_records(
                repo_root,
                task_id,
                profile,
                report,
                require_complete=completion_required,
            )
        )
    return findings


def validate_high_risk_records(
    repo_root: Path,
    task_id: str,
    report: dict[str, Any],
    require_complete: bool,
) -> list[Finding]:
    findings: list[Finding] = []
    digest = report.get("source", {}).get("content_digest")
    try:
        expected_revision = report_exact_revision(report)
    except ValueError as exc:
        findings.append(Finding("error", "source", str(exc)))
        expected_revision = None
    for key in ("handoff", "review"):
        if not isinstance(report.get(key), str):
            findings.append(
                Finding("error", key, f"high-risk report requires {key} path")
            )
    if findings:
        return findings
    try:
        handoff_path = resolve_repo_path(repo_root, report["handoff"])
        handoffs = load_jsonl(handoff_path)
    except ValueError as exc:
        findings.append(Finding("error", "handoff", str(exc)))
        handoffs = []
    if require_complete and not handoffs:
        findings.append(
            Finding("error", "handoff", "complete high-risk report needs handoff")
        )
    if handoffs:
        latest = handoffs[-1]
        required = {
            "schema_version",
            "task_id",
            "driver_label",
            "revision",
            "content_digest",
            "summary",
            "changed_surface",
            "evidence",
            "assumptions",
            "failed_hypotheses",
            "known_traps",
            "untested_surfaces",
            "human_decisions",
            "disagreements",
            "next_action",
        }
        missing = sorted(required - latest.keys())
        if missing:
            findings.append(
                Finding(
                    "error", "handoff", f"latest handoff missing: {', '.join(missing)}"
                )
            )
        if latest.get("task_id") != task_id:
            findings.append(Finding("error", "handoff", "task_id mismatch"))
        if not isinstance(latest.get("revision"), str) or not REVISION_RE.fullmatch(
            latest["revision"]
        ):
            findings.append(Finding("error", "handoff", "invalid exact revision"))
        for key in (
            "changed_surface",
            "evidence",
            "assumptions",
            "failed_hypotheses",
            "known_traps",
            "untested_surfaces",
            "human_decisions",
            "disagreements",
        ):
            if not isinstance(latest.get(key), list):
                findings.append(Finding("error", "handoff", f"{key} must be a list"))
        if latest.get("content_digest") != digest:
            findings.append(
                Finding("error", "handoff", "handoff binds stale content digest")
            )
        if (
            expected_revision is not None
            and latest.get("revision") != expected_revision
        ):
            findings.append(
                Finding(
                    "error",
                    "handoff",
                    "latest handoff revision differs from report source revision",
                )
            )

    try:
        review_path = resolve_repo_path(repo_root, report["review"])
        reviews = load_jsonl(review_path)
    except ValueError as exc:
        findings.append(Finding("error", "review", str(exc)))
        reviews = []
    if require_complete and not reviews:
        findings.append(
            Finding("error", "review", "complete high-risk report needs review")
        )
    if reviews:
        for index, review in enumerate(reviews):
            verdict = review.get("verdict")
            kind = review.get("review_kind")
            if verdict not in REVIEW_VERDICTS:
                findings.append(Finding("error", f"review[{index}]", "invalid verdict"))
            if kind not in {"self", "independent"}:
                findings.append(
                    Finding("error", f"review[{index}]", "invalid review_kind")
                )
            if not isinstance(review.get("revision"), str) or not REVISION_RE.fullmatch(
                review["revision"]
            ):
                findings.append(
                    Finding("error", f"review[{index}]", "invalid exact revision")
                )
            if not isinstance(
                review.get("content_digest"), str
            ) or not SHA256_RE.fullmatch(review["content_digest"]):
                findings.append(
                    Finding("error", f"review[{index}]", "invalid content digest")
                )
            if kind == "self" and verdict == "accepted":
                findings.append(
                    Finding("error", f"review[{index}]", "self-review cannot accept")
                )
            if kind == "independent" and review.get("reviewer_label") == review.get(
                "driver_label"
            ):
                findings.append(
                    Finding(
                        "error",
                        f"review[{index}]",
                        "independent reviewer label equals driver label",
                    )
                )
            if (
                isinstance(review.get("revision_count"), int)
                and review["revision_count"] > 3
                and not review.get("escalation")
            ):
                findings.append(
                    Finding(
                        "error",
                        f"review[{index}]",
                        "unbounded revisions require escalation",
                    )
                )
        latest = reviews[-1]
        if (
            expected_revision is not None
            and latest.get("revision") != expected_revision
        ):
            findings.append(
                Finding(
                    "error",
                    "review",
                    "latest review revision differs from report source revision",
                )
            )
        if latest.get("content_digest") != digest:
            findings.append(
                Finding("error", "review", "latest review binds stale content digest")
            )
        if require_complete:
            if latest.get("verdict") != "accepted":
                findings.append(
                    Finding(
                        "error", "review", "latest terminal review must be accepted"
                    )
                )
            if latest.get("review_kind") != "independent":
                findings.append(
                    Finding(
                        "error",
                        "review",
                        "accepted terminal review must be independent",
                    )
                )
    return findings


def validate_claim_grade_records(
    repo_root: Path,
    task_id: str,
    profile: str,
    report: dict[str, Any],
    require_complete: bool,
) -> list[Finding]:
    value = report.get("experiment_root")
    canonical = f"tasks/evidence/{task_id}/experiment"
    if value != canonical:
        return [
            Finding(
                "error",
                "experiment_root",
                f"claim-grade report must use canonical experiment root {canonical}",
            )
        ]
    if not require_complete:
        return []
    try:
        root = resolve_repo_path(repo_root, value)
    except ValueError as exc:
        return [Finding("error", "experiment_root", str(exc))]
    if not root.is_dir():
        return [
            Finding(
                "error",
                "experiment_root",
                "claim-grade completion requires experiment custody records",
            )
        ]
    result = subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("experiment_custody.py")),
            "validate-completion",
            "--root",
            str(repo_root),
            "--task-id",
            task_id,
            "--profile",
            profile,
            "--experiment-root",
            value,
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode == 0:
        return []
    detail = (result.stdout.strip() or result.stderr.strip()).replace("\n", "; ")
    return [
        Finding(
            "error",
            "experiment_root",
            f"claim-grade completion custody is invalid: {detail}",
        )
    ]


def validate_repository(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    required = set(args.require_complete)
    findings: list[Finding] = []
    seen: set[str] = set()
    for task_path in find_markdown_files(repo_root / "tasks"):
        parsed = parse_task(task_path)
        if parsed.front_matter is None or parsed.task_id is None:
            continue
        try:
            _, metadata = load_task_metadata(task_path)
        except ValueError as exc:
            findings.append(Finding("error", str(task_path), str(exc)))
            continue
        if metadata.get("workflow_schema") != SCHEMA_VERSION:
            continue
        task_id = parsed.task_id
        seen.add(task_id)
        evidence = metadata.get("evidence")
        lifecycle = {part.lower() for part in task_path.parts}
        report_path = evidence_root(repo_root, task_id) / "report.yaml"
        force_complete = task_id in required or "done" in lifecycle
        if evidence == "required" and force_complete and not report_path.is_file():
            findings.append(
                Finding(
                    "error",
                    relative_path(repo_root, report_path),
                    "required completion report is missing",
                )
            )
            continue
        if report_path.is_file():
            findings.extend(
                validate_report(
                    repo_root,
                    task_path,
                    metadata,
                    report_path,
                    require_complete=force_complete,
                )
            )
    unknown = sorted(required - seen)
    for task_id in unknown:
        findings.append(Finding("error", task_id, "required task was not found"))

    errors = [finding for finding in findings if finding.severity == "error"]
    warnings = [finding for finding in findings if finding.severity == "warning"]
    if findings:
        print("Workflow evidence findings:")
        for finding in findings:
            print(
                f" [{finding.severity.upper()}] {finding.location}: {finding.message}"
            )
    print(
        f"Workflow evidence validation: {len(errors)} error(s), "
        f"{len(warnings)} warning(s)."
    )
    return 1 if errors else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate and validate IntrinsicEngine workflow evidence."
    )
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    validate = subparsers.add_parser(
        "validate", help="Validate enrolled task evidence."
    )
    validate.add_argument("--root", type=Path, default=Path("."))
    validate.add_argument(
        "--require-complete",
        action="append",
        default=[],
        metavar="TASK-ID",
        help="Require a complete report even while the task remains active.",
    )
    validate.set_defaults(handler=validate_repository)

    record = subparsers.add_parser("record-command", help="Run and receipt a command.")
    record.add_argument("--root", type=Path, default=Path("."))
    record.add_argument("--task-id", required=True)
    record.add_argument("--label", required=True)
    record.add_argument("--cwd")
    record.add_argument("--optional", action="store_true")
    record.add_argument("--shell")
    record.add_argument("command", nargs=argparse.REMAINDER)
    record.set_defaults(handler=record_command)

    generate = subparsers.add_parser("generate-report", help="Generate report.yaml.")
    generate.add_argument("--root", type=Path, default=Path("."))
    generate.add_argument("--task-id", required=True)
    generate.add_argument("--base-ref", default="origin/main")
    generate.add_argument("--complete", action="store_true")
    generate.add_argument("--replace", action="store_true")
    generate.add_argument("--claim-eligible", action="store_true")
    generate.add_argument("--layer", action="append", default=[])
    generate.add_argument("--module", action="append", default=[])
    generate.add_argument("--extension-seam")
    generate.add_argument("--future-change-plan")
    generate.add_argument("--public-contract", action="append", default=[])
    generate.add_argument("--diagnostic", action="append", default=[])
    generate.add_argument("--benchmark-evidence", action="append", default=[])
    generate.add_argument("--risk", action="append", default=[])
    generate.add_argument("--skip", action="append", default=[])
    generate.add_argument("--artifact", action="append", default=[])
    generate.add_argument("--self-review-complete", action="store_true")
    generate.add_argument("--skill-mirrors-checked", action="store_true")
    generate.set_defaults(handler=generate_report)

    handoff = subparsers.add_parser("append-handoff", help="Append durable handoff.")
    handoff.add_argument("--root", type=Path, default=Path("."))
    handoff.add_argument("--task-id", required=True)
    handoff.add_argument("--driver", required=True)
    handoff.add_argument("--revision", required=True)
    handoff.add_argument("--content-digest", required=True)
    handoff.add_argument("--summary", required=True)
    handoff.add_argument("--changed-surface", action="append", default=[])
    handoff.add_argument("--evidence", action="append", default=[])
    handoff.add_argument("--assumption", action="append", default=[])
    handoff.add_argument("--failed-hypothesis", action="append", default=[])
    handoff.add_argument("--known-trap", action="append", default=[])
    handoff.add_argument("--untested-surface", action="append", default=[])
    handoff.add_argument("--human-decision", action="append", default=[])
    handoff.add_argument("--disagreement", action="append", default=[])
    handoff.add_argument("--next-action", required=True)
    handoff.set_defaults(handler=append_handoff)

    review = subparsers.add_parser("append-review", help="Append fixed-surface review.")
    review.add_argument("--root", type=Path, default=Path("."))
    review.add_argument("--task-id", required=True)
    review.add_argument("--driver", required=True)
    review.add_argument("--reviewer", required=True)
    review.add_argument("--self-review", action="store_true")
    review.add_argument("--revision", required=True)
    review.add_argument("--content-digest", required=True)
    review.add_argument("--revision-count", type=int, default=0)
    review.add_argument("--verdict", required=True, choices=sorted(REVIEW_VERDICTS))
    review.add_argument("--finding", action="append", default=[])
    review.add_argument("--untested-surface", action="append", default=[])
    review.add_argument("--disagreement", action="append", default=[])
    review.add_argument("--escalation")
    review.set_defaults(handler=append_review)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.handler(args)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
