---
id: RUNTIME-261
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
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
- [ ] Bind baseline source/map, exported types/functions/constants/reexports and recursive-header/overload proof.
- [ ] Have Claude delete only the six proven import lines; preserve all remaining bytes and other production files.
- [ ] Record actual compiler closure and restore any needed candidate rather than introducing a declaration workaround.

## Tests
- [ ] Supported ci producer build and focused editor/locality tests pass.
- [ ] IntrinsicTests build and full supported CPU suite pass.
- [ ] Focused editor ASan/UBSan and promoted-Vulkan runtime compilation pass.

## Docs
- [ ] Record fixed-source Claude review, owner proof and gate receipts.
- [ ] Retire, locally commit/seal and update task brief/index; keep accurate existing architecture docs.

## Acceptance criteria
- [ ] Only proven unused import lines change in Models.cpp; every remaining byte and other production file stays identical.
- [ ] Canonical owners and feature behavior remain, with passing actual compiler/native/sanitizer/build evidence and truthful dependency counts.
- [ ] Fixed diff is Claude-reviewed, locally committed, retired and sealed without timing, GPU execution or whole-engine completion claims.

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
