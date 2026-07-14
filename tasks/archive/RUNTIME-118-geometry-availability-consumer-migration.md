---
id: RUNTIME-118
theme: F
depends_on: [RUNTIME-117]
maturity_target: CPUContracted
---
# RUNTIME-118 — Geometry availability consumer migration

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: runtime packers, extraction routing, progressive property
  resolution, selected bake validation, and primitive-selection refinement now
  consume the availability/provenance model instead of treating exact
  `ActiveDomain` as the common capability gate.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicECSTests` succeeded, and focused CPU/null coverage for
  `GeometrySources`, geometry availability, packers, extraction, progressive
  data, and `SandboxEditorUi` passed 205/205 tests.

## Goal
- Migrate existing runtime consumers from ad hoc `ActiveDomain` capability checks to the standard runtime availability resolver, while preserving exact-domain validation where an algorithm or packer truly needs a full mesh, graph, or point-cloud contract.

## Non-goals
- No new resolver design beyond `RUNTIME-117`.
- No Sandbox editor UI migration; `UI-021` owns editor-facing windows and controls.
- No GPU availability reporting; `RUNTIME-119` owns GPU snapshots.
- No behavior change for algorithms that genuinely require full halfedge-mesh topology.

## Context
- Owning subsystem/layer: `runtime`.
- Current duplicated or overly strict checks exist in runtime packers, progressive render-data helpers, selected bake validation, primitive-view/extraction preflight, and selected-entity models.
- Pack modules such as `Runtime.MeshGeometryPacker`, `Runtime.GraphGeometryPacker`, `Runtime.PointCloudGeometryPacker`, and `Runtime.MeshPrimitiveViewPacker` already fail closed with useful diagnostics, but several `WrongDomain` gates conflate provenance with missing source capability.
- `Runtime.ProgressiveRenderData::ResolvePropertySet(...)` currently gates property-domain availability through `ActiveDomain`, which prevents mesh vertices or graph nodes from participating in point-style consumers when the data exists.
- This task is the cleanup pass that makes `RUNTIME-117` real for engine code instead of leaving it as a parallel helper.

## Required changes
- [x] Replace runtime call-site capability checks identified by `RUNTIME-117` with the standard resolver, leaving exact-domain checks only where the required data contract is truly exact.
- [x] Update progressive property binding/enumeration helpers so property domains resolve from available CPU sources and provenance, not exact active domain alone.
- [x] Update packer preflight and diagnostics to distinguish "wrong provenance for this packer" from "missing source capability needed by this lane".
- [x] Update render extraction lane routing to use the resolver for requested/supported lane decisions before invoking packers.
- [x] Update selected bake and selected geometry command preflight where they are source-capability checks rather than mesh-only algorithm requirements.
- [x] Preserve existing fail-closed behavior and stale-residency cleanup for unsupported or invalid lanes.

## Tests
- [x] Update runtime extraction tests for mesh edge-only, mesh point-only, graph point/edge, and point-cloud point paths to assert resolver-driven diagnostics.
- [x] Add progressive property binding tests proving mesh vertex properties and graph node properties can be offered to point-style consumers while keeping provenance visible.
- [x] Add packer diagnostic tests for missing halfedges, missing faces, missing edge endpoints, and unsupported surface requests.
- [x] Preserve selected bake tests that intentionally require mesh surface/UV data.

## Docs
- [x] Update `src/runtime/README.md` to replace `ActiveDomain`-as-capability wording with resolver-based availability wording.
- [x] Update `tasks/backlog/runtime/README.md` if migration scope changes.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening or re-gating this task.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] Runtime consumers no longer duplicate source-availability policy for common point, edge, halfedge, face, property-domain, or render-lane checks.
- [x] Existing exact-domain validation remains explicit and documented where required.
- [x] Progressive property/domain helpers can represent mesh-as-point-set and graph-as-point-set use cases without losing provenance.
- [x] Packer and extraction diagnostics distinguish missing source data from unsupported provenance.
- [x] Focused runtime contract tests prove no stale renderable survives after a capability becomes unavailable.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressiveRenderData|MeshPrimitiveViewExtraction|MeshGeometryExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction|SelectedMeshTextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Weakening fail-closed behavior for invalid topology or missing buffers.
- Treating exact `ActiveDomain` as the standard capability query after this migration.
- Moving GPU availability into this CPU/runtime-consumer migration.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
- This task closes the runtime consumer migration under the CPU/null gate. GPU availability is owned by `RUNTIME-119`.
