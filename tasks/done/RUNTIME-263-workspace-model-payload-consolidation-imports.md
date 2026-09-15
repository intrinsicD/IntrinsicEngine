---
id: RUNTIME-263
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T04:57:51Z"
contract_schema: 1
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-263 — Remove unused workspace payload and consolidation imports

## Goal
Remove three unused imports from Models.cpp with all other production bytes
unchanged. This is the final implementation candidate for the overnight run.

## Non-goals
No other import/body/header/API/export change, new helper/module/file, compiler
migration, backend/UI behavior change or timing claim. No new slice after this
before the morning report.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after RUNTIME-262
is retired, sealed, claim-released and all writers/builds stop. Standing
source-sharing authorization applies. If the implementation cutoff has arrived,
leave this task prepared and perform final reconciliation/reporting instead.

Claude's final bounded audit selects exactly these import lines:
- Extrinsic.Asset.GeometryPayload
- Extrinsic.Asset.ModelTexturePayload
- Extrinsic.Runtime.PointCloudConsolidationModule

GeometryPayload exports IsGeometryPayloadKind, AssetPayloadTypeIdOf<T> and
AssetGeometryPayload; the functions/template/type are unreferenced in this
unit/four recursive private headers. ModelTexturePayload's exported types,
constant and ten namespace functions likewise have no references. Neither
module re-exports another module. The A alias is Extrinsic::Assets, so check
exact symbols including qualified and unqualified uses. AssetPayloadKind and
DebugNameForAssetPayloadKind are owned by retained Asset.ImportRouter, not by
these payload modules. Sibling context adapters use IsGeometryPayloadKind and
must retain their own import; no repository-wide unused claim.

ConsolidationModule re-exports ConsolidationTypes and GeometryAvailability.
Types also re-exports PointCloudConsolidationConfig. Fully inventory that
transitive config surface before edits: four scoped enums, the config struct,
three section constants, four StableToken overloads, and five codec functions
inside extern C++ (Serialize/Validate/Get/Set/Make registration). Root read the
config and found zero StableToken, codec or constant-name references in the
complete Models.cpp/four headers. Do not summarize this surface as "etc." or
repeat the earlier prefix-based omission of an individually exported function.
The module's own two free functions and the Types factory also require exact
name checks. Include ordinary export-namespace and individually exported forms;
label any conservative private-type inventory as such.

Types' ToString(PointCloudConsolidationRunStatus) shares a name with the two
existing bare calls, but their arguments are GeometryPresentationSlotSemantic
and SourceKind. The exact GeometryPresentation overloads remain imported;
the distinct scoped enums cannot convert to the consolidation status. Verify
that proof explicitly. GeometryAvailability is already directly imported, so
its re-exported names remain available. No candidate exports a namespace
operator; member assignment is not a free operator. The local AppendDiagnostics
function uses Runtime diagnostic vectors and is unaffected.

Keep all other imports, full EnTT, Graph/GeometrySourcesPopulate, all body/header
bytes and sibling units. AssetWorkflowModule/SceneDocumentModule supply needed
events; AssetWorkflowRecipePolicies, TextureBakeModule, VisualizationEditingOperations,
GeometryPresentation, GeometryAvailability and direct VisualizationRecipes remain.
No replacement import, forward declaration, wrapper or new helper is needed.

Plan: /tmp/intrinsic-overnight-20260915/runtime262/claude-plan-next.txt.
Root corrections: source baseline is 3,457 lines per the exact source proof,
not the plan's 3,458. Record the complete config surface and scoped-enum proof,
not an approximate namespace inventory. Measure the actual compiler closure;
three fewer direct imports need not remove three transitively reachable modules.
Use existing supported Clang23 preset trees and the real Models.cpp producer.
Existing architecture stays accurate; task/evidence records the result.

## Required changes
- [x] Bind baseline source/map and complete own-export/reexport/config/template/private-header proof.
- [x] Have Claude delete only the three proven import lines; preserve every other byte and production file.
- [x] Verify actual compiler closure, restore a needed candidate rather than adding a workaround, and stop implementation after this slice.

## Tests
- [x] Existing ci producer build and focused editor/recipe/locality tests pass.
- [x] IntrinsicTests build and full supported CPU suite pass.
- [x] Focused editor/recipe ASan/UBSan and promoted-Vulkan runtime compilation pass.

## Docs
- [x] Record fixed-source Claude review and verification evidence; no architecture change beyond existing accurate owners.
- [x] Retire, locally commit/seal and update task brief/index before morning reconciliation.

## Acceptance criteria
- [x] Only proven unused import lines change in Models.cpp; all other bytes and production files stay identical.
- [x] Required asset, geometry and editor owners remain, with passing actual compiler/native/sanitizer/build evidence and honest dependency counts.
- [x] Fixed diff is Claude-reviewed, locally committed, retired and sealed without timing, GPU execution or whole-engine completion claims.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests -j2
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --check-source src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp --forbid-module Extrinsic.Graphics.Renderer
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle|EditorCompilationLocality|ProcessingCompilationLocality|RenderCompilationLocality.RuntimeDiagnostics|VisualizationRecipes)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle|VisualizationRecipes)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle|VisualizationRecipes)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-vulkan --target ExtrinsicRuntime -j2
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Use existing trees with Clang 23 and disabled cache; avoid duplicate builds or
new compiled trees. No test that merely asserts deleted source text is needed.

## Forbidden changes
- Any other import, production line, header, public interface or sibling-unit edit.
- New helpers, wrappers, forward declarations, compatibility paths or export changes.
- Incomplete re-export/config/namespace-function enumeration or treating ToString name matches as automatically safe/unsafe.
- Changes to layers, test selectors, backend/cache identities, persisted data or user-facing behavior.
- Pushing, starting implementation after 07:15 Berlin or opening another implementation slice before the morning report.

## Completion — 2026-09-15
- Endpoint: **Retired**, final overnight implementation slice.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-263/seal.yaml` binds the exact source revision.
- Exactly three import lines removed: Asset.GeometryPayload,
  Asset.ModelTexturePayload and Runtime.PointCloudConsolidationModule.
  Models.cpp decreases from 3,457 to 3,454 lines and 60 to 57 direct imports.
  Every other byte and all 876 other tracked production files are unchanged.
- Actual rebuilt compiler closure decreases from 148 to 120 modules, with
  28 removals and no additions. The removed consolidation facade carried
  unrelated primitive/spatial-query and memory dependencies into this unit.
  No elapsed-time improvement is claimed; BUILD-007/C92 retain matched timing.
- Complete five-interface and four-recursive-header audit includes namespace
  functions, templates, constants, the transitive config's four StableToken
  overloads and five codec functions. AssetPayloadKind/debug naming retain
  their ImportRouter owner. Both bare ToString calls retain exact
  GeometryPresentation scoped-enum overloads; consolidation status is not
  convertible. GeometryAvailability stays directly imported. Sibling asset
  payload consumers and their imports remain untouched.
- Claude implemented only the selected deletions and independently reviewed
  the supplied fixed full source/interfaces/headers. Root verified the exact
  expected bytes and actual compiler map. Reader line-count convention differs
  by its trailing empty line; SHA-bound source and splitlines counts agree.
- All gates pass: 250 focused native editor, recipe and locality cases,
  4,636 full CPU cases plus one expected GLFW/LSan skip, 217 focused ASan and
  217 focused UBSan cases, promoted-Vulkan runtime compilation, source proof
  and strict structural checks. No new tests that only mirror deleted source.
  Vulkan compilation is not GPU execution; sanitizer coverage is focused.
- Clean-workshop rows 1–3 and 8 pass (canonical owners, unchanged layers/public
  boundaries, no exception); rows 4–7 do not apply (no frame/pass/protocol/
  maturity change). No new module, file, helper, wrapper or compatibility path.
  Existing architecture description remains accurate. No new implementation
  slice follows this before the morning reconciliation and report.
