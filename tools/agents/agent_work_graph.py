#!/usr/bin/env python3
"""Manage a bounded, repository-native work graph for one claimed task.

The checked-in recipe is topology.  The Git-common-dir JSON state and JSONL
events are live observability only: they do not replace task ownership,
verification receipts, independent review, or experiment custody.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import socket
import sys
import time
import uuid
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent))

import task_claim  # noqa: E402
from workflow_evidence import (  # noqa: E402
    PROFILE_ORDER,
    _append_jsonl,
    _atomic_write,
    changed_paths,
    find_task,
    git,
    load_jsonl,
    load_task_metadata,
    relative_path,
    repo_root_from,
    resolve_base_revision,
    resolve_repo_path,
    sha256_file,
    sha256_worktree_artifact,
    strict_json_load,
    surface_digest,
    surface_entries,
    utc_now,
)

RECIPE_SCHEMA_VERSION = 1
RUN_SCHEMA_VERSION = 1
GRAPH_ID_RE = re.compile(r"^[a-z][a-z0-9_.-]{2,127}$")
NODE_ID_RE = re.compile(r"^[a-z][a-z0-9_-]{0,63}$")
NOTE_KINDS = {"decision", "finding", "idea", "constraint"}
NODE_KINDS = {"agent", "deterministic", "gate", "join"}
PERMISSIONS = {"read", "write"}
FINISH_OUTCOMES = {"blocked", "failed", "succeeded"}
TERMINAL_NODE_STATUSES = {"blocked", "failed", "profile_skipped", "succeeded"}
RECIPE_FIELDS = {"schema_version", "graph_id", "title", "description", "nodes"}
NODE_FIELDS = {
    "id",
    "title",
    "kind",
    "permission",
    "requires",
    "minimum_profile",
    "max_attempts",
    "independent",
    "binds_surface",
}


class WorkGraphError(ValueError):
    pass


def _validate_task_id(task_id: str) -> None:
    if not task_claim.TASK_ID_RE.fullmatch(task_id):
        raise WorkGraphError(f"invalid task ID: {task_id}")


def _claim_identity(claim: dict[str, Any]) -> dict[str, str]:
    canonical = json.dumps(
        claim, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return {
        "task_id": claim["task_id"],
        "owner": claim["owner"],
        "branch": claim["branch"],
        "worktree": claim["worktree"],
        "claimed_at": claim["claimed_at"],
        "record_sha256": hashlib.sha256(canonical).hexdigest(),
    }


def _require_exact_fields(
    value: dict[str, Any], required: set[str], location: str
) -> None:
    missing = sorted(required - value.keys())
    unknown = sorted(value.keys() - required)
    if missing:
        raise WorkGraphError(f"{location}: missing fields: {', '.join(missing)}")
    if unknown:
        raise WorkGraphError(f"{location}: unknown fields: {', '.join(unknown)}")


def _node_map(recipe: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {node["id"]: node for node in recipe["nodes"]}


def _topological_order(recipe: dict[str, Any]) -> list[str]:
    nodes = _node_map(recipe)
    indegree = {node_id: len(node["requires"]) for node_id, node in nodes.items()}
    dependents: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    for node_id, node in nodes.items():
        for requirement in node["requires"]:
            dependents[requirement].append(node_id)
    ready = sorted(node_id for node_id, degree in indegree.items() if degree == 0)
    ordered: list[str] = []
    while ready:
        node_id = ready.pop(0)
        ordered.append(node_id)
        for dependent in sorted(dependents[node_id]):
            indegree[dependent] -= 1
            if indegree[dependent] == 0:
                ready.append(dependent)
                ready.sort()
    if len(ordered) != len(nodes):
        raise WorkGraphError("recipe nodes must form an acyclic graph")
    return ordered


def _descendants(recipe: dict[str, Any], node_id: str) -> set[str]:
    dependents: dict[str, list[str]] = {node["id"]: [] for node in recipe["nodes"]}
    for node in recipe["nodes"]:
        for requirement in node["requires"]:
            dependents[requirement].append(node["id"])
    result: set[str] = set()
    pending = list(dependents[node_id])
    while pending:
        candidate = pending.pop()
        if candidate in result:
            continue
        result.add(candidate)
        pending.extend(dependents[candidate])
    return result


def validate_recipe_data(recipe: dict[str, Any], location: str) -> dict[str, Any]:
    _require_exact_fields(recipe, RECIPE_FIELDS, location)
    if recipe.get("schema_version") != RECIPE_SCHEMA_VERSION:
        raise WorkGraphError(
            f"{location}: schema_version must equal {RECIPE_SCHEMA_VERSION}"
        )
    graph_id = recipe.get("graph_id")
    if not isinstance(graph_id, str) or not GRAPH_ID_RE.fullmatch(graph_id):
        raise WorkGraphError(f"{location}: invalid graph_id")
    for field in ("title", "description"):
        if not isinstance(recipe.get(field), str) or not recipe[field].strip():
            raise WorkGraphError(f"{location}: {field} must be non-empty text")
    nodes = recipe.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        raise WorkGraphError(f"{location}: nodes must be a non-empty list")

    seen: set[str] = set()
    normalized_nodes: list[dict[str, Any]] = []
    for index, raw in enumerate(nodes):
        node_location = f"{location}:nodes[{index}]"
        if not isinstance(raw, dict):
            raise WorkGraphError(f"{node_location}: must be an object")
        _require_exact_fields(raw, NODE_FIELDS, node_location)
        node_id = raw.get("id")
        if not isinstance(node_id, str) or not NODE_ID_RE.fullmatch(node_id):
            raise WorkGraphError(f"{node_location}: invalid node id")
        if node_id in seen:
            raise WorkGraphError(f"{node_location}: duplicate node id {node_id!r}")
        seen.add(node_id)
        if not isinstance(raw.get("title"), str) or not raw["title"].strip():
            raise WorkGraphError(f"{node_location}: title must be non-empty text")
        if raw.get("kind") not in NODE_KINDS:
            raise WorkGraphError(f"{node_location}: invalid kind")
        if raw.get("permission") not in PERMISSIONS:
            raise WorkGraphError(f"{node_location}: invalid permission")
        if raw.get("minimum_profile") not in PROFILE_ORDER:
            raise WorkGraphError(f"{node_location}: invalid minimum_profile")
        if (
            type(raw.get("max_attempts")) is not int
            or not 1 <= raw["max_attempts"] <= 10
        ):
            raise WorkGraphError(
                f"{node_location}: max_attempts must be an integer from 1 to 10"
            )
        for field in ("independent", "binds_surface"):
            if type(raw.get(field)) is not bool:
                raise WorkGraphError(f"{node_location}: {field} must be boolean")
        requirements = raw.get("requires")
        if not isinstance(requirements, list) or any(
            not isinstance(value, str) for value in requirements
        ):
            raise WorkGraphError(f"{node_location}: requires must be a string list")
        if len(requirements) != len(set(requirements)):
            raise WorkGraphError(f"{node_location}: duplicate dependency")
        if node_id in requirements:
            raise WorkGraphError(f"{node_location}: node cannot require itself")
        if raw["permission"] == "write" and raw["kind"] != "agent":
            raise WorkGraphError(
                f"{node_location}: only agent nodes may be write-capable"
            )
        if raw["permission"] == "write" and raw["independent"]:
            raise WorkGraphError(
                f"{node_location}: a write-capable node cannot be independent"
            )
        if raw["kind"] == "gate" and not raw["independent"]:
            raise WorkGraphError(
                f"{node_location}: gate nodes must require an independent actor"
            )
        if raw["independent"] and raw["permission"] != "read":
            raise WorkGraphError(
                f"{node_location}: independent nodes must be read-only"
            )
        if raw["binds_surface"] and raw["permission"] != "read":
            raise WorkGraphError(
                f"{node_location}: final surface binding must be read-only"
            )
        normalized_nodes.append(dict(raw))

    recipe = dict(recipe)
    recipe["nodes"] = normalized_nodes
    node_ids = set(seen)
    for node in normalized_nodes:
        unknown = sorted(set(node["requires"]) - node_ids)
        if unknown:
            raise WorkGraphError(
                f"{location}:{node['id']}: unknown dependencies: {', '.join(unknown)}"
            )
    _topological_order(recipe)

    final_nodes = [node for node in normalized_nodes if node["binds_surface"]]
    if len(final_nodes) != 1:
        raise WorkGraphError(
            "recipe must contain exactly one final surface-binding node"
        )
    final_id = final_nodes[0]["id"]
    if _descendants(recipe, final_id):
        raise WorkGraphError("the final surface-binding node must be a graph leaf")
    for node in normalized_nodes:
        if node["id"] != final_id and final_id not in _descendants(recipe, node["id"]):
            raise WorkGraphError(
                f"recipe node {node['id']!r} does not lead to final node {final_id!r}"
            )

    writers = [node["id"] for node in normalized_nodes if node["permission"] == "write"]
    if not writers:
        raise WorkGraphError("recipe must contain at least one write-capable node")
    standard_active = {
        node["id"]
        for node in normalized_nodes
        if _profile_active("standard", node["minimum_profile"])
    }
    if final_id not in standard_active:
        raise WorkGraphError(
            "the final surface-binding node must be active for the standard profile"
        )
    if not any(writer in standard_active for writer in writers):
        raise WorkGraphError(
            "recipe must contain a write-capable node active for the standard profile"
        )
    for index, left in enumerate(writers):
        left_descendants = _descendants(recipe, left)
        for right in writers[index + 1 :]:
            if right not in left_descendants and left not in _descendants(
                recipe, right
            ):
                raise WorkGraphError(
                    "write-capable nodes must be dependency-ordered, not parallel: "
                    f"{left}, {right}"
                )
    return recipe


def load_recipe(path: Path) -> dict[str, Any]:
    return validate_recipe_data(strict_json_load(path), str(path))


def _work_graph_root(repo_root: Path) -> Path:
    return task_claim._common_dir(repo_root) / "intrinsic-agent-work-graphs" / "v1"


def _state_path(repo_root: Path, task_id: str) -> Path:
    _validate_task_id(task_id)
    return _work_graph_root(repo_root) / f"{task_id}.json"


def _events_path(repo_root: Path, task_id: str) -> Path:
    _validate_task_id(task_id)
    return _work_graph_root(repo_root) / f"{task_id}.events.jsonl"


# A lock whose holder record cannot be read is only broken after this long, to
# cover the instant between mkdir and publishing the record.
LOCK_UNIDENTIFIED_GRACE_SECONDS = 30.0
# A holder on another host cannot be liveness-checked, so it is only broken
# after a window far longer than any legitimate critical section.
LOCK_FOREIGN_HOST_STALE_SECONDS = 900.0


def _process_is_running(pid: object) -> bool:
    """Conservative liveness check: unknown means alive, so we never steal."""
    if not isinstance(pid, int) or pid <= 0:
        return True
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except OSError:
        return True
    return True


def _lock_holder(owner_path: Path) -> dict[str, Any] | None:
    try:
        record = json.loads(owner_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return record if isinstance(record, dict) else None


def _lock_is_breakable(lock: Path, owner_path: Path) -> bool:
    """True only when the recorded holder is provably gone or genuinely stale.

    Elapsed time alone is never sufficient: the previous implementation compared
    a mkdir mtime that was never refreshed, so any writer slower than the
    threshold had its lock taken while it was still working.
    """
    holder = _lock_holder(owner_path)
    if holder is None:
        # Either the holder has not published yet, or the record is damaged.
        try:
            age = time.time() - lock.stat().st_mtime
        except OSError:
            return False
        return age > LOCK_UNIDENTIFIED_GRACE_SECONDS
    if holder.get("host") == socket.gethostname():
        return not _process_is_running(holder.get("pid"))
    acquired_at = holder.get("acquired_at")
    if not isinstance(acquired_at, (int, float)):
        return False
    return (time.time() - float(acquired_at)) > LOCK_FOREIGN_HOST_STALE_SECONDS


def _break_lock(lock: Path, owner_path: Path) -> None:
    try:
        owner_path.unlink()
    except OSError:
        pass
    try:
        lock.rmdir()
    except OSError:
        pass


@contextmanager
def _state_lock(root: Path, timeout_seconds: float = 5.0) -> Iterator[None]:
    root.mkdir(parents=True, exist_ok=True)
    lock = root / ".lock"
    owner_path = lock / "owner.json"
    token = uuid.uuid4().hex
    deadline = time.monotonic() + timeout_seconds
    while True:
        try:
            lock.mkdir()
        except FileExistsError:
            if _lock_is_breakable(lock, owner_path):
                _break_lock(lock, owner_path)
                continue
            if time.monotonic() >= deadline:
                raise WorkGraphError("timed out waiting for the work-graph lock")
            time.sleep(0.05)
            continue
        _atomic_write(
            owner_path,
            json.dumps(
                {
                    "token": token,
                    "pid": os.getpid(),
                    "host": socket.gethostname(),
                    "acquired_at": time.time(),
                },
                sort_keys=True,
            ).encode("utf-8"),
        )
        break

    def release(quiet: bool) -> None:
        holder = _lock_holder(owner_path)
        if holder is not None and holder.get("token") != token:
            # Our lock was broken and someone else holds it now. Removing it
            # would steal theirs and cascade, so surface the anomaly instead.
            if quiet:
                return
            raise WorkGraphError(
                "the work-graph lock was taken over by another holder while "
                "this operation was running; re-run and inspect the run state"
            )
        _break_lock(lock, owner_path)

    try:
        yield
    except BaseException:
        release(quiet=True)
        raise
    release(quiet=False)


def _load_live_claim(repo_root: Path, task_id: str) -> dict[str, Any]:
    _validate_task_id(task_id)
    path = task_claim._claim_root(repo_root) / f"{task_id}.json"
    if not path.is_file():
        raise WorkGraphError(f"{task_id} has no live task claim")
    claim = strict_json_load(path)
    task_claim._validate_record(claim, path)
    if task_claim._is_stale(claim):
        raise WorkGraphError(
            f"{task_id} task claim is stale; recover or renew it first"
        )
    if Path(claim["worktree"]).resolve() != repo_root.resolve():
        raise WorkGraphError(
            f"{task_id} is claimed in another worktree: {claim['worktree']}"
        )
    branch = git(repo_root, "branch", "--show-current")
    if claim["branch"] != branch:
        raise WorkGraphError(
            f"{task_id} claim branch {claim['branch']!r} does not match {branch!r}"
        )
    return claim


def _task_claim_metadata_matches(
    metadata: dict[str, Any], claim: dict[str, Any]
) -> bool:
    return all(
        metadata.get(field) == claim[field]
        for field in ("owner", "branch", "worktree", "claimed_at")
    )


def _legacy_claim_can_bind(
    state: dict[str, Any], claim: dict[str, Any], metadata: dict[str, Any]
) -> bool:
    claimed_at = task_claim._parse_time(claim.get("claimed_at"))
    created_at = task_claim._parse_time(state.get("created_at"))
    return (
        claimed_at is not None
        and created_at is not None
        and claimed_at <= created_at
        and claim["owner"] == state["owner"]
        and claim["branch"] == state["branch"]
        and claim["worktree"] == state["worktree"]
        and _task_claim_metadata_matches(metadata, claim)
    )


def _claim_binding_reasons(
    state: dict[str, Any], claim: dict[str, Any], metadata: dict[str, Any]
) -> list[str]:
    reasons: list[str] = []
    if claim["owner"] != state["owner"]:
        reasons.append("task claim owner changed")
    if claim["branch"] != state["branch"]:
        reasons.append("task claim branch changed")
    if claim["worktree"] != state["worktree"]:
        reasons.append("task claim worktree changed")
    if not _task_claim_metadata_matches(metadata, claim):
        reasons.append("task front-matter claim binding changed")

    binding = state["source"].get("claim")
    if binding is None:
        if not _legacy_claim_can_bind(state, claim, metadata):
            reasons.append(
                "work graph lacks the live claim generation; explicit resume required"
            )
    elif binding != _claim_identity(claim):
        reasons.append("task claim generation changed")
    return reasons


def _source_snapshot(
    repo_root: Path, base_revision: str, task_id: str
) -> tuple[list[dict[str, Any]], str]:
    paths = changed_paths(repo_root, base_revision, task_id)
    entries = surface_entries(repo_root, paths)
    return entries, surface_digest(entries)


def _profile_active(task_profile: str, minimum_profile: str) -> bool:
    return PROFILE_ORDER[task_profile] >= PROFILE_ORDER[minimum_profile]


def _dependencies_succeeded(
    state: dict[str, Any], recipe_nodes: dict[str, dict[str, Any]], node_id: str
) -> bool:
    def satisfied(candidate: str) -> bool:
        status = state["nodes"][candidate]["status"]
        if status == "succeeded":
            return True
        if status != "profile_skipped":
            return False
        return all(
            satisfied(requirement)
            for requirement in recipe_nodes[candidate]["requires"]
        )

    return all(
        satisfied(requirement) for requirement in recipe_nodes[node_id]["requires"]
    )


def _refresh_ready(state: dict[str, Any], recipe: dict[str, Any]) -> None:
    nodes = _node_map(recipe)
    for node_id in _topological_order(recipe):
        current = state["nodes"][node_id]
        if current["status"] != "pending":
            continue
        if _dependencies_succeeded(state, nodes, node_id):
            current["status"] = "ready"


def _derive_status(state: dict[str, Any], stale_reasons: list[str]) -> str:
    if state["status"] == "aborted":
        return "aborted"
    if stale_reasons:
        return "stale"
    active = [
        node for node in state["nodes"].values() if node["status"] != "profile_skipped"
    ]
    if active and all(node["status"] == "succeeded" for node in active):
        return "succeeded"
    if any(node["status"] in {"blocked", "failed"} for node in active):
        return "blocked"
    return "active"


def _load_state(repo_root: Path, task_id: str) -> dict[str, Any]:
    path = _state_path(repo_root, task_id)
    if not path.is_file():
        raise WorkGraphError(f"{task_id} has no work-graph run")
    state = strict_json_load(path)
    required = {
        "schema_version",
        "run_id",
        "task_id",
        "task_profile",
        "owner",
        "branch",
        "worktree",
        "created_at",
        "updated_at",
        "status",
        "recipe",
        "source",
        "nodes",
        "event_count",
        "abort_reason",
    }
    _require_exact_fields(state, required, str(path))
    if state["schema_version"] != RUN_SCHEMA_VERSION:
        raise WorkGraphError(f"{path}: unsupported run schema")
    if state["task_id"] != task_id:
        raise WorkGraphError(f"{path}: task identity mismatch")
    events = load_jsonl(_events_path(repo_root, task_id))
    if len(events) != state["event_count"]:
        raise WorkGraphError(
            f"{task_id} state/event projection mismatch: "
            f"state={state['event_count']} events={len(events)}"
        )
    if any(event.get("run_id") != state["run_id"] for event in events):
        raise WorkGraphError(f"{task_id} event log contains another run identity")
    return state


def _recipe_for_state(
    repo_root: Path, state: dict[str, Any]
) -> tuple[dict[str, Any], list[str]]:
    reasons: list[str] = []
    try:
        path = resolve_repo_path(repo_root, state["recipe"].get("path"))
        recipe = load_recipe(path)
        observed = sha256_file(path)
        if observed != state["recipe"].get("sha256"):
            reasons.append("checked-in recipe digest changed")
        if recipe["graph_id"] != state["recipe"].get("graph_id"):
            reasons.append("checked-in recipe graph_id changed")
    except (ValueError, OSError) as exc:
        raise WorkGraphError(f"cannot load run recipe: {exc}") from exc
    return recipe, reasons


def _stale_reasons(
    repo_root: Path, state: dict[str, Any], recipe_reasons: list[str]
) -> tuple[list[str], str]:
    reasons = list(recipe_reasons)
    task_path = find_task(repo_root, state["task_id"])
    _, metadata = load_task_metadata(task_path)
    if state["status"] not in {"succeeded", "aborted"}:
        try:
            claim = _load_live_claim(repo_root, state["task_id"])
            reasons.extend(_claim_binding_reasons(state, claim, metadata))
        except (ValueError, OSError) as exc:
            reasons.append(str(exc))
    if metadata.get("workflow_profile") != state["task_profile"]:
        reasons.append("task workflow profile changed")
    _, current_digest = _source_snapshot(
        repo_root, state["source"]["base_revision"], state["task_id"]
    )
    final_binding = state["source"].get("final_content_digest")
    if final_binding is not None and final_binding != current_digest:
        reasons.append("final source binding is stale")
    review_binding = state["source"].get("review_content_digest")
    if review_binding is not None and review_binding != current_digest:
        reasons.append("review source binding is stale")
    return sorted(set(reasons)), current_digest


def _write_state(repo_root: Path, state: dict[str, Any]) -> None:
    state["updated_at"] = utc_now()
    _atomic_write(
        _state_path(repo_root, state["task_id"]),
        (json.dumps(state, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        replace=True,
    )


def _commit_event(
    repo_root: Path, state: dict[str, Any], event_type: str, **payload: Any
) -> None:
    event = {
        "schema_version": RUN_SCHEMA_VERSION,
        "run_id": state["run_id"],
        "task_id": state["task_id"],
        "recorded_at": utc_now(),
        "event": event_type,
        **payload,
    }
    _append_jsonl(_events_path(repo_root, state["task_id"]), event)
    state["event_count"] += 1
    _write_state(repo_root, state)


def _actor_allowed(node: dict[str, Any], actor: str, state: dict[str, Any]) -> None:
    if not task_claim.OWNER_RE.fullmatch(actor):
        raise WorkGraphError(f"invalid actor label: {actor}")
    if node["permission"] == "write" and actor != state["owner"]:
        raise WorkGraphError(
            f"write node {node['id']} belongs to claimed owner {state['owner']}"
        )
    if node["independent"] and actor == state["owner"]:
        raise WorkGraphError(
            f"independent node {node['id']} cannot be performed by task owner"
        )


def _transition_context(
    repo_root: Path, task_id: str
) -> tuple[dict[str, Any], dict[str, Any], dict[str, dict[str, Any]]]:
    state = _load_state(repo_root, task_id)
    recipe, recipe_reasons = _recipe_for_state(repo_root, state)
    stale, _ = _stale_reasons(repo_root, state, recipe_reasons)
    if stale:
        raise WorkGraphError("work graph is stale: " + "; ".join(stale))
    if state["status"] == "aborted":
        raise WorkGraphError("work graph is aborted")
    if _derive_status(state, []) == "succeeded":
        raise WorkGraphError("work graph is already complete")
    if state["source"].get("claim") is None:
        state["source"]["claim"] = _claim_identity(
            _load_live_claim(repo_root, state["task_id"])
        )
    _refresh_ready(state, recipe)
    return state, recipe, _node_map(recipe)


def validate_recipe_command(args: argparse.Namespace) -> int:
    recipe = load_recipe(args.recipe.resolve())
    print(
        f"Valid work-graph recipe {recipe['graph_id']}: {len(recipe['nodes'])} nodes."
    )
    return 0


def start(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    task_path = find_task(repo_root, args.task_id)
    _, metadata = load_task_metadata(task_path)
    profile = metadata.get("workflow_profile")
    if profile not in PROFILE_ORDER or profile == "micro":
        raise WorkGraphError("work graphs require an enrolled non-micro task")
    claim = _load_live_claim(repo_root, args.task_id)
    if args.owner != claim["owner"]:
        raise WorkGraphError(
            f"owner {args.owner!r} does not match live claim {claim['owner']!r}"
        )
    recipe_path = args.recipe.resolve()
    try:
        recipe_relative = relative_path(repo_root, recipe_path)
    except ValueError as exc:
        raise WorkGraphError("recipe must be inside the repository") from exc
    recipe = load_recipe(recipe_path)
    active_nodes = [
        node
        for node in recipe["nodes"]
        if _profile_active(profile, node["minimum_profile"])
    ]
    if not any(node["permission"] == "write" for node in active_nodes):
        raise WorkGraphError(
            f"recipe has no write-capable node active for profile {profile}"
        )
    if not any(node["binds_surface"] for node in active_nodes):
        raise WorkGraphError(
            f"recipe has no final surface-binding node active for profile {profile}"
        )
    base_revision = resolve_base_revision(repo_root, args.base_ref)
    entries, digest = _source_snapshot(repo_root, base_revision, args.task_id)
    now = utc_now()
    nodes: dict[str, dict[str, Any]] = {}
    for node in recipe["nodes"]:
        active = _profile_active(profile, node["minimum_profile"])
        nodes[node["id"]] = {
            "status": "pending" if active else "profile_skipped",
            "attempts": 0,
            "actor": None,
            "started_at": None,
            "finished_at": None,
            "outcome_note": None,
            "artifacts": [],
            "surface_digest": None,
            "notes_count": 0,
        }
    state = {
        "schema_version": RUN_SCHEMA_VERSION,
        "run_id": f"{args.task_id.lower()}-{now.replace(':', '').replace('-', '')}",
        "task_id": args.task_id,
        "task_profile": profile,
        "owner": claim["owner"],
        "branch": claim["branch"],
        "worktree": claim["worktree"],
        "created_at": now,
        "updated_at": now,
        "status": "active",
        "recipe": {
            "path": recipe_relative,
            "graph_id": recipe["graph_id"],
            "sha256": sha256_file(recipe_path),
        },
        "source": {
            "base_ref": args.base_ref,
            "base_revision": base_revision,
            "head_revision_at_start": git(repo_root, "rev-parse", "HEAD"),
            "content_digest_at_start": digest,
            "surface_at_start": entries,
            "claim": _claim_identity(claim),
            "review_content_digest": None,
            "review_bound_by": None,
            "final_content_digest": None,
        },
        "nodes": nodes,
        "event_count": 0,
        "abort_reason": None,
    }
    _refresh_ready(state, recipe)
    root = _work_graph_root(repo_root)
    with _state_lock(root):
        if (
            _state_path(repo_root, args.task_id).exists()
            or _events_path(repo_root, args.task_id).exists()
        ):
            raise WorkGraphError(
                f"{args.task_id} already has work-graph state; one run per task is allowed"
            )
        _commit_event(
            repo_root,
            state,
            "run_started",
            actor=args.owner,
            recipe_sha256=state["recipe"]["sha256"],
            source_digest=digest,
        )
    ready = [node_id for node_id, value in nodes.items() if value["status"] == "ready"]
    print(f"Started {args.task_id} work graph; ready={ready}.")
    return 0


def resume(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        state = _load_state(repo_root, args.task_id)
        if state["status"] in {"succeeded", "aborted"}:
            raise WorkGraphError(f"{state['status']} work graphs cannot be resumed")
        recipe, recipe_reasons = _recipe_for_state(repo_root, state)
        if recipe_reasons:
            raise WorkGraphError(
                "work graph recipe changed: " + "; ".join(recipe_reasons)
            )
        task_path = find_task(repo_root, state["task_id"])
        _, metadata = load_task_metadata(task_path)
        if metadata.get("workflow_profile") != state["task_profile"]:
            raise WorkGraphError("task workflow profile changed")
        claim = _load_live_claim(repo_root, state["task_id"])
        if args.owner != claim["owner"]:
            raise WorkGraphError(
                f"owner {args.owner!r} does not match live claim {claim['owner']!r}"
            )
        if not _task_claim_metadata_matches(metadata, claim):
            raise WorkGraphError("task front matter does not match the live claim")

        nodes = _node_map(recipe)
        running = {
            node_id
            for node_id, node_state in state["nodes"].items()
            if node_state["status"] == "running"
        }
        exhausted = {
            node_id
            for node_id in running
            if state["nodes"][node_id]["attempts"] >= nodes[node_id]["max_attempts"]
        }
        invalidated = set(running)
        for node_id in running:
            invalidated.update(_descendants(recipe, node_id))
        for node_id in invalidated:
            definition = nodes[node_id]
            node_state = state["nodes"][node_id]
            if node_id in exhausted:
                node_state.update(
                    {
                        "status": "failed",
                        "finished_at": utc_now(),
                        "outcome_note": (
                            "abandoned during claim handoff with attempt budget exhausted"
                        ),
                        "artifacts": [],
                        "surface_digest": None,
                    }
                )
                continue
            node_state["status"] = (
                "pending"
                if _profile_active(state["task_profile"], definition["minimum_profile"])
                else "profile_skipped"
            )
            node_state.update(
                {
                    "actor": None,
                    "started_at": None,
                    "finished_at": None,
                    "outcome_note": None,
                    "artifacts": [],
                    "surface_digest": None,
                }
            )

        previous = {
            "owner": state["owner"],
            "branch": state["branch"],
            "worktree": state["worktree"],
        }
        state.update(
            {
                "owner": claim["owner"],
                "branch": claim["branch"],
                "worktree": claim["worktree"],
            }
        )
        state["source"]["final_content_digest"] = None
        state["source"]["claim"] = _claim_identity(claim)
        _refresh_ready(state, recipe)
        state["status"] = _derive_status(state, [])
        _commit_event(
            repo_root,
            state,
            "run_resumed",
            actor=args.owner,
            reason=args.reason,
            previous=previous,
            invalidated=sorted(invalidated),
            exhausted=sorted(exhausted),
        )
    print(
        f"Resumed {args.task_id} for {args.owner}; invalidated={sorted(invalidated)}; "
        f"exhausted={sorted(exhausted)}."
    )
    return 0


def _status_payload(repo_root: Path, task_id: str) -> dict[str, Any]:
    state = _load_state(repo_root, task_id)
    recipe, recipe_reasons = _recipe_for_state(repo_root, state)
    _refresh_ready(state, recipe)
    stale, current_digest = _stale_reasons(repo_root, state, recipe_reasons)
    return {
        **state,
        "derived": {
            "status": _derive_status(state, stale),
            "stale_reasons": stale,
            "current_content_digest": current_digest,
            "ready_nodes": sorted(
                node_id
                for node_id, node in state["nodes"].items()
                if node["status"] == "ready"
            ),
            "running_nodes": sorted(
                node_id
                for node_id, node in state["nodes"].items()
                if node["status"] == "running"
            ),
        },
    }


def show(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        payload = _status_payload(repo_root, args.task_id)
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        derived = payload["derived"]
        print(
            f"{payload['task_id']} run={payload['run_id']} "
            f"profile={payload['task_profile']} status={derived['status']}"
        )
        for node_id, node in payload["nodes"].items():
            actor = node["actor"] or "-"
            print(
                f"  {node_id:24} {node['status']:15} "
                f"attempts={node['attempts']} actor={actor} notes={node['notes_count']}"
            )
        if derived["stale_reasons"]:
            for reason in derived["stale_reasons"]:
                print(f"  STALE: {reason}")
    return 1 if payload["derived"]["status"] == "stale" else 0


def list_runs(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    root = _work_graph_root(repo_root)
    with _state_lock(root):
        task_ids = sorted(path.stem for path in root.glob("*.json"))
        payloads = [_status_payload(repo_root, task_id) for task_id in task_ids]
    if args.json:
        print(json.dumps(payloads, indent=2, sort_keys=True))
    elif not payloads:
        print("No agent work-graph runs.")
    else:
        for payload in payloads:
            print(
                f"{payload['task_id']} owner={payload['owner']} "
                f"status={payload['derived']['status']} "
                f"ready={payload['derived']['ready_nodes']}"
            )
    return 1 if any(p["derived"]["status"] == "stale" for p in payloads) else 0


def begin(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        state, recipe, nodes = _transition_context(repo_root, args.task_id)
        if args.node not in nodes:
            raise WorkGraphError(f"unknown node: {args.node}")
        definition = nodes[args.node]
        current = state["nodes"][args.node]
        _actor_allowed(definition, args.actor, state)
        if current["status"] != "ready":
            raise WorkGraphError(f"node {args.node} is {current['status']}, not ready")
        if current["attempts"] >= definition["max_attempts"]:
            raise WorkGraphError(f"node {args.node} exhausted its attempt budget")
        current.update(
            {
                "status": "running",
                "attempts": current["attempts"] + 1,
                "actor": args.actor,
                "started_at": utc_now(),
                "finished_at": None,
                "outcome_note": None,
                "artifacts": [],
                "surface_digest": None,
            }
        )
        if definition["permission"] == "write":
            state["source"]["review_content_digest"] = None
            state["source"]["review_bound_by"] = None
            state["source"]["final_content_digest"] = None
        state["status"] = "active"
        _commit_event(
            repo_root,
            state,
            "node_started",
            node=args.node,
            actor=args.actor,
            attempt=current["attempts"],
        )
    print(f"Started {args.task_id}:{args.node} attempt {current['attempts']}.")
    return 0


def finish(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        state, recipe, nodes = _transition_context(repo_root, args.task_id)
        if args.node not in nodes:
            raise WorkGraphError(f"unknown node: {args.node}")
        definition = nodes[args.node]
        current = state["nodes"][args.node]
        if current["status"] != "running":
            raise WorkGraphError(
                f"node {args.node} is {current['status']}, not running"
            )
        if current["actor"] != args.actor:
            raise WorkGraphError(
                f"node {args.node} is owned by running actor {current['actor']}"
            )
        _actor_allowed(definition, args.actor, state)
        artifacts: list[dict[str, str]] = []
        for value in args.artifact:
            path = resolve_repo_path(repo_root, value)
            artifacts.append(
                {
                    "path": relative_path(repo_root, path),
                    "sha256": sha256_worktree_artifact(
                        repo_root, relative_path(repo_root, path)
                    ),
                }
            )
        node_digest: str | None = None
        if definition["permission"] == "write" and args.outcome == "succeeded":
            _, node_digest = _source_snapshot(
                repo_root, state["source"]["base_revision"], state["task_id"]
            )
            state["source"]["review_content_digest"] = node_digest
            state["source"]["review_bound_by"] = args.node
        elif definition["binds_surface"] and args.outcome == "succeeded":
            _, node_digest = _source_snapshot(
                repo_root, state["source"]["base_revision"], state["task_id"]
            )
            review_digest = state["source"].get("review_content_digest")
            if review_digest is None or review_digest != node_digest:
                raise WorkGraphError(
                    "final source does not match the writer-frozen review binding"
                )
            state["source"]["final_content_digest"] = node_digest
        current.update(
            {
                "status": args.outcome,
                "finished_at": utc_now(),
                "outcome_note": args.note,
                "artifacts": artifacts,
                "surface_digest": node_digest,
            }
        )
        _refresh_ready(state, recipe)
        state["status"] = _derive_status(state, [])
        _commit_event(
            repo_root,
            state,
            "node_finished",
            node=args.node,
            actor=args.actor,
            attempt=current["attempts"],
            outcome=args.outcome,
            note=args.note,
            artifacts=artifacts,
            surface_digest=node_digest,
        )
    print(f"Finished {args.task_id}:{args.node} as {args.outcome}.")
    return 0


def reopen(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        state = _load_state(repo_root, args.task_id)
        if state["status"] in {"succeeded", "aborted"}:
            raise WorkGraphError(f"{state['status']} work graphs cannot be reopened")
        recipe, recipe_reasons = _recipe_for_state(repo_root, state)
        nodes = _node_map(recipe)
        if args.node not in nodes:
            raise WorkGraphError(f"unknown node: {args.node}")
        definition = nodes[args.node]
        stale, _ = _stale_reasons(repo_root, state, recipe_reasons)
        allowed_stale = (
            {"review source binding is stale"}
            if definition["permission"] == "write"
            else set()
        )
        blocking_stale = [reason for reason in stale if reason not in allowed_stale]
        if blocking_stale:
            raise WorkGraphError(
                "work graph is stale: " + "; ".join(blocking_stale)
            )
        current = state["nodes"][args.node]
        _actor_allowed(definition, args.actor, state)
        if current["status"] not in TERMINAL_NODE_STATUSES - {"profile_skipped"}:
            raise WorkGraphError(
                f"node {args.node} is {current['status']}, not reopenable"
            )
        if current["attempts"] >= definition["max_attempts"]:
            raise WorkGraphError(f"node {args.node} exhausted its attempt budget")
        invalidated = {args.node, *_descendants(recipe, args.node)}
        for node_id in invalidated:
            node_definition = nodes[node_id]
            node_state = state["nodes"][node_id]
            if not _profile_active(
                state["task_profile"], node_definition["minimum_profile"]
            ):
                node_state["status"] = "profile_skipped"
            else:
                node_state["status"] = "pending"
            node_state.update(
                {
                    "actor": None,
                    "started_at": None,
                    "finished_at": None,
                    "outcome_note": None,
                    "artifacts": [],
                    "surface_digest": None,
                }
            )
        if state["source"].get("claim") is None:
            state["source"]["claim"] = _claim_identity(
                _load_live_claim(repo_root, state["task_id"])
            )
        if definition["permission"] == "write":
            state["source"]["review_content_digest"] = None
            state["source"]["review_bound_by"] = None
        state["source"]["final_content_digest"] = None
        state["status"] = "active"
        _refresh_ready(state, recipe)
        _commit_event(
            repo_root,
            state,
            "node_reopened",
            node=args.node,
            actor=args.actor,
            reason=args.reason,
            invalidated=sorted(invalidated),
        )
    print(f"Reopened {args.task_id}:{args.node}; invalidated={sorted(invalidated)}.")
    return 0


def note(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    if not task_claim.OWNER_RE.fullmatch(args.actor):
        raise WorkGraphError(f"invalid actor label: {args.actor}")
    with _state_lock(_work_graph_root(repo_root)):
        state = _load_state(repo_root, args.task_id)
        if state["status"] in {"succeeded", "aborted"}:
            raise WorkGraphError(f"{state['status']} work graphs cannot receive notes")
        recipe, recipe_reasons = _recipe_for_state(repo_root, state)
        stale, _ = _stale_reasons(repo_root, state, recipe_reasons)
        if stale:
            raise WorkGraphError("work graph is stale: " + "; ".join(stale))
        if args.node not in _node_map(recipe):
            raise WorkGraphError(f"unknown node: {args.node}")
        state["nodes"][args.node]["notes_count"] += 1
        _commit_event(
            repo_root,
            state,
            "node_note",
            node=args.node,
            actor=args.actor,
            note_kind=args.kind,
            text=args.text,
        )
    print(f"Attached {args.kind} note to {args.task_id}:{args.node}.")
    return 0


def abort(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    with _state_lock(_work_graph_root(repo_root)):
        state = _load_state(repo_root, args.task_id)
        claim = _load_live_claim(repo_root, args.task_id)
        task_path = find_task(repo_root, state["task_id"])
        _, metadata = load_task_metadata(task_path)
        claim_reasons = _claim_binding_reasons(state, claim, metadata)
        if claim_reasons:
            raise WorkGraphError(
                f"work graph owner {state['owner']!r} no longer holds the live task "
                f"claim: {'; '.join(claim_reasons)}"
            )
        if args.actor != state["owner"]:
            raise WorkGraphError("only the claimed task owner may abort the run")
        if state["status"] in {"succeeded", "aborted"}:
            raise WorkGraphError(f"work graph is already {state['status']}")
        state["status"] = "aborted"
        state["abort_reason"] = args.reason
        if state["source"].get("claim") is None:
            state["source"]["claim"] = _claim_identity(claim)
        _commit_event(
            repo_root,
            state,
            "run_aborted",
            actor=args.actor,
            reason=args.reason,
        )
    print(f"Aborted {args.task_id} work graph; state and events retained.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command_name", required=True)

    validate = commands.add_parser("validate-recipe")
    validate.add_argument("--recipe", required=True, type=Path)
    validate.set_defaults(handler=validate_recipe_command)

    create = commands.add_parser("start")
    create.add_argument("--root", type=Path, default=Path("."))
    create.add_argument("--task-id", required=True)
    create.add_argument("--owner", required=True)
    create.add_argument("--recipe", required=True, type=Path)
    create.add_argument("--base-ref", default="origin/main")
    create.set_defaults(handler=start)

    resume_run = commands.add_parser("resume")
    resume_run.add_argument("--root", type=Path, default=Path("."))
    resume_run.add_argument("--task-id", required=True)
    resume_run.add_argument("--owner", required=True)
    resume_run.add_argument("--reason", required=True)
    resume_run.set_defaults(handler=resume)

    listing = commands.add_parser("list")
    listing.add_argument("--root", type=Path, default=Path("."))
    listing.add_argument("--json", action="store_true")
    listing.set_defaults(handler=list_runs)

    inspect = commands.add_parser("show")
    inspect.add_argument("--root", type=Path, default=Path("."))
    inspect.add_argument("--task-id", required=True)
    inspect.add_argument("--json", action="store_true")
    inspect.set_defaults(handler=show)

    begin_node = commands.add_parser("begin")
    begin_node.add_argument("--root", type=Path, default=Path("."))
    begin_node.add_argument("--task-id", required=True)
    begin_node.add_argument("--node", required=True)
    begin_node.add_argument("--actor", required=True)
    begin_node.set_defaults(handler=begin)

    finish_node = commands.add_parser("finish")
    finish_node.add_argument("--root", type=Path, default=Path("."))
    finish_node.add_argument("--task-id", required=True)
    finish_node.add_argument("--node", required=True)
    finish_node.add_argument("--actor", required=True)
    finish_node.add_argument("--outcome", required=True, choices=FINISH_OUTCOMES)
    finish_node.add_argument("--note")
    finish_node.add_argument("--artifact", action="append", default=[])
    finish_node.set_defaults(handler=finish)

    reopen_node = commands.add_parser("reopen")
    reopen_node.add_argument("--root", type=Path, default=Path("."))
    reopen_node.add_argument("--task-id", required=True)
    reopen_node.add_argument("--node", required=True)
    reopen_node.add_argument("--actor", required=True)
    reopen_node.add_argument("--reason", required=True)
    reopen_node.set_defaults(handler=reopen)

    add_note = commands.add_parser("note")
    add_note.add_argument("--root", type=Path, default=Path("."))
    add_note.add_argument("--task-id", required=True)
    add_note.add_argument("--node", required=True)
    add_note.add_argument("--actor", required=True)
    add_note.add_argument("--kind", required=True, choices=sorted(NOTE_KINDS))
    add_note.add_argument("--text", required=True)
    add_note.set_defaults(handler=note)

    abort_run = commands.add_parser("abort")
    abort_run.add_argument("--root", type=Path, default=Path("."))
    abort_run.add_argument("--task-id", required=True)
    abort_run.add_argument("--actor", required=True)
    abort_run.add_argument("--reason", required=True)
    abort_run.set_defaults(handler=abort)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if hasattr(args, "task_id"):
            _validate_task_id(args.task_id)
        return args.handler(args)
    except (WorkGraphError, ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
