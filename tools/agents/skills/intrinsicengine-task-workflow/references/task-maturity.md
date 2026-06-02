# Task Maturity Taxonomy

This file defines a shared vocabulary for distinguishing partial completion
from full capability. Use it to keep "foundation exists" from being read as
"capability shipped", especially for rendering, Vulkan, assets, hot reload,
pass command bodies, and legacy retirement.

The taxonomy is **describable**, not prescriptive: every level is real work
and may be the right place to stop for a given task. The point is that future
readers can tell which level was actually reached.

## Levels

The levels are intentionally cumulative. A task at a higher level meets all
the criteria of every level below it.

### `Scaffolded`

Structure or API exists. Behavior may be stubbed, fail-closed, return
defaults, or otherwise be incomplete. The seam is reachable but does not yet
prove the engine does the thing the seam describes.

Typical signals:

- New module/header/class/function exists with a deliberate empty body or
  fail-closed return.
- `// TODO`, `assert(false)`, or "minimal" / "skeleton" wording in commit or
  task language.
- The default CPU gate passes, but only because the new path is never
  exercised end-to-end.

Closing a task at this level requires either an explicit follow-up task ID
(per "`Scaffolded` closure rule" below) or an `Acceptance criteria` /
`Non-goals` statement that pins the scaffold as the intended endpoint.

### `CPUContracted`

CPU/null/backend-neutral contract tests exist for the seam. The behavior the
seam describes is verified by the default CPU gate
(`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) against either the Null
backend, a mock, or a deterministic CPU reference.

Typical signals:

- `tests/contract/<layer>/Test.<Feature>.cpp` or equivalent runs in the
  default gate.
- The test would fail if the seam's contract regressed.
- Backend-specific behavior (Vulkan, CUDA) and end-to-end behavior may still
  be unverified.

This is a common and legitimate stopping point for non-GPU work, foundation
tasks, and layering/seam decisions.

### `Operational`

A concrete backend or real runtime path exercises the seam under appropriate
test labels. For graphics this typically means an opt-in `gpu;vulkan` smoke
on a Vulkan-capable host; for runtime this means the seam runs in
`Engine::Run()` with the default reference config.

Typical signals:

- A `gpu` / `vulkan` / `integration` test selects the seam.
- The seam is wired into the runtime composition root or the canonical
  `app` entry point.
- Diagnostics counters / fallback breadcrumbs prove the path is taken, not
  silently skipped.

CPU-only contract coverage is insufficient on its own to claim
`Operational`; the corresponding backend-labeled or integration-labeled run
must be cited in the task's `Verification` section as having actually run
(per the contract's "verification commands actually ran in this session"
rule).

### `ParityProven`

The non-legacy path either has tests/evidence matching legacy behavior, or
records an explicit "no-parity" decision in `Non-goals` / `Acceptance
criteria`. This is the gate that lets a legacy module retire.

Typical signals:

- A migration matrix row in `docs/migration/nonlegacy-parity-matrix.md` cites
  the parity evidence and is not marked "blocked" or "partial".
- A round-trip test, golden image, or numeric tolerance covers the parity
  claim, or the task records that the legacy behavior is intentionally not
  reproduced.
- The promoted path is the default for the relevant consumers; legacy
  consumers are enumerated and tracked.

### `Retired`

The legacy path or shim has been deleted; docs, generated inventories, and
allowlists are updated. There is no compatibility re-export.

Typical signals:

- `src/legacy/<subtree>` no longer exists, or the named module is gone from
  `docs/api/generated/module_inventory.md`.
- `tools/repo/layering_allowlist.yaml` row count drops by the count of rows
  removed.
- `docs/migration/legacy-retirement.md` records the retirement.

`Retired` is the only level where the engine genuinely loses code. Reaching
it requires a `ParityProven` upstream gate or an explicit non-goal that the
legacy behavior is not reproduced.

## How to use the taxonomy

The taxonomy is **language**, not a new required field. Use it in any of the
following ways:

- In a task's `Status` block when promoting to `tasks/active/`:
  `Maturity target: Operational (this slice closes Scaffolded → Operational)`.
- In a task's `Acceptance criteria` to make the stop-state explicit: "Slice 1
  ends at `CPUContracted`; `Operational` is the responsibility of the
  follow-up `GRAPHICS-NNN`."
- In `tasks/done/` retirement summaries so future readers can tell at a
  glance what level was actually reached.
- In `docs/migration/nonlegacy-parity-matrix.md` readiness cells to replace
  the older `blocked` / `partial` / `done` vocabulary where the taxonomy is
  more precise.
- In commit messages when the maturity transition is the point of the slice:
  "GRAPHICS-033D: ReferenceTriangleRecordsOnOperationalPromotedVulkan
  (Scaffolded → CPUContracted; Operational pending Vulkan-capable host)."

The taxonomy does **not** introduce a new required `## Maturity` section in
the task template. The validator
(`tools/agents/validate_tasks.py`) still requires only the existing nine
sections; the recommended `## Maturity` field is optional and shown in
`tasks/templates/task.md` for tasks that benefit from it.

## `Scaffolded` closure rule

A task that retires to `tasks/done/` at `Scaffolded` maturity must either:

1. **Name a follow-up task ID** that owns the `CPUContracted` (or higher)
   gate, and link the follow-up from the done task's `Acceptance criteria`
   or `Status` block; or
2. **Record an explicit `Non-goals` line** stating that the scaffold is the
   intended endpoint and that no follow-up gate is owed.

The rule is enforced by review (see
[`../../../../../docs/agent/review-checklist.md`](../../../../../docs/agent/review-checklist.md) "Scope and ownership" row), not
by the validator, because the words "scaffold", "stub", "fail-closed", and
"minimal" are domain language that legitimately appears in many tasks.
Reviewers should challenge any done task whose body uses those words without
either form of follow-up.

The same rule applies one level up: a task that retires at `CPUContracted`
when the seam exists to be operational on a real backend (graphics, Vulkan,
CUDA, runtime composition) should name the `Operational` follow-up or
explicitly record the deferral.

## Vocabulary mapping for existing docs

Existing docs already use overlapping vocabulary. Treat the taxonomy as the
authoritative vocabulary going forward; do not mass-edit older tasks.

| Older wording | Taxonomy term | Notes |
| --- | --- | --- |
| "Skeleton exists" / "stub" / "fail-closed" / "minimal" | `Scaffolded` | The seam is present but does not yet prove behavior. |
| "Contract tests pass" / "default CPU gate green" | `CPUContracted` | Backend-specific behavior may still be unverified. |
| "Visible triangle on host" / "GPU smoke green" / "runtime wires X" | `Operational` | A backend or runtime path exercises the seam. |
| "Parity matrix row green" / "round-trip matches legacy" | `ParityProven` | Legacy module is eligible for deletion. |
| "Legacy subtree deleted" / "retirement complete" | `Retired` | Allowlists and inventory updated. |

## Related

- [`/AGENTS.md`](../../../../../AGENTS.md) §6 (method implementation protocol — the
  CPU-reference-first flow is the canonical `Scaffolded → CPUContracted →
  Operational → ParityProven` sequence for method work).
- [`task-format.md`](task-format.md) — task file structure; optional
  `## Maturity` guidance.
- [`../../../../../docs/agent/review-checklist.md`](../../../../../docs/agent/review-checklist.md) — per-PR review including the
  `Scaffolded` closure rule.
- [`../../../../../docs/agent/architecture-review-checklist.md`](../../../../../docs/agent/architecture-review-checklist.md) —
  architecture-impacting closure language.
- [`../../../../../docs/migration/nonlegacy-parity-matrix.md`](../../../../../docs/migration/nonlegacy-parity-matrix.md)
  — readiness cells use the taxonomy where it is more precise than the older
  `blocked`/`partial`/`done` triplet.
