---
id: UI-021
theme: F
depends_on: [RUNTIME-117, UI-020]
maturity_target: CPUContracted
---
# UI-021 — Sandbox editor geometry availability migration

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: `Runtime.SandboxEditorUi` now uses the runtime availability resolver
  for domain windows, visualization target activation, property catalogs,
  primitive-view commands, render hints, K-Means affordances, and mesh
  UV/bake diagnostics while preserving mesh/graph/point-cloud provenance.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicECSTests` succeeded, and focused CPU/null coverage for
  `GeometrySources`, geometry availability, packers, extraction, progressive
  data, and `SandboxEditorUi` passed 205/205 tests.

## Goal
- Migrate `Runtime.SandboxEditorUi` domain windows, property catalogs, visualization controls, and processing affordances to the standard geometry availability resolver so UI activation depends on the data/render lane needed, not exact `ActiveDomain` alone.

## Non-goals
- No new geometry algorithms.
- No new renderer, RHI, shader, or GPU residency behavior.
- No UI ownership of geometry data, render resources, asset lifetime, or ECS sidecars.
- No native dialog, multi-window, or platform input work.

## Context
- Owning subsystem/layer: `runtime` editor UI reads live ECS through runtime-owned models and emits runtime/editor commands; it must not become the canonical availability-policy owner.
- `UI-020` retired lane uniform-color controls but still left mixed local availability logic in `Runtime.SandboxEditorUi.cpp`.
- `RUNTIME-117` defines the runtime availability resolver that this task consumes.
- Current UI helpers such as `DomainsForSourceView(...)`, property catalog domain routing, and some window activation paths are still tied to `GeometrySources::ActiveDomain`.
- Desired behavior: a mesh can expose vertex-point controls when `RenderPoints` is active, edge controls when `RenderEdges` is active, and surface controls when `RenderSurface` plus required mesh sources are available; graph and point-cloud windows follow the same "ask for what they need" rule.

## Required changes
- [x] Replace UI-local geometry source/domain availability helpers with calls into the standard runtime resolver from `RUNTIME-117`.
- [x] Keep domain/provenance labels visible in the UI so users can tell whether points/edges come from a mesh, graph, or point-cloud entity.
- [x] Make visualization controls activate by render-lane availability: surface controls need surface lane support, edge controls need edge lane support, point controls need point lane support.
- [x] Make property catalogs and binding pickers enumerate properties from available CPU source capabilities rather than exact active domain alone.
- [x] Make geometry-processing affordances ask for the minimum CPU data required by each operation; operations requiring full mesh topology must still early-out when halfedges/faces are missing.
- [x] Remove or shrink UI-only duplicated capability bitmasks where the runtime resolver provides the same information.
- [x] Preserve undo/redo, dirty-state, and command-history routing.

## Tests
- [x] Update `contract;runtime` `SandboxEditorUi` coverage for mesh selections with independent surface, edge, and point render lanes.
- [x] Add UI coverage proving mesh vertex properties can be offered to point-lane visualization/property controls without opening a false point-cloud provenance.
- [x] Add UI coverage for graph node point-lane controls independent of graph edge controls.
- [x] Add UI coverage for partial/missing-source entities proving windows or rows fail closed with deterministic diagnostics instead of silently hiding unrelated valid lanes.
- [x] Preserve existing UI-019/UI-020 uniform-color and lane-override behavior.

## Docs
- [x] Update `tasks/backlog/ui/README.md` and the now-retired
      `tasks/archive/RORG-031F-ui-integration.md` with this child task and its
      trigger.
- [x] Update `src/runtime/README.md` only if UI-facing resolver usage needs a runtime note beyond `RUNTIME-117`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task.

## Acceptance criteria
- [x] `SandboxEditorUi` no longer owns a separate source/domain availability policy for visualization, property catalogs, or processing affordances.
- [x] Mesh vertices rendered as points can be controlled as point-lane data while preserving mesh provenance.
- [x] Mesh edges and graph edges are controlled by the data/render lane they require, not by unrelated surface/point availability.
- [x] Algorithms or UI actions that require halfedges/faces are disabled or diagnostic when those sources are absent.
- [x] Existing visualization uniform-color and lane uniform-color controls keep working through runtime-owned commands.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|ProgressiveRenderData|MeshPrimitiveViewExtraction|GraphGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding renderer, RHI, asset-service, or geometry storage ownership to the UI.
- Reintroducing `ActiveDomain` as the UI's standard availability gate.
- Making UI-local helpers the source of truth for CPU/GPU availability.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
- This task closes the editor migration to the runtime CPU/render-lane availability resolver. GPU availability display, if desired, should consume `RUNTIME-119` through a separate UI follow-up.
