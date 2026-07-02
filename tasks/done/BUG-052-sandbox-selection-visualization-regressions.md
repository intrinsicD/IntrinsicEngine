---
id: BUG-052
theme: none
depends_on: [BUG-026, BUG-047, BUG-051]
maturity_target: CPUContracted
---
# BUG-052 — Sandbox selection and visualization regressions

## Goal
- Restore responsive selected-entity rendering and correct scalar/color visualization behavior in the sandbox.

## Non-goals
- Rewriting the renderer recipe system.
- Changing selection semantics or primitive-picking result encoding.
- Adding new visualization UI modes beyond fixing the existing controls.

## Context
- Symptom: selecting an entity makes the sandbox feel slow and delayed.
- Symptom: scalar colormaps and isolines can resolve to black when the selected property lives on a graph or point-cloud domain.
- Symptom: KMeans label colors flatten mesh shading even when vertex normals are assigned.
- Expected behavior: selected entities should only pay for the ID work required by the current frame, scalar/color property buffers should be extracted for the active geometry-source domain, and SciVis colors should remain lit unless unlit rendering is explicitly requested.
- Impact: the primary sandbox inspection path is difficult to use for real meshes, point clouds, and method-label visualization.

## Completion
- Completed: 2026-07-02. Commit/PR: this local fix commit.
- Root cause: selected/hovered frames reused the aggregate picking route and recorded face/edge/point primitive ID subpasses every frame even when only the outline needed entity IDs; SciVis override material builders set the legacy `Unlit` flag for visualization color modes; and automatic visualization property-buffer extraction was restricted to mesh-domain `GeometrySources`.
- Fix summary: outline-only frames now record only entity IDs and skip primitive-picking/readback work, all visualization color-source overrides stay lit by default, and runtime extraction maps visualization properties across mesh, graph, and point-cloud domains with fail-closed diagnostics.

## Required changes
- [x] Make outline-only selected/hovered frames record entity IDs for outline sampling without recording face/edge/point primitive picking subpasses.
- [x] Keep visualization color-source overrides lit by default so assigned normals continue to shade the surface.
- [x] Generalize automatic visualization property-buffer extraction from mesh-only sources to mesh, graph, and point-cloud domains.

## Tests
- [x] Add renderer lifecycle coverage proving outline-only frames skip primitive picking work while click-pick frames still record the full primitive route.
- [x] Add visualization-sync coverage proving uniform, scalar, and per-element overrides are lit.
- [x] Add runtime extraction coverage for point-cloud and graph scalar/color property-buffer uploads.

## Docs
- [x] Update task records and generated session brief.

## Acceptance criteria
- [x] Selection outline remains visible and click picking still records readback copies.
- [x] Scalar and per-element color visualizations preserve normal-based lighting by default.
- [x] Graph/point-cloud scalar and color-buffer visualizations publish nonzero property-buffer BDAs under the CPU/null contract gate.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
# Passed.

cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
# Passed after updating the failure-count regression expectation.

ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|GraphicsMinimalAcceptance|RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 111/111 tests.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Failed: 3450/3451 tests passed; unrelated pre-existing
# SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms remains
# deterministic with FinalRMSE 0.30912062422932945 > 1.0e-3.

python3 tools/agents/check_task_policy.py --root . --strict
# Passed.

python3 tools/repo/check_layering.py --root src --strict
# Passed.

python3 tools/repo/check_test_layout.py --root . --strict
# Passed.

python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
# Passed.

python3 tools/docs/check_doc_links.py --root .
# Passed.
```

## Forbidden changes
- Shipping a fix without feasible regression coverage.
- Reverting BUG-026, BUG-047, or BUG-051 behavior.
- Mixing unrelated UI/backend-selector changes into this bug fix.

## Maturity
- Target: `CPUContracted` for the regression contracts in this slice. A manual Vulkan sandbox smoke is useful after merge, but no `Operational` follow-up is owed because the changed behavior is pinned at the renderer/runtime contract seams.
