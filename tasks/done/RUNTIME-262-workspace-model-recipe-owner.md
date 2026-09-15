---
id: RUNTIME-262
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T04:42:43Z"
contract_schema: 1
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-262 — Import workspace visualization recipes from their owner

## Goal
Replace Models.cpp's RenderExtraction import with the existing direct
VisualizationRecipes owner, preserving every other production byte.

## Non-goals
No body/header/API/export change, new helper/module/file, other import cleanup,
compiler migration, object-byte identity requirement or timing claim.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after RUNTIME-261
is retired, sealed, claim-released and all writers/builds stop. Standing
source-sharing authorization applies. BUILD-007/C92 retain matched timing.

Settled production diff, one line in
src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:
replace import Extrinsic.Runtime.RenderExtraction;
with import Extrinsic.Runtime.VisualizationRecipes;

Claude audited the RenderExtraction export namespace and its two re-exports.
Models.cpp and the four recursive private headers use none of its own types,
functions or cache methods; retain explicit proof including its three free
functions MirrorRenderWorldPoolDiagnostics, ObserveRenderableAssetGeneration
and AcknowledgeRenderableAssetRebind. GeometryAvailability is already directly
imported. The body explicitly names VisualizationRecipe and calls
GetVisualizationRecipeKind, whose existing owner is VisualizationRecipes.
VisualizationEditingOperations imports that owner without re-exporting it;
the direct import replaces the current RenderExtraction re-export path.

RuntimeRenderRecipeState and RuntimeRenderRecipeApplyResult in the private
bindings are supplied by retained EngineConfigControl, which explicitly
re-exports RenderRecipeActivation. Root confirmed that declaration. No new
RenderRecipeActivation import is necessary; do not add one speculatively.
Keep all other imports, full EnTT, Graph/GeometrySourcesPopulate and all body,
header and sibling-unit bytes unchanged. Other editor units use extraction
and must retain their own imports.

Read-only plan: /tmp/intrinsic-overnight-20260915/runtime261/claude-plan-next.txt.
Root corrections: the used recipe/availability visibility is preserved; the
unused extraction names disappear, so this is not a visibility superset or
widened recipe overload set. Use the actual Models.cpp implementation producer
from compile_commands through IntrinsicRuntimeContractTests, not the plan's
mistaken identification of Test.VisualizationRecipes.cpp as that producer.
No interface/BMI change. Do not assert object-file byte equality or Vulkan
operational behavior: source equality and stated compile/test gates are the
verification, and debug/dependency metadata can differ legitimately.

Record actual compiler closure after rebuilding. The facade might remain
transitively reachable through other imports; do not require its absence or
claim a timing win without evidence. Reuse the existing architecture paragraph
at docs/architecture/runtime.md describing Models.cpp's direct diagnostic
owners: add one concise sentence for its direct VisualizationRecipes owner,
without changing the antecedent of the existing RenderCommandRouter sentence
or overstating the public RuntimeDiagnostics CTest coverage. No README history
or generated module-inventory change is needed for a private .cpp import.

## Required changes
- [x] Bind baseline source/map and complete own-export/reexport/private-header ownership proof.
- [x] Have Claude replace only the one import line and update the existing architecture paragraph as scoped.
- [x] Verify every remaining byte and other production file; record actual dependency change.

## Tests
- [x] Existing ci producer build, focused editor/locality and VisualizationRecipes cases pass.
- [x] IntrinsicTests build and full supported CPU suite pass.
- [x] Focused editor plus VisualizationRecipes cases pass under ASan/UBSan; promoted-Vulkan runtime compiles.

## Docs
- [x] Update the existing Models.cpp owner paragraph and preserve accurate coverage wording.
- [x] Record fixed-source Claude review, receipts, retirement, local commit/seal and task brief/index updates.

## Acceptance criteria
- [x] Production change is exactly the one-line owner substitution; all other bytes/files and existing feature behavior remain unchanged.
- [x] Recipe, availability and activation record names retain their proven owners; actual compiler/native/sanitizer/build gates pass with honest dependency counts.
- [x] Code/docs diff is Claude-reviewed, locally committed, retired and sealed without timing, GPU execution or whole-engine completion claims.

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
- Editing any other production line or sibling unit, adding a speculative activation import or changing exports/headers/public interfaces.
- Adding a wrapper, forward declaration, new helper/module/test file or compatibility path.
- Treating plain imports as re-exports or assuming all transitive extraction dependencies disappear.
- Requiring object bytes to match, claiming GPU execution from compilation, changing test selectors to avoid failures or changing backend/cache identities.
- Pushing or starting implementation after the deadline reserve begins.

## Completion — 2026-09-15
- Endpoint: **Retired**, direct visualization-recipe ownership in Models.cpp.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-262/seal.yaml` binds the exact source revision.
- One import substitution: RenderExtraction to VisualizationRecipes. Source
  stays at 3,457 lines and 60 direct imports; every other byte and all 876
  other tracked production files are unchanged. No body/header, public API,
  export, target, new helper/file or sibling-unit change.
- Actual rebuilt compiler closure decreases from 161 to 148 modules. The
  extraction facade and twelve rendering/resource-management dependencies
  leave, including GpuWorld, GpuAssetCache, RenderWorld, Material, LightSystem
  and resource managers. No elapsed-time or object-byte identity claim.
- The body explicitly uses VisualizationRecipe/GetVisualizationRecipeKind
  from their direct owner. Extraction's own types and three namespace-level
  functions have no references in the complete body/four private headers.
  GeometryAvailability stays direct; EngineConfigControl still re-exports
  RenderRecipeActivation for the private binding record types. No speculative
  activation import or change to required recipe overload visibility.
- The existing architecture paragraph now records direct recipe ownership.
  Root clarified that the per-change dependency check applies to this
  implementation, retaining the distinction from public RuntimeDiagnostics
  CTest coverage. Claude independently reviewed the fixed code/docs diff.
- All gates pass: exact source/compiler proof, 250 focused native editor,
  recipe and locality cases, 4,636 full CPU cases plus one expected GLFW/LSan
  control skip, 217 focused ASan and 217 focused UBSan cases, promoted-Vulkan
  runtime compilation and strict structural checks. The focused selectors
  include all 15 existing VisualizationRecipes cases; no new tests were needed.
  Vulkan compilation is not GPU execution; sanitizer coverage is not full-suite.
- Clean-workshop rows 1–3 and 8 pass (canonical owners, unchanged layers and
  public boundaries, no exception); rows 4–7 do not apply (no frame/pass/
  protocol/maturity change). BUILD-007/C92, remaining cleanup and Framework24
  convergence stay open.
