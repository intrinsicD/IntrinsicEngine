---
id: RUNTIME-258
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog: removal of unreferenced anonymous-namespace declarations only, with unchanged live owners, imports, headers, public API, method/data contracts, config and executing behavior. No new or changed reusable contract."
---
# RUNTIME-258 — Remove unused copies from workspace model code

## Goal
Delete ten unused private declarations left in the workspace model implementation;
preserve the existing live implementations and every other source byte.

## Non-goals
No import/header cleanup, public API, executing algorithm, owner migration,
new file/helper/module, test/CMake change or timing claim. No global sweep.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15. Local commits only. Begin after RUNTIME-257
is retired, sealed and claim-released and its writer/builds have stopped.
Standing source-sharing authorization applies; BUILD-007/C92 retain timing.

Claude inspected the full definitions, anonymous namespace and repo-wide exact
name occurrences. Root confirmed all ten names occur exactly once in
`src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp`, only at their
unused declaration. No include consumes this cpp and no later declaration uses
these names. No object initialization, template instantiation or exported type
is removed. The canonical active copies remain in visualization/scene actions,
feature context adapters, PointFields.hpp and the workspace session.

Delete only these four complete blocks, including one trailing separator:
- EditorVisualizationMutationIdentity (nine lines).
- VertexChannelBindingMutationIdentity (eight lines).
- EditorRenderHintState, SameOptionalRenderComponent,
  EditorRenderHintMutationIdentity, EditorJobResult and EditorJobIdentityIndex,
  including the obsolete comment attached to the unused job result (42 lines).
- GeometryPresentationEditorState, GeometryPresentationMutationIdentity and
  GeometryPresentationSlotLookup (19 lines).

Against the completed RUNTIME-257 source these are lines 253–261, 739–746,
987–1028 and 1103–1121: 78 lines total, 3,554 to 3,476. Re-anchor by exact
names/content at execution, not line numbers alone. Root's proposed-block audit
is `/tmp/intrinsic-overnight-20260915/runtime257/next-dead-declarations.json`;
Claude's read-only plan is `claude-plan-next.txt` in the same directory.

The existing comment at Runtime.GeometryProcessingOperations.PointFields.hpp's
live EditorJobResult already explains diagnostic-only envelopes and dropped
jobs. Claude's concern that this rationale would be lost was checked and does
not apply; do not move or duplicate that comment. Historical audit reports stay
historical. No other apparent unused declaration/import is approved here.

Reuse/right-sizing: delete unused copies; keep canonical live owners unchanged.
Every remaining byte, including the complete preamble, must match the expected
four-block deletion. There is no need for an abstraction or absence-only test.

## Required changes
- [ ] Delete exactly the ten unused declarations in four verified blocks.
- [ ] Prove the resulting file equals the baseline minus those blocks; all other production files and public surfaces unchanged.

## Tests
- [ ] Verify exact-name non-use and source deletion proof; preserve all live owners.
- [ ] Focused editor/model/command tests, full CPU and focused ASan/UBSan pass.
- [ ] Promoted-Vulkan runtime target compiles; no GPU execution claim.

## Docs
- [ ] Record the canonical-owner audit, fixed-source Claude review and completed verification in this task/evidence.
- [ ] Retire with an exact local source seal. Existing architecture remains accurate; no README/history narrative or new source comment is needed.

## Acceptance criteria
- [ ] Exactly 78 unused production lines are removed from one existing implementation; all remaining bytes and live owners preserved.
- [ ] Reviewed source passes relevant native, sanitizer and build verification with no feature/API loss.
- [ ] Completed slice is locally committed, retired and sealed without a performance or whole-engine completion claim.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorUi|SandboxEditorWorkspaceContext|SandboxEditorSessionLifecycle|EditorCompilationLocality|ProcessingCompilationLocality)' --no-tests=error --timeout 60 --parallel 1
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
Use existing supported Clang 23 preset trees, one writer/build at a time and
consistent disabled cache. No new tree under the current disk-headroom limit.
The native focused selector includes models, history, visualization commands
and compilation boundaries. Preserve prior Renderer removal; no new timing run.

## Forbidden changes
- Removing additional imports, standard headers, functions or aliases by guess.
- Editing canonical live owners, source headers, module interfaces or test/build lists.
- Adding helper frameworks, compatibility paths or tests that only assert names are absent.
- Weakening gates, claiming matched timing, pushing or exceeding the deadline.
