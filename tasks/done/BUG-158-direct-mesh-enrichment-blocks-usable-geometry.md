---
id: BUG-158
theme: J
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation; evidence is the reviewed diff and CPU/Vulkan test runs."
owner: codex
branch: codex/bug-158-ready-during-enrichment
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-05T14:09:29+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
contract_review: "The repair changes runtime/UI method readiness while optional UV/normal enrichment may write property-backed mesh state. It must reuse canonical element-domain preflight and preserve generation-safe property publication without narrowing compatible sources."
maturity_target: Operational
---
# BUG-158 — Optional direct-mesh enrichment blocks already usable geometry

## Status

- Completed 2026-09-05 at `Operational` maturity; full CPU and GPU/Vulkan
  gates passed on the recovered source.
- Commit: `f131d1c4f` (implementation).
- Operator confirmed BUG-158 first. Atlas algorithm/cost work and
  `BUG-097/159/160` remain outside this repair.

## Goal

- Make an imported mesh visible, selectable, and geometry-method-ready as soon
  as its geometry-only materialization is published, while UV/texture
  enrichment continues independently with truthful progress and stale-result
  rejection.

## Non-goals

- No UV atlas algorithm or performance change (`BUG-159` and `BUG-160`).
- No removal of authored/generated UVs, normal computation, texture baking, or
  progressive status.
- No method-specific readiness exception or UI-only bypass.

## Context

- Symptom: the import executor already publishes
  `BuildRuntimeHalfedgeMeshGeometryOnly(...)`, but
  `Runtime.EditorWorkspaceSnapshots.Models.cpp` returns before resolving any
  geometry-processing entries whenever `AssetImportMeshEnrichmentState` is
  queued/running.
- Expected behavior: optional presentation enrichment may be pending without
  making the canonical geometry source unavailable. Only an operation whose
  semantic inputs are actually missing should be disabled.
- Impact: on UV-less meshes, a multi-second or pathological atlas job makes
  loading feel slower than Framework24 and prevents curvature or other
  geometry work even though topology and positions are resident.
- Existing source-signature validation must drop enrichment if a user method
  changes the mesh before the job publishes; the repair must test that race
  rather than weakening it.

## Control surfaces

- Config: unchanged; enrichment policy remains on the existing import recipe.
- UI: pending enrichment remains visible, but method buttons use canonical
  per-operation readiness rather than a blanket return.
- Agent/CLI: the same runtime operation preflight becomes available at the same
  geometry-only boundary.

## Diagnosis observations

- 2026-09-05: `cmake --preset ci` and
  `cmake --build --preset ci --target IntrinsicRuntimeContractTests` succeeded
  with Clang 23 (unsanitized). Both new regressions failed under
  `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.DirectMeshEnrichment(PendingPreservesGeometryReadiness|DiscardsCompletionAfterCurvaturePublication)$' --no-tests=error --timeout 120`.
  Only pending-state availability assertions failed. Actual curvature
  publication and stale-enrichment rejection passed, ruling out a separate
  command-level block and absent geometry inputs in the reproduced case.
  The existing snapshot early return explains the observed discrepancy.
  Full local output: `/tmp/bug-158-red-tests.log`.

## Results

- 2026-09-05: fresh `ci` configure selected Clang 23 with sanitizers off.
  The focused `SandboxEditorUi` enrichment/postprocess/texture-bake cohort
  passed 10/10. Layering, test layout, task policy, task-state links, and
  documentation links passed.
- The required `IntrinsicTests` CPU build succeeded. The default CPU gate
  completed in 95.56 seconds: 4,265 registered cases, zero failures, one
  expected GLFW LeakSanitizer control skip in the unsanitized preset. The
  task-specific import selector also passed 47/47, including its small-grid
  enrichment close test. Logs: `/tmp/bug-158-recovery-cpu-full.log` and
  `/tmp/bug-158-recovery-cpu-import.log`.
- The `ci-vulkan` focused smoke
  `RuntimeSandboxAcceptanceGpuSmoke.DirectMeshEnrichmentPendingRendersGeometryOnlyImport`
  passed 1/1 in 6.70 seconds on NVIDIA GeForce RTX 3050, driver 590.48.01,
  with ASan+UBSan enabled. It checks the red center pixel against background,
  off-origin focus, source/component/selection state, pending enrichment, and
  curvature readiness before any enrichment completion can be drained.
  Log: `/tmp/bug-158-recovery-vulkan-focused.log`.
- The full `ci-vulkan` `IntrinsicTests` build succeeded. The 10 focused
  enrichment/postprocess/texture-bake regressions also passed under its
  ASan+UBSan instrumentation (`--parallel 1`; 5.46 seconds total).
  Log: `/tmp/bug-158-recovery-sanitized-focused.log`.
- The full GPU/Vulkan gate passed 55/55 with zero skips in 464.04 seconds,
  including the pending-enrichment readback and the shutdown LeakSanitizer
  contract with its engine-leak negative control.
  Log: `/tmp/bug-158-recovery-vulkan-full.log`.
- W2 stays open for representative small/large matched evidence; this fix
  adds bounded regression coverage, not a latency or parity claim.

## Required changes

- [x] Remove the blanket geometry-processing model return on active direct-mesh
      enrichment and derive each action from its real canonical inputs.
- [x] Keep enrichment status/progress/diagnostics visible while actions are
      available.
- [x] Prove a geometry mutation while enrichment is pending invalidates the
      stale enrichment publication instead of overwriting the user's result.
- [x] Preserve base-geometry visibility, selection, focus, dirty-state, and
      failure behavior when enrichment later succeeds, fails, or is cancelled.

## Tests

- [x] Add a contract regression with a blocked enrichment job asserting
      curvature and compatible geometry actions are available immediately.
- [x] Add a race regression that mutates the mesh before completion and asserts
      stale UV/normal enrichment cannot replace the newer property/topology
      generation.
- [x] Add or extend promoted-Vulkan smoke evidence that the geometry-only mesh
      remains rendered while enrichment is pending.
- [x] Default CPU and opt-in `gpu;vulkan` gates stay green.

## Docs

- [x] Update the import-progress/readiness prose to distinguish base geometry
      readiness from optional presentation enrichment.
- [x] Update the product scorecard evidence when the live workflow passes.

## Acceptance criteria

- [x] A UV-less imported mesh can run curvature before atlas completion.
- [x] Enrichment progress remains truthful and no completed job clobbers a
      newer user mutation.
- [x] No method-specific bypass or duplicate availability rule is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests --parallel 2
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R 'DirectMeshEnrichment|SandboxEditorMeshMethods|AssetImport' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests --parallel 2
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'SandboxEditorUi\.(DirectMeshEnrichment|DirectMeshPostProcess|TextureBakeControlsReportUvSources)' \
  --no-tests=error --timeout 120 --parallel 1
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan \
  --no-tests=error --timeout 120 --parallel 1
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- No waiting for atlas completion on the main/editor thread.
- No disabling all geometry actions from one presentation-enrichment bit.
- No publishing a stale enrichment result after canonical geometry changes.

## Delivery

- Local implementation checkpoint: `f131d1c4f`.
- Automatic approval review rejected `git push --set-upstream origin
  codex/bug-158-ready-during-enrichment`: explicit authorization is required
  to send this private branch to `https://github.com/intrinsicD/IntrinsicEngine.git`.
  No push occurred or was retried. The completed branch remains local pending
  operator approval; it also retains the three earlier local planning commits.
