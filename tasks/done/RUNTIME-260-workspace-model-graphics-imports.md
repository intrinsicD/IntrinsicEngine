---
id: RUNTIME-260
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T04:12:49Z"
contract_schema: 1
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-260 — Remove unused workspace graphics imports

## Goal
Remove five unused graphics imports from Models.cpp while preserving its
complete implementation and direct diagnostic/component/config owners.

## Non-goals
No body, header, public API, new helper/file, broader import sweep, compiler
migration, timing claim or assertion about other editor translation units.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after RUNTIME-259
is retired, sealed, claim-released and all writers/builds stop. Standing
source-sharing authorization applies. BUILD-007/C92 retain matched timing.

Claude audited the five remaining graphics candidate interfaces against the
body and recursive private headers. Delete only these import lines:
- Extrinsic.Graphics.CameraSnapshots
- Extrinsic.Graphics.CurrentRendererContractAdapter
- Extrinsic.Graphics.GpuAssetCache
- Extrinsic.Graphics.RenderFrameInput
- Extrinsic.Graphics.RenderingContract

None re-exports another module or declares a namespace-scope operator. Exported
type names are unused; namespace-scope functions have no call sites except the
shared names ToString and AppendDiagnostics. Both ToString calls take Runtime
GeometryPresentation enums with retained canonical overloads. AppendDiagnostics
is a TU-local function taking vector<EditorDiagnostic>; both calls pass those
Runtime vectors, not Graphics::RenderingContractValidationResult. Root checks
these exact owners/call sites and records the export/header proof before edits.

Do not propagate this result to sibling workspace-session/context-adapter units:
they use these graphics APIs. Keep every other import, full EnTT, recursive
headers and all body bytes unchanged, including Geometry.Graph and
GeometrySourcesPopulate. No forward declarations or facade substitutions.
Measure the actual rebuilt module closure: five fewer direct imports need not
remove five modules (or any module) from transitive reachability.

Read-only plan: /tmp/intrinsic-overnight-20260915/runtime259/claude-plan-next.txt.
Use existing supported Clang23 preset trees and the actual compile_commands
producer through IntrinsicRuntimeContractTests, not the plan's misleading BMI
rebuild wording: no module interface changes. No unity-build, compiler or
behavior changes. Existing architecture stays accurate; task/evidence records
the structural result instead of adding README history. No new test for mere
text deletion; existing compile and behavioral gates remain required.

## Required changes
- [x] Preserve baseline source/map and verify candidate exports, recursive-header uses, ToString and AppendDiagnostics ownership.
- [x] Have Claude remove only the five proven import lines; restore a needed candidate rather than inventing a workaround.
- [x] Verify exact remaining bytes and all other production files; record actual module closure without a timing claim.

## Tests
- [x] Supported ci producer build and focused editor/locality tests pass.
- [x] IntrinsicTests build and full supported CPU suite pass.
- [x] Focused editor ASan/UBSan and promoted-Vulkan runtime compilation pass.

## Docs
- [x] Bind fixed-source Claude review, export/owner proof and final verification.
- [x] Retire, locally commit and seal; update task index/brief. Architecture remains accurate.

## Acceptance criteria
- [x] Only proven unused import lines change in Models.cpp; all remaining bytes and other production files stay identical.
- [x] Canonical owners remain and actual compiler/native/sanitizer/build evidence passes with honest dependency counts.
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
- Editing sibling units, body/header/standard includes, other imports or public interfaces.
- Adding declarations, wrappers, helper files or a speculative compile-time assertion.
- Treating name-only absence as a substitute for the shared-name call/argument proof.
- Changing test selectors, backend/cache identities, layers, APIs or user behavior.
- Pushing or starting implementation after the deadline reserve begins.

## Completion — 2026-09-15
- Endpoint: **Retired**, five unused graphics imports in Models.cpp.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-260/seal.yaml` binds the exact source revision.
- Exactly five planned import lines removed: source 3,468 to 3,463 lines,
  direct imports 71 to 66. Every remaining byte and all 876 other tracked
  production files are unchanged; no public interface, header, body, standard
  include, new helper/file or sibling editor-unit edit.
- Actual rebuilt compiler module closure decreases 172 to 171: only
  CurrentRendererContractAdapter leaves. CameraSnapshots, GpuAssetCache,
  RenderFrameInput and RenderingContract remain transitively reachable.
  This is a structural count, not a measured compilation-time improvement.
- Export/type/free-function and recursive-header proof is recorded, including
  the two shared names. ToString takes Runtime GeometryPresentation enums
  with retained direct overloads; AppendDiagnostics uses the TU-local
  vector<EditorDiagnostic> function, not the graphics validation overload.
  No candidate reexports or namespace operators. Sibling editor code that
  uses these APIs remains unchanged, as do Graph and GeometrySourcesPopulate.
- Claude implemented and independently reviewed the fixed diff. Gates pass:
  exact source/compiler proof, 235 focused native tests, 4,636 full CPU cases
  plus one expected GLFW/LSan control skip, 202 focused ASan and 202 focused
  UBSan cases, promoted-Vulkan runtime compilation and strict structural
  checks. No GPU execution or full sanitizer-suite claim.
- Existing architecture remains accurate. Clean-workshop rows 1–3 and 8 pass
  (unchanged layers, owners, public boundaries and no exception); rows 4–7
  do not apply (no frame/pass/protocol/maturity change). BUILD-007/C92,
  remaining engine cleanup and Framework24 convergence stay open.
