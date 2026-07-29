#!/usr/bin/env python3
"""Atomic task and optional path claims shared by Git worktrees."""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import sys
import time
from contextlib import contextmanager
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any, Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent))

from workflow_evidence import (  # noqa: E402
    PROFILE_ORDER,
    SCHEMA_VERSION,
    _atomic_write,
    find_task,
    git,
    load_task_metadata,
    repo_root_from,
    strict_json_load,
    utc_now,
)

TASK_ID_RE = re.compile(r"^[A-Z]+-\d+[A-Z0-9-]*$")
OWNER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.@/-]{0,127}$")
CLAIM_SCHEMA_VERSION = 1


class ClaimError(ValueError):
    pass


def _common_dir(repo_root: Path) -> Path:
    value = git(
        repo_root,
        "rev-parse",
        "--path-format=absolute",
        "--git-common-dir",
    )
    return Path(value).resolve()


def _claim_root(repo_root: Path) -> Path:
    return _common_dir(repo_root) / "intrinsic-agent-claims" / "v1"


@contextmanager
def _claim_lock(root: Path, timeout_seconds: float = 5.0) -> Iterator[None]:
    root.mkdir(parents=True, exist_ok=True)
    lock = root / ".lock"
    deadline = time.monotonic() + timeout_seconds
    while True:
        try:
            lock.mkdir()
            break
        except FileExistsError:
            try:
                age = time.time() - lock.stat().st_mtime
                if age > 30:
                    lock.rmdir()
                    continue
            except (FileNotFoundError, OSError):
                pass
            if time.monotonic() >= deadline:
                raise ClaimError("timed out waiting for the atomic claim ledger lock")
            time.sleep(0.05)
    try:
        yield
    finally:
        try:
            lock.rmdir()
        except FileNotFoundError:
            pass


def _parse_time(value: object) -> datetime | None:
    if not isinstance(value, str):
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed


def _is_stale(record: dict[str, Any], now: datetime | None = None) -> bool:
    now = now or datetime.now(UTC)
    claimed = _parse_time(record.get("claimed_at"))
    lease_seconds = record.get("lease_seconds")
    if claimed is None or type(lease_seconds) is not int or lease_seconds <= 0:
        return True
    return now >= claimed + timedelta(seconds=lease_seconds)


def _load_claims(root: Path) -> list[tuple[Path, dict[str, Any]]]:
    claims: list[tuple[Path, dict[str, Any]]] = []
    for path in sorted(root.glob("*.json")):
        claims.append((path, strict_json_load(path)))
    return claims


def _normalize_claim_path(repo_root: Path, value: str) -> str:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ClaimError(f"claim path must be repository-relative: {value}")
    normalized = Path(os.path.normpath(value))
    if str(normalized) in {"", "."}:
        raise ClaimError("claim path cannot be the repository root")
    candidate = (repo_root / normalized).resolve()
    try:
        candidate.relative_to(repo_root)
    except ValueError as exc:
        raise ClaimError(f"claim path escapes repository: {value}") from exc
    return normalized.as_posix().rstrip("/")


def _paths_overlap(left: str, right: str) -> bool:
    left_parts = Path(left).parts
    right_parts = Path(right).parts
    common = min(len(left_parts), len(right_parts))
    return left_parts[:common] == right_parts[:common]


def _validate_record(record: dict[str, Any], path: Path) -> None:
    required = {
        "schema_version",
        "task_id",
        "owner",
        "branch",
        "worktree",
        "claimed_at",
        "lease_seconds",
        "paths",
        "host",
    }
    missing = sorted(required - record.keys())
    if missing:
        raise ClaimError(f"{path}: claim missing fields: {', '.join(missing)}")
    if record.get("schema_version") != CLAIM_SCHEMA_VERSION:
        raise ClaimError(f"{path}: unsupported claim schema")
    if not isinstance(record.get("paths"), list):
        raise ClaimError(f"{path}: paths must be a list")


def _archive_claim(
    root: Path,
    path: Path,
    record: dict[str, Any],
    disposition: str,
    actor: str,
    reason: str,
) -> None:
    record = dict(record)
    record["disposition"] = disposition
    record["closed_at"] = utc_now()
    record["closed_by"] = actor
    record["close_reason"] = reason
    safe_time = record["closed_at"].replace(":", "").replace("-", "")
    history = root / "history" / record["task_id"]
    destination = history / f"{safe_time}-{disposition}.json"
    _atomic_write(
        destination,
        (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    path.unlink()


def _update_task_front_matter(
    task_path: Path,
    *,
    profile: str,
    skip_reason: str | None,
    owner: str,
    branch: str,
    worktree: str,
    claimed_at: str,
) -> None:
    content = task_path.read_text(encoding="utf-8")
    if not content.startswith("---\n"):
        raise ClaimError(f"task has no front-matter: {task_path}")
    end = content.find("\n---\n", 4)
    if end < 0:
        raise ClaimError(f"task front-matter is unterminated: {task_path}")
    front_lines = content[4:end].splitlines()
    values: dict[str, str] = {
        "workflow_schema": str(SCHEMA_VERSION),
        "workflow_profile": profile,
        "evidence": "not_applicable" if profile == "micro" else "required",
        "owner": json.dumps(owner),
        "branch": json.dumps(branch),
        "worktree": json.dumps(worktree),
        "claimed_at": json.dumps(claimed_at),
    }
    if profile == "micro":
        if not skip_reason:
            raise ClaimError("micro claim requires --evidence-skip-reason")
        values["evidence_skip_reason"] = json.dumps(skip_reason)

    key_re = re.compile(
        r"^(workflow_schema|workflow_profile|evidence|evidence_skip_reason|"
        r"owner|branch|worktree|claimed_at):"
    )
    retained = [line for line in front_lines if not key_re.match(line)]
    insertion = len(retained)
    for index, line in enumerate(retained):
        if line.startswith("depends_on:"):
            insertion = index + 1
            break
    added = [f"{key}: {value}" for key, value in values.items()]
    new_front = retained[:insertion] + added + retained[insertion:]
    new_content = "---\n" + "\n".join(new_front) + content[end:]
    _atomic_write(task_path, new_content.encode("utf-8"), replace=True)


def acquire(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    if not TASK_ID_RE.fullmatch(args.task_id):
        raise ClaimError(f"invalid task ID: {args.task_id}")
    if not OWNER_RE.fullmatch(args.owner):
        raise ClaimError(f"invalid owner label: {args.owner}")
    if args.lease_seconds <= 0 or args.lease_seconds > 7 * 24 * 60 * 60:
        raise ClaimError("lease must be between 1 second and 7 days")
    task_path = find_task(repo_root, args.task_id)
    lifecycle = {part.lower() for part in task_path.parts}
    if not ({"active", "backlog"} & lifecycle):
        raise ClaimError("only open backlog/active tasks can be claimed")
    _, task_metadata = load_task_metadata(task_path)
    existing_profile = task_metadata.get("workflow_profile")
    profile = args.profile or (
        existing_profile if existing_profile in PROFILE_ORDER else "standard"
    )
    if (
        args.profile
        and existing_profile in PROFILE_ORDER
        and args.profile != existing_profile
    ):
        raise ClaimError(
            f"claim profile {args.profile!r} would change task profile "
            f"{existing_profile!r}; edit/review the task contract separately"
        )
    branch = git(repo_root, "branch", "--show-current")
    if not branch:
        raise ClaimError("task claims require a named branch")
    worktree = str(repo_root)
    paths = sorted({_normalize_claim_path(repo_root, value) for value in args.path})
    claimed_at = utc_now()
    record = {
        "schema_version": CLAIM_SCHEMA_VERSION,
        "task_id": args.task_id,
        "owner": args.owner,
        "branch": branch,
        "worktree": worktree,
        "claimed_at": claimed_at,
        "lease_seconds": args.lease_seconds,
        "paths": paths,
        "host": socket.gethostname(),
    }
    root = _claim_root(repo_root)
    claim_path = root / f"{args.task_id}.json"
    with _claim_lock(root):
        live_claims: list[tuple[Path, dict[str, Any]]] = []
        stale_claims: list[tuple[Path, dict[str, Any]]] = []
        for existing_path, existing in _load_claims(root):
            _validate_record(existing, existing_path)
            if _is_stale(existing):
                stale_claims.append((existing_path, existing))
            else:
                live_claims.append((existing_path, existing))
        relevant_stale = [
            existing
            for _, existing in stale_claims
            if existing["task_id"] == args.task_id
            or existing["worktree"] == worktree
            or any(
                _paths_overlap(left, right)
                for left in paths
                for right in existing.get("paths", [])
            )
        ]
        if relevant_stale:
            listing = ", ".join(record["task_id"] for record in relevant_stale)
            raise ClaimError(
                f"overlapping stale/abandoned claims require explicit recover first: "
                f"{listing}"
            )
        for _, existing in live_claims:
            if existing["task_id"] == args.task_id:
                raise ClaimError(
                    f"task is already claimed by {existing['owner']} in "
                    f"{existing['worktree']}"
                )
            if existing["worktree"] == worktree:
                raise ClaimError(
                    "one-writer policy rejects another live claim in the same worktree: "
                    f"{existing['task_id']}"
                )
            for left in paths:
                for right in existing.get("paths", []):
                    if _paths_overlap(left, right):
                        raise ClaimError(
                            f"path overlap with {existing['task_id']}: {left} vs {right}"
                        )
        _atomic_write(
            claim_path,
            (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
        try:
            _update_task_front_matter(
                task_path,
                profile=profile,
                skip_reason=args.evidence_skip_reason,
                owner=args.owner,
                branch=branch,
                worktree=worktree,
                claimed_at=claimed_at,
            )
        except Exception:
            claim_path.unlink(missing_ok=True)
            raise
    print(
        f"Claimed {args.task_id} for {args.owner} on {branch}; "
        f"paths={paths or ['task-only']}."
    )
    return 0


def release(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    root = _claim_root(repo_root)
    path = root / f"{args.task_id}.json"
    with _claim_lock(root):
        if not path.is_file():
            raise ClaimError(f"task has no live claim: {args.task_id}")
        record = strict_json_load(path)
        _validate_record(record, path)
        if record.get("owner") != args.owner:
            raise ClaimError(
                f"wrong owner: claim belongs to {record.get('owner')}, not {args.owner}"
            )
        _archive_claim(root, path, record, "released", args.owner, args.reason)
    print(f"Released {args.task_id}; history retained in the Git common directory.")
    return 0


def recover(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    root = _claim_root(repo_root)
    path = root / f"{args.task_id}.json"
    with _claim_lock(root):
        if not path.is_file():
            raise ClaimError(f"task has no live claim: {args.task_id}")
        record = strict_json_load(path)
        _validate_record(record, path)
        if not _is_stale(record):
            raise ClaimError(
                "live claim cannot be recovered; ask its owner to release it"
            )
        _archive_claim(root, path, record, "recovered", args.actor, args.reason)
    print(f"Recovered stale claim {args.task_id}; abandoned history retained.")
    return 0


def status(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    root = _claim_root(repo_root)
    if not root.exists():
        print("No task claims.")
        return 0
    claims = _load_claims(root)
    if not claims:
        print("No task claims.")
        return 0
    stale = False
    for path, record in claims:
        _validate_record(record, path)
        state = "STALE" if _is_stale(record) else "LIVE"
        stale = stale or state == "STALE"
        print(
            f"{state} {record['task_id']} owner={record['owner']} "
            f"branch={record['branch']} worktree={record['worktree']} "
            f"paths={record['paths'] or ['task-only']}"
        )
    return 1 if stale and args.fail_on_stale else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command_name", required=True)

    claim = subs.add_parser("acquire")
    claim.add_argument("--root", type=Path, default=Path("."))
    claim.add_argument("--task-id", required=True)
    claim.add_argument("--owner", required=True)
    claim.add_argument("--profile", choices=sorted(PROFILE_ORDER))
    claim.add_argument("--evidence-skip-reason")
    claim.add_argument("--path", action="append", default=[])
    claim.add_argument("--lease-seconds", type=int, default=24 * 60 * 60)
    claim.set_defaults(handler=acquire)

    drop = subs.add_parser("release")
    drop.add_argument("--root", type=Path, default=Path("."))
    drop.add_argument("--task-id", required=True)
    drop.add_argument("--owner", required=True)
    drop.add_argument("--reason", required=True)
    drop.set_defaults(handler=release)

    recovery = subs.add_parser("recover")
    recovery.add_argument("--root", type=Path, default=Path("."))
    recovery.add_argument("--task-id", required=True)
    recovery.add_argument("--actor", required=True)
    recovery.add_argument("--reason", required=True)
    recovery.set_defaults(handler=recover)

    show = subs.add_parser("status")
    show.add_argument("--root", type=Path, default=Path("."))
    show.add_argument("--fail-on-stale", action="store_true")
    show.set_defaults(handler=status)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return args.handler(args)
    except (ClaimError, ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
