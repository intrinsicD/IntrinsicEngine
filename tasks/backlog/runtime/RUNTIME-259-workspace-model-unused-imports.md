---
id: RUNTIME-259
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
contract_schema: 1
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-259 — Remove unused workspace model imports

## Goal
Remove eight unused explicit imports from the existing workspace model unit,
preserving its complete body and the canonical diagnostic owners.

## Non-goals
No body/header/API change, new helper/module/file, broad import sweep,
compiler migration, compatibility path or elapsed-time speedup claim.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after BUILD-008
is retired, sealed, claim-released and all writers/builds stop. Standing source
sharing authorization applies. BUILD-007/C92 retain matched timing.

Claude audited candidate exports against Models.cpp and its recursive private
headers. Settled scope is eight preamble deletions only:
- Extrinsic.Core.Dag.Scheduler
- Extrinsic.Graphics.GpuWorld
- Extrinsic.Graphics.UvView
- Extrinsic.Platform.Window
- Extrinsic.Runtime.CommandBus
- Extrinsic.Runtime.EditorUiHost
- Extrinsic.Runtime.SceneSerialization
- Geometry.UvAtlas

Before editing, retain the export/name and header proof in task evidence,
including EditorUiHost's re-exported EditorWindowRegistry and CommandBus's
CommandTypeNameOf template (the latter was omitted from Claude's summary).
Check exported free functions and unqualified/ADL call sites too: qualified-name
absence alone is insufficient. Then compile the exact producer and inspect its
actual module closure to test the hypothesis. Restore any needed import and
review the narrowed diff rather than introducing workaround declarations.

The recursive private headers use only the Runtime namespace directive. Their
Extent2D is Core::Extent2D, whose direct Core.Geometry2D owner stays imported.
Graphics diagnostic and recipe names retain their explicit RenderDiagnostics,
RenderCommandRouter and RenderRecipeConfig owners. Preserve all other imports,
full EnTT and every byte of the complete body. Keep Geometry.Graph and
GeometrySourcesPopulate together unchanged; no conditional ninth removal.
Other candidate imports remain unproven and outside this task.

The read-only Claude plan is
/tmp/intrinsic-overnight-20260915/build008/claude-plan-next.txt.
Root overrides its stale build suggestion: use the existing supported preset
Clang 23 trees and actual compile_commands producer, not an unverified
ci-clang20 tree or nonexistent module-named target. GCC/MSVC are not substitute
verification under the repository contract. No cross-compiler claim is made.
No dependency or rebuild-impact decrease is assumed until measured from the
rebuilt compiler map; removed direct imports may remain transitively reachable.

Reuse/right-sizing: remove unused consumers of existing modules; retain actual
owners. No facade replacement or new abstraction is required. Architecture docs
remain accurate if only these private imports change; record structural facts
and review in this task/evidence rather than adding a README history paragraph.

## Required changes
- [ ] Verify the eight candidate export surfaces and recursive-header/body uses, preserving baseline source and compiler dependency evidence.
- [ ] Delete only the eight proven import lines; retain any candidate the compiler/owner audit shows is needed.
- [ ] Prove that all other bytes and production files are unchanged; record actual module closure before/after without inferring elapsed-time improvement.

## Tests
- [ ] Rebuild the actual Models.cpp producer through the supported ci preset and run focused editor/model/locality coverage.
- [ ] Build IntrinsicTests and run the full supported CPU gate.
- [ ] Run existing focused editor cases under ci-asan and ci-ubsan and compile the promoted-Vulkan runtime target.

## Docs
- [ ] Bind fixed-source Claude review, structural facts, gate receipts and any narrowed scope.
- [ ] Retire and seal the completed slice, update session brief and retirement index; existing architecture remains accurate.

## Acceptance criteria
- [ ] Production diff contains only proven unused import-line deletions in Models.cpp; all bodies, headers and other production files are identical.
- [ ] Actual compiler dependencies are recorded, canonical diagnostic owners retained, and relevant native/sanitizer/build gates pass.
- [ ] Claude reviewed the fixed diff; task is locally committed, retired and sealed with no timing, GPU execution or whole-engine completion claim.

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
- Removing other imports, bodies, includes, declarations or source comments.
- Adding forward declarations or relying on an unexplained transitive owner to force compilation.
- Claiming a removed direct import necessarily leaves the full module closure.
- Changing layer policy, public APIs, test selectors, feature behavior or cache/backend identities.
- Pushing or starting implementation after the deadline reserve begins.
