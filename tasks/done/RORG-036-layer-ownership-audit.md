# RORG-036 — Layer ownership audit for misplaced concepts

## Status

- Status: done (retired 2026-06-06). The whole-tree layer-ownership audit ran on
  `HEAD`; results are recorded in the "Audit results (2026-06-06)" section below.
- Completed: 2026-06-06.
- Commit: this retirement commit records the audit findings and the task move to
  `tasks/done/`. No engine code was moved (audit-only task; zero accepted moves).
- Owner/layer: architecture governance (Theme F).

## Goal
- Identify promoted non-legacy modules, value types, and files whose current layer placement forces higher-layer imports or obscures the true architectural owner, then produce a prioritized refactor plan with one task per safe move/split.

## Non-goals
- Do not move files or change code in this audit task.
- Do not introduce new dependency edges to justify existing misplaced code.
- Do not batch unrelated mechanical moves with semantic API redesigns.
- Do not retire legacy modules as part of this audit; legacy retirement remains tracked by existing retirement gates.

## Context
- `RORG-034` moved the dependency-free frame-loop contract from `runtime` to `core`.
- `RORG-035` moved generic 2D extent/rectangle value types from `platform` to `core` after `graphics`, `rhi`, and camera code were importing a live platform window module just to name viewport dimensions.
- Similar smells may remain elsewhere: a low-level value type living in a high-level layer, pure math/data transforms living in renderer/runtime modules, or modules importing a layer only for a POD type rather than for that layer's live service/port.
- The audit must apply the dependency table in `/AGENTS.md` exactly. Candidate destinations must preserve the allowed dependency flow: `core` has no engine-layer imports, `geometry` depends only on `core`, `ecs` owns gameplay/entity authoring data, `graphics` owns render snapshots/GPU-facing contracts, `platform` owns window/input ports, and `runtime` owns composition and cross-layer policy.

## Audit results (2026-06-06)

Audit run on `HEAD`. Import inventory gathered with
`git grep -hE "^\s*(export\s+)?import\s+Extrinsic\."` grouped by layer over
`src/` excluding `src/legacy/`, plus `find -name '*.cppm'` to enumerate
dependency-free modules. Layering re-verified with
`python3 tools/repo/check_layering.py --root src --strict`.

### Import inventory summary (promoted, non-legacy)

| Layer | cppm | Cross-layer imports observed | Verdict |
|---|---|---|---|
| `core` | 39 | none (foundation) | clean |
| `geometry` | 85 | `core` only | clean |
| `assets` | 11 | `core` only | clean (CPU-only) |
| `ecs` | 28 | `core` only (`Core.FrameGraph`, `Core.Hash`) | clean |
| `physics` | 1 | `core`, `geometry` | clean (ADR-0019) |
| `graphics/*` | 99 | `core`, asset IDs, `rhi`; **no** `ecs`/`runtime`/`platform` imports and **no** `Extrinsic.Geometry` value-type imports | clean |
| `platform` | 5 | `core` only | clean |
| `runtime` | 26 | all lower layers (composition) | clean |
| `app` | 1 | `runtime` only | clean |

No promoted module imports a higher or sibling layer for a simple value/data
type. `RORG-034` (frame-loop contract → `core`) and `RORG-035` (extent/rect
value types → `core`) already absorbed the previously-observed smells, so the
graph satisfies the `/AGENTS.md` §2 table directly.

### Candidates examined and decisions

| Module | Imports | Smell hypothesis | Decision | Move type |
|---|---|---|---|---|
| `Runtime.CameraControllers` | runtime | camera authoring vs controller policy | **stays** — runtime input/controller policy, already separated from the renderer-consumed `Graphics.CameraSnapshots` | none |
| `Graphics.CameraSnapshots` | `core`/`rhi` | renderer-consumed immutable snapshot | **stays** — graphics-owned frame snapshot (explicit `RORG-036` non-goal forbids moving it to ECS) | none |
| `Runtime.ProceduralGeometryPacker` | `ECS` + `Graphics.GpuWorld` | geometry CPU primitive misplaced in `runtime` | **stays** — genuine ECS→`GpuWorld` residency bridge; cross-layer wiring is runtime-owned | none |
| `Runtime.{Mesh,Graph,PointCloud}GeometryPacker` | `ECS` + `Graphics.GpuWorld` | same | **stays** — same bridge rationale | none |
| `Runtime.StreamingExecutor` | `core` only | generic scheduler misplaced in `runtime` | **stays** — models a streaming task state machine (CPU-payload / GPU-upload / main-thread-apply states) that is runtime streaming-orchestration policy; reusing `Core.Dag.Scheduler` is the correct lower-layer dependency, not a misplacement | none |
| `Runtime.RenderWorldPool` | none (pure std) | generic pipelined slot pool → `core` | **stays** — the slot-lifecycle mechanism is generic, but it has a single consumer (runtime extraction) and render-world-specific naming/diagnostics. Extracting a generic `core` primitive now would be premature abstraction (a flagged failure mode in `docs/agent/agent-output-review-checklist.md`). Recorded as a latent candidate; revisit only if a second consumer appears. | split (deferred) |

The remaining dependency-free promoted `.cppm` interfaces (ECS component
structs, `RHI.Types`/`RHI.Descriptors`/`RHI.FrameHandle`, `Backends.Vulkan.*`
value types, `Asset.TypePool`) are domain-specific value contracts owned by
their layer; none force a lower layer to import a higher one, so none are
movement candidates.

### Outcome

**Zero accepted moves.** The promoted layer graph satisfies the `/AGENTS.md` §2
dependency table with no value-type-only cross-layer imports, so no scoped
move/split follow-up tasks are filed. `RenderWorldPool` is recorded above as a
latent `core`-split candidate to revisit on a second consumer. This audit task
retires recording the clean baseline; no `docs/architecture/*` or
`nonlegacy-parity-matrix.md` edit is required because no ownership decision
changed.

## Required changes
- [x] Build an import/dependency inventory for promoted `src/` modules excluding `src/legacy/`, grouped by owning layer and imported layer.
- [x] Flag imports that appear to exist only for low-level value types, dimensions, IDs, math helpers, or POD snapshots.
- [x] Flag modules with names/paths suggesting higher-layer ownership while their imports are dependency-free or lower-layer-only.
- [x] Audit value/data contracts currently in `platform`, `graphics`, `runtime`, and `ecs` for likely lower-layer homes.
- [x] Audit camera-related promoted surfaces and distinguish:
  - ECS authoring components/state.
  - Runtime controller/input policy.
  - Graphics renderer-consumed immutable snapshots/pick-ray derivation.
  - Core/geometry reusable math or value types.
- [x] Audit procedural-geometry promoted surfaces and distinguish:
  - ECS procedural authoring descriptors.
  - Geometry CPU mesh generation/packing primitives.
  - Graphics/RHI upload descriptors and GPU handles.
  - Runtime cache/extraction bridge policy.
- [x] Audit runtime modules that may be lower-layer candidates after dependency inspection, especially `Runtime.StreamingExecutor`, procedural geometry packer helpers, and any remaining dependency-free contracts.
- [x] Produce a table of findings with: current module/path, imports, current owner, proposed owner, move type (`mechanical`, `semantic rename`, `split`), risk, tests/docs affected, and follow-up task ID.
- [x] Create or update follow-up backlog tasks for each accepted refactor; each task must be scoped to one move/split and must not mix mechanical relocation with semantic API redesign.

## Tests
- [x] Run the generated/import-inventory script or documented command used for the audit and archive the resulting summary in the task or linked report.
- [x] Run `python3 tools/repo/check_layering.py --root src --strict` before and after any follow-up tasks created by this audit.
- [x] For the audit-only task, run task/docs structural checks after adding follow-up tasks.

## Docs
- [x] Update `docs/architecture/*` or `docs/migration/nonlegacy-parity-matrix.md` only with factual ownership decisions accepted by the audit.
- [x] If module surfaces are changed by follow-up tasks, regenerate `docs/api/generated/module_inventory.md` in those follow-ups.
- [x] Link accepted follow-up task IDs from the audit findings table.

## Acceptance criteria
- [x] The audit identifies all promoted modules that import a higher or sibling layer only for a simple value/data type.
- [x] The audit explicitly decides whether each finding should move to `core`, `geometry`, `ecs`, `graphics`, `platform`, stay in `runtime`, or be split.
- [x] Each accepted refactor has a separate backlog/active task with clear non-goals and verification.
- [x] No code movement is performed by this audit task itself.
- [x] Layering invariants in `/AGENTS.md` remain unchanged unless a separate architecture decision task updates the contract and docs.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

Optional inventory helpers for the audit:

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
grep -R "^import Extrinsic\.\|^export import Extrinsic\." -n src --exclude-dir=legacy
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Moving a module to `core` if it requires `glm`, ECS, assets, platform, graphics, runtime, Vulkan, or other higher-layer imports without a separate approved architecture decision.
- Moving renderer snapshot contracts into ECS merely because cameras are authored by ECS; ECS may own persistent camera authoring data, but renderer frame snapshots and GPU-facing handoff contracts remain graphics/runtime unless split by a dedicated task.
- Moving platform window/input ports out of `platform`; only generic value types and pure data contracts are candidates for lower layers.

