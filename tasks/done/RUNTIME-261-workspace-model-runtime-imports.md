---
id: RUNTIME-261
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T04:26:16Z"
contract_schema: 1
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-261 — Remove unused workspace runtime imports

## Goal
Remove six unused runtime imports from Models.cpp, retaining canonical
availability, processing-operation, selection and event owners.

## Non-goals
No body/header/API change, new helper/file, other import cleanup, compiler
migration, changed backend availability or measured compile-time claim.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after RUNTIME-260
is retired, sealed, claim-released and all writers/builds stop. Standing
source-sharing authorization applies. BUILD-007/C92 retain matched timing.

Claude's bounded export/body/private-header audit selects six imports only:
- Extrinsic.Runtime.ClusteringModule
- Extrinsic.Runtime.MeshPrimitiveView
- Extrinsic.Runtime.ProgressivePoissonGpuBackend
- Extrinsic.Runtime.ParameterizationConfig
- Extrinsic.Runtime.ProgressivePoissonConfig
- Extrinsic.Runtime.SceneInteractionModule

Record the complete exported-name and re-export proof before editing.
ClusteringModule re-exports ClusteringTypes and GeometryAvailability; the former
also re-exports GeometryAvailability, whose direct import stays. Parameterization
and ProgressivePoisson config modules likewise re-export GeometryAvailability.
No candidate's API is called directly by this unit; current KMeans availability
comes from GeometryProcessingOperations, and PrimitiveSelectionResult comes
from the retained PrimitiveSelectionRefinement module. Parameterization UV-view
command-surface forward declarations in the private header are distinct from
the removed config types. Retain all other imports and every body/header byte.

Root correction to the plan: ClusteringTypes exports two ToString overloads,
so the prose claim of zero exported-name hits is too broad. The two existing
bare ToString calls take GeometryPresentationSlotSemantic/SourceKind, with exact
Runtime.GeometryPresentation overloads retained. Neither scoped enum converts
to ClusteringBackend or KMeansRunStatus. Verify these argument types and any
other overload/operator/re-export path explicitly; name absence alone is not
sufficient. Do not confuse class member assignment with namespace operators.

Keep these coupled owners: AssetWorkflowModule supplies RuntimeAssetImportEvent
named in the private bindings; SceneDocumentModule supplies RuntimeSceneFileEvent.
RenderExtraction currently re-exports VisualizationRecipes used in the body;
its removal/replacement needs a separate owner plan. PointCloudConsolidationModule
and its re-exported types remain unproven. Geometry.Graph/GeometrySourcesPopulate,
full EnTT and all other imports stay unchanged. Sibling editor units may use
these services; no repository-wide unused-API claim is implied.

Plan: /tmp/intrinsic-overnight-20260915/runtime260/claude-plan-next.txt.
Bind original plan with the above correction beside its relevant claim. Use
existing supported Clang23 preset trees and the actual Models.cpp producer via
IntrinsicRuntimeContractTests. Six direct deletions need not remove six modules
from the compiler's transitive closure. Record the measured structural count;
no timing or BMI/public-interface change. Existing architecture remains accurate.

## Required changes
- [x] Bind baseline source/map, exported types/functions/constants/reexports and recursive-header/overload proof.
- [x] Have Claude delete only the six proven import lines; preserve all remaining bytes and other production files.
- [x] Record actual compiler closure and restore any needed candidate rather than introducing a declaration workaround.

## Tests
- [x] Supported ci producer build and focused editor/locality tests pass.
- [x] IntrinsicTests build and full supported CPU suite pass.
- [x] Focused editor ASan/UBSan and promoted-Vulkan runtime compilation pass.

## Docs
- [x] Record fixed-source Claude review, owner proof and gate receipts.
- [x] Retire, locally commit/seal and update task brief/index; keep accurate existing architecture docs.

## Acceptance criteria
- [x] Only proven unused import lines change in Models.cpp; every remaining byte and other production file stays identical.
- [x] Canonical owners and feature behavior remain, with passing actual compiler/native/sanitizer/build evidence and truthful dependency counts.
- [x] Fixed diff is Claude-reviewed, locally committed, retired and sealed without timing, GPU execution or whole-engine completion claims.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests -j2
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --check-source src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp --forbid-module Extrinsic.Graphics.Renderer
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle|EditorCompilationLocality|ProcessingCompilationLocality|RenderCompilationLocality.RuntimeDiagnostics)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-vulkan --target ExtrinsicRuntime -j2
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Use existing trees with Clang 23 and disabled cache; avoid duplicate builds or
new compiled trees. No test that merely asserts deleted source text is needed.

## Forbidden changes
- Removing the coupled or unproven imports, standard includes, body/header code, comments or sibling-unit imports.
- Adding a wrapper, forward declaration, new helper/module/test file or compatibility path.
- Treating shared ToString names or re-exported declarations as unused without call/type proof.
- Changing layers, APIs, test selectors, backend/cache identities or user-facing methods.
- Pushing or starting implementation after the deadline reserve begins.

## Completion — 2026-09-15
- Endpoint: **Retired**, six unused runtime imports in Models.cpp.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-261/seal.yaml` binds the exact source revision.
- Exactly six planned import lines removed: 3,463 to 3,457 source lines,
  66 to 60 direct imports. Every remaining byte and all 876 other tracked
  production files stay unchanged. No body/header, public interface, new
  helper/file or sibling editor-unit change.
- Actual rebuilt compiler module closure decreases 171 to 161. Five removed
  direct imports leave with five further dependencies: ClusteringTypes,
  GizmoInteraction, ComputeParallelPrimitives, GpuTransfer and BufferTransfer.
  MeshPrimitiveView remains indirectly reachable. No timing claim.
- The two ToString calls retain exact GeometryPresentation overloads; the
  removed clustering overloads take distinct scoped enums and are not viable.
  Direct GeometryAvailability satisfies its re-exported names. KMeans-domain
  lookup and PrimitiveSelectionResult keep their processing/selection owners.
  Required asset/scene event owners and unproven imports remain untouched.
- Claude's first review blocked on a real evidence omission: the prefix-based
  function inventory missed individually exported
  BuildPrimitiveSelectionRenderSnapshot. Root searched the complete file and
  four recursive headers (zero hits), manually re-enumerated namespace-level
  declarations, and labeled the type inventory as conservative including
  private declarations. The original review remains recorded. Independent
  follow-up review approved the corrected evidence; no source fix was needed.
- All gates pass: exact source/compiler proof, 235 focused native tests,
  4,636 full CPU cases plus one expected GLFW/LSan control skip, 202 focused
  ASan and 202 focused UBSan cases, promoted-Vulkan runtime compilation,
  corrected owner-audit receipt and strict structural checks. Vulkan build
  evidence is not GPU execution; sanitizer coverage is focused, not full-suite.
- Existing architecture remains accurate. Clean-workshop rows 1–3 and 8 pass
  (unchanged layers, owners, public boundaries and no exception); rows 4–7
  do not apply (no frame/pass/protocol/maturity change). BUILD-007/C92,
  broader engine cleanup and Framework24 convergence remain open.
