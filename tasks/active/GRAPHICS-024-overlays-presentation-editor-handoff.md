# GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff

- Status: in-progress (planning-only; no C++ behavior in this task).
- Owner / agent: planning across `src/runtime`, `src/graphics/renderer`, and
  `src/platform`, with cross-links into `src/legacy/Graphics` retirement.
- Branch: `claude/plan-rendering-tasks-ORYwq` (planning).
- PR: TBD.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` after each planning slice.

## Goal

Assign promoted ownership for legacy overlay entity factory behavior,
presentation adjacency, and editor/UI rendering handoff without putting
editor mutation or platform/window ownership into graphics. Produce:

1. A canonical inventory of legacy overlay/presentation/editor-adjacent
   modules and their behaviors.
2. A per-behavior owner decision (runtime/editor/app, graphics, or platform).
3. Promoted overlay snapshot/packet contract sketches with destruction and
   lifecycle invariants.
4. Cross-links from `GRAPHICS-010`, `GRAPHICS-011`, `GRAPHICS-014`,
   `GRAPHICS-017`, and `GRAPHICS-020` so retirement gating can mechanically
   resolve overlay/presentation modules to a final owner.

## Non-goals

- No editor feature expansion.
- No platform/window/swapchain policy transfer into `src/graphics/renderer`.
- No legacy code copying.
- No renderer implementation in this planning task.
- No new public C++ API in `src/graphics/*`; promoted contract sketches stay
  in markdown until each subtask is split out and promoted to its owning
  backlog (graphics or runtime).
- No live-ECS knowledge introduced into `src/graphics/*` packets.

## Context

Legacy modules touching this scope (canonical references only — must not be
copied into promoted layers):

- `src/legacy/Graphics/Graphics.OverlayEntityFactory.cppm` /
  `Graphics.OverlayEntityFactory.cpp` — user-facing factory that creates
  overlay entities (lines/points/spheres/vector-field children) and seeds
  GPU upload state.
- `src/legacy/Graphics/Graphics.Presentation.cppm` /
  `Graphics.Presentation.cpp` — presentation adjacency + final composition
  handoff that drove legacy swapchain finalization.
- `src/legacy/Graphics/Passes/Graphics.Passes.Composition.cppm` /
  `Graphics.Passes.Composition.cpp` — legacy composition pass behavior.
- `src/legacy/Graphics/Graphics.VisualizationConfig.cppm` — visualization
  configuration adjacent to overlay parametrization.

Promoted-layer modules already partly covering this area:

- `src/graphics/renderer/Graphics.VisualizationPackets.{cppm,cpp}` — overlay
  visualization packet shape (canonical promoted home).
- `src/graphics/renderer/Graphics.VisualizationSyncSystem.{cppm,cpp}` —
  graphics-side visualization sync that consumes runtime snapshots.
- `src/graphics/renderer/Components/Graphics.Component.VisualizationConfig.cppm`
  — promoted visualization component descriptor.
- `src/graphics/renderer/Graphics.ImGuiOverlaySystem.{cppm,cpp}` — promoted
  ImGui overlay system that owns CPU-visible draw-data summaries (no
  platform backend ownership).
- `src/runtime/Runtime.RenderExtraction.cppm` — runtime extraction that
  produces immutable graphics snapshots.

Documents that are authoritative inputs for this task:

- `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` — five
  named invariants (geometry ownership, dirty-domain monotonicity,
  parent/child destruction closure, selection/outline parity, extraction
  determinism). This task must not weaken any of them.
- `docs/architecture/rendering-three-pass.md` — ImGui overlay + present
  rules and the imported-Backbuffer write authority restricted to the
  present finalizer.
- `docs/architecture/runtime-subsystem-boundaries.md` — runtime ownership
  boundaries.
- `docs/migration/nonlegacy-parity-matrix.md` — current parity status that
  must be updated when overlay/presentation rows move.

Adjacent tasks that depend on the decisions in this task:

- `GRAPHICS-010` (line/point/transient debug primitives) — owns the
  primitive-pass packet contract that overlays render through.
- `GRAPHICS-011` (spatial debug visualizers) — overlay-adjacent
  visualizers.
- `GRAPHICS-014` (visualization attributes and overlays) — visualization
  attribute packets that overlap with overlay packet authoring.
- `GRAPHICS-017` (camera, interaction, and gizmo boundaries) — editor /
  camera ownership at the overlay/picking edge.
- `GRAPHICS-020` (legacy graphics retirement gates) — consumer of the
  per-module owner decisions made here.

## Required changes

This task is planning-only. It produces:

1. An inventory document and decision matrix.
2. Promoted snapshot/packet contract sketches (text only).
3. Cross-link updates that wire retirement gating to those decisions.

### 1. Inventory and decision matrix

Add a section to `docs/migration/nonlegacy-parity-matrix.md` (or a sibling
linked file under `docs/migration/`) that lists, for each legacy module
above and any sibling legacy overlay/presentation behavior, the following
columns:

- legacy module path,
- behavior summary (≤1 sentence),
- promoted owner (`runtime/editor/app`, `graphics`, or `platform`),
- promoted module/path (existing or proposed),
- snapshot/packet hand-off shape (immutable struct name or "TBD"),
- retirement gate (`GRAPHICS-020` row reference),
- open questions (free text).

The decision matrix must cover at minimum:

| Legacy behavior | Promoted owner |
| --- | --- |
| Overlay entity creation (line/point/sphere/vector-field children) | runtime/editor/app |
| Overlay component dirty/topology/attribute mutation | runtime/editor/app |
| Overlay GPU upload + descriptor binding | graphics (via existing `VisualizationSyncSystem` / `VisualizationPackets`) |
| Overlay selection-outline eligibility classification | graphics (consumed from runtime selection state via packet) |
| Final present / `SceneColorLDR → Backbuffer` finalization | graphics (single finalizer pass per recipe) |
| Swapchain creation/recreation/resize | platform → graphics RHI (no `platform` import inside `graphics/renderer`) |
| ImGui platform/window glue | runtime + platform (graphics consumes draw-data summaries only) |
| Editor input → overlay mutation | runtime/editor/app |
| Camera/gizmo state mutation | runtime (per `GRAPHICS-017`) |

Each row that maps to a runtime/editor/app or platform owner must be
referenced by a follow-up task (existing or newly-filed) so retirement
gating in `GRAPHICS-020` is mechanical.

### 2. Promoted overlay snapshot packet sketches

Sketch the shape (markdown only — no `.cppm` edits in this task) of the
immutable overlay snapshot/packet contracts that runtime should produce and
graphics should consume:

- `OverlayLineSnapshot`: per-line endpoints (object-space), color, width,
  selection-outline eligibility flag, z-bias hint, dirty-domain stamp.
- `OverlayPointSnapshot`: positions, radii, colors, selection-outline
  eligibility flag, dirty-domain stamp.
- `OverlayTriangleSnapshot` (only if needed by an existing overlay use
  case — keep optional and cross-link the use case).
- `OverlayVectorFieldSnapshot`: parent reference, sample positions,
  vectors, color/scale parameters, child-overlay invariants stamp.
- `PresentSnapshot`: presentation source resource name (e.g.
  `SceneColorLDR`), backbuffer state expectations, optional ImGui-overlay
  attachment flag.

Each sketch must call out:

- which side owns mutation (always runtime/editor/app for these),
- which side owns GPU translation (always graphics),
- the dirty-domain stamp scheme (must satisfy invariant B),
- parent/child closure expectations (must satisfy invariant C),
- selection-outline parity (must satisfy invariant D),
- extraction determinism guarantees (must satisfy invariant E),
- where invalid-packet diagnostics surface (graphics-side, structured).

### 3. Lifecycle invariant alignment

If any new overlay class or selection-outline eligibility change emerges
during inventory, update
`docs/architecture/vectorfield-overlay-lifecycle-invariants.md` to keep all
five invariants intact. Avoid weakening any invariant; if scope creep is
required, split it into a new task rather than amending invariants here.

### 4. Cross-links

Edit each of the following task files to add a one-paragraph cross-link to
this task's owner decisions, scoped to the part each task owns:

- `tasks/done/GRAPHICS-010-lines-points-debug-primitives.md` — cross-link
  in a "Follow-ups" or "Related" subsection (do not change acceptance
  state).
- `tasks/done/GRAPHICS-011-spatial-debug-visualizers.md` — same.
- `tasks/done/GRAPHICS-014-visualization-attributes-overlays.md` — same.
- `tasks/done/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md` —
  same.
- `tasks/backlog/rendering/GRAPHICS-020-legacy-graphics-retirement-gates.md`
  — add this task as the source of overlay/presentation/editor handoff
  ownership decisions to evaluate during retirement gating.

The done-task edits must not change acceptance criteria or completion
metadata; add only a "Follow-up cross-link" appendix line so the
validator's done-task constraints (completion date, PR/commit reference)
stay valid.

### 5. Architecture doc updates

- `docs/architecture/graphics.md` — if any new promoted overlay packet
  contracts become public, add a single paragraph in the existing overlay
  section. Do not introduce a new top-level architecture section without
  explicit follow-up.
- `docs/architecture/runtime-subsystem-boundaries.md` — add a subsection
  cross-linking this task and listing the runtime-owned overlay
  responsibilities derived in §1.

### 6. Forbidden-during-this-task behaviors

- No new `.cppm` / `.cpp` files in `src/graphics/*` or `src/runtime/*`.
- No edits to `Graphics.VisualizationPackets.cppm`,
  `Graphics.VisualizationSyncSystem.cppm`,
  `Graphics.ImGuiOverlaySystem.cppm`, or any other promoted-layer module.
- No edits to `src/legacy/Graphics/Graphics.OverlayEntityFactory.{cppm,cpp}`
  or `src/legacy/Graphics/Graphics.Presentation.{cppm,cpp}`.
- No CMakeLists changes.

When the inventory uncovers a behavior that needs an immediate API change,
file a separate task under `tasks/backlog/rendering/` (or
`tasks/backlog/runtime/`) and reference it from this task's matrix; do not
sneak the change in here.

## Tests

Planning-only verification:

- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

When follow-up implementation tasks are filed for runtime/editor or
graphics work derived from this matrix, those tasks own their own:

- `contract;runtime;graphics` or `integration;runtime;graphics` tests for
  deterministic overlay extraction (per invariants A–E).
- `contract;graphics` tests for overlay packet defaults and invalid-packet
  diagnostics surfaced by graphics-side packet consumers.
- Optional `gpu;vulkan` smoke coverage; the default CPU gate must remain
  the primary correctness signal.

## Docs

- `docs/migration/nonlegacy-parity-matrix.md` — overlay/presentation/editor
  handoff rows updated with the matrix from §1.
- `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` — only
  edited if §3 surfaces invariant-relevant changes.
- `docs/architecture/graphics.md` — minimal update only if §5 conditions
  are met.
- `docs/architecture/runtime-subsystem-boundaries.md` — runtime overlay
  ownership subsection update.
- `tasks/done/GRAPHICS-010|011|014|017-*.md` — cross-link appendix only.
- `tasks/backlog/rendering/GRAPHICS-020-legacy-graphics-retirement-gates.md`
  — cross-link to this task as overlay/presentation owner source.

## Acceptance criteria

- Every legacy overlay/presentation/editor-adjacent behavior in the
  inventory has a single named promoted owner (runtime/editor/app,
  graphics, or platform) and a follow-up task or explicit "no follow-up
  needed" decision.
- Graphics is documented as not owning editor mutation, ECS mutation,
  platform presentation policy, or window/input state.
- `GRAPHICS-020` retirement gating can mechanically map every legacy
  overlay/presentation module to a promoted owner via this task's matrix.
- Runtime/editor-to-graphics overlay handoff is documented as
  snapshot/packet based, deterministic, and consistent with the five
  vector-field/overlay lifecycle invariants.
- All five invariants (A–E) in
  `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` remain
  intact (or are explicitly strengthened with documented rationale).
- Doc-link and task-policy checks pass in strict mode.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict

# Optional: re-run the full default CPU CTest gate to confirm planning-only
# edits introduced no accidental code changes.
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No editor feature expansion.
- No platform/window ownership in graphics.
- No legacy code copying.
- No live ECS ownership in promoted graphics APIs.
- No source-code edits in `src/graphics/*`, `src/runtime/*`, or
  `src/legacy/*` while this task is open; this task is planning-only.
- No mixing of docs-only planning here with C++ behavior changes elsewhere
  in the same PR.

## Implementation slice plan

Land in two small markdown-only PRs to keep cross-link edits separate from
the new matrix:

1. **Slice A — inventory and decision matrix** in
   `docs/migration/nonlegacy-parity-matrix.md` plus the runtime ownership
   subsection in `docs/architecture/runtime-subsystem-boundaries.md`.
2. **Slice B — cross-links** from `GRAPHICS-010/011/014/017/020` task
   files plus the optional one-paragraph update in
   `docs/architecture/graphics.md`. Filed-out follow-up tasks that emerge
   from §1 also land in Slice B (one new task file per row that requires
   implementation, each conforming to `docs/agent/task-format.md`).

Each slice runs the verification commands above before merge.
