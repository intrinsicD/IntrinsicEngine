# RORG-036 — Layer ownership audit for misplaced concepts

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

## Required changes
- [ ] Build an import/dependency inventory for promoted `src/` modules excluding `src/legacy/`, grouped by owning layer and imported layer.
- [ ] Flag imports that appear to exist only for low-level value types, dimensions, IDs, math helpers, or POD snapshots.
- [ ] Flag modules with names/paths suggesting higher-layer ownership while their imports are dependency-free or lower-layer-only.
- [ ] Audit value/data contracts currently in `platform`, `graphics`, `runtime`, and `ecs` for likely lower-layer homes.
- [ ] Audit camera-related promoted surfaces and distinguish:
  - ECS authoring components/state.
  - Runtime controller/input policy.
  - Graphics renderer-consumed immutable snapshots/pick-ray derivation.
  - Core/geometry reusable math or value types.
- [ ] Audit procedural-geometry promoted surfaces and distinguish:
  - ECS procedural authoring descriptors.
  - Geometry CPU mesh generation/packing primitives.
  - Graphics/RHI upload descriptors and GPU handles.
  - Runtime cache/extraction bridge policy.
- [ ] Audit runtime modules that may be lower-layer candidates after dependency inspection, especially `Runtime.StreamingExecutor`, procedural geometry packer helpers, and any remaining dependency-free contracts.
- [ ] Produce a table of findings with: current module/path, imports, current owner, proposed owner, move type (`mechanical`, `semantic rename`, `split`), risk, tests/docs affected, and follow-up task ID.
- [ ] Create or update follow-up backlog tasks for each accepted refactor; each task must be scoped to one move/split and must not mix mechanical relocation with semantic API redesign.

## Tests
- [ ] Run the generated/import-inventory script or documented command used for the audit and archive the resulting summary in the task or linked report.
- [ ] Run `python3 tools/repo/check_layering.py --root src --strict` before and after any follow-up tasks created by this audit.
- [ ] For the audit-only task, run task/docs structural checks after adding follow-up tasks.

## Docs
- [ ] Update `docs/architecture/*` or `docs/migration/nonlegacy-parity-matrix.md` only with factual ownership decisions accepted by the audit.
- [ ] If module surfaces are changed by follow-up tasks, regenerate `docs/api/generated/module_inventory.md` in those follow-ups.
- [ ] Link accepted follow-up task IDs from the audit findings table.

## Acceptance criteria
- [ ] The audit identifies all promoted modules that import a higher or sibling layer only for a simple value/data type.
- [ ] The audit explicitly decides whether each finding should move to `core`, `geometry`, `ecs`, `graphics`, `platform`, stay in `runtime`, or be split.
- [ ] Each accepted refactor has a separate backlog/active task with clear non-goals and verification.
- [ ] No code movement is performed by this audit task itself.
- [ ] Layering invariants in `/AGENTS.md` remain unchanged unless a separate architecture decision task updates the contract and docs.

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

