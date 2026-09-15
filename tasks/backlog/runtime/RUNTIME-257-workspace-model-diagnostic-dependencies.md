---
id: RUNTIME-257
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
contracts: [runtime.render-diagnostics-locality]
---
# RUNTIME-257 — Import workspace model diagnostics from their owners

## Goal
Remove the renderer facade dependency and unused mutation-template include from
the existing workspace model implementation, preserving every declaration/body.

## Non-goals
No public API, body, type layout, other production file, build topology, backend,
new module/header/helper, blanket import sweep or performance claim. No removal
of potentially dead body declarations or unrelated standard headers.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. Begin after BUG-196 is
retired, sealed, claim-released and no writer/build remains. Standing source
sharing authorization applies. BUILD-007/C92 retain matched compilation timing.

Claude read all 3,556 lines of
`src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp`, its private
EditorFeatures header and three nested headers, and the mutation header.
Graphics diagnostic names are currently supplied by Renderer's re-exports:
RenderGraphFrameStats and GPU/command diagnostic records belong to
Graphics.RenderDiagnostics; RenderCommandPassStatus belongs to
Graphics.RenderCommandRouter. The latter is not re-exported by the former.
Root confirmed the current compiler graph: this TU reaches Renderer only through
its direct Renderer import. Broader removal guesses remain unapproved.

Settled edits are restricted to the existing implementation preamble:
1. Replace `import Extrinsic.Graphics.Renderer;` with direct imports of
   `Extrinsic.Graphics.RenderCommandRouter` and
   `Extrinsic.Graphics.RenderDiagnostics` beside the related graphics imports.
2. Remove `Editor/internal/Runtime.EditorMutation.Internal.hpp` and its surplus
   blank separator. Its InitialMutationState and ExecuteUndoableEntityMutation
   declarations are unused in this TU and are private to each including unit.

Keep every byte from the existing extern C++ body onward unchanged, and all
other production files exactly unchanged. Keep full EnTT, Core.Geometry2D,
Asset.Registry, DirtyTags, RHI.Profiler/QueueAffinity and other existing imports:
the body or included declarations require them, or their reachability has not
been fully resolved. No header/body refactor is approved by this note.

Reuse/right-sizing: call the existing diagnostic owners directly; no forwarding
wrapper or new compilation unit. Update only the relevant paragraph in
`docs/architecture/runtime.md`; distinguish this implementation from the public
interfaces checked by RuntimeDiagnostics. Do not imply that existing CTest
forbidden-module lists already cover Models.cpp's Renderer edge.

## Required changes
- [ ] Replace the renderer facade import with both direct diagnostic owners.
- [ ] Remove the unused mutation-template include without changing any body.
- [ ] Bind before/after source hashes and actual rebuilt compiler dependencies.

## Tests
- [ ] Record baseline/final body and other-production hashes; run existing compiler-boundary tool against Models.cpp with Renderer forbidden after rebuild.
- [ ] Focused editor/model and locality checks, full CPU, focused ASan and UBSan pass.
- [ ] Promoted-Vulkan runtime target compiles; no GPU execution claim.

## Docs
- [ ] Update the existing runtime diagnostic-owner paragraph without overstating permanent CTest coverage.
- [ ] Record fixed-source Claude review, relevant receipts, retirement and exact source seal.

## Acceptance criteria
- [ ] Only the existing Models.cpp preamble changes in production; Renderer leaves its rebuilt module closure and no body/header changes.
- [ ] Direct imports resolve all diagnostic names and existing model/renderer behavior passes relevant verification.
- [ ] Completed slice is reviewed, locally committed, retired and sealed without a timing or full-engine completion claim.

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
Use the configured producer from compile_commands.json, not similarly named
stale artifacts from earlier source locations. Keep the supported Clang 23
preset identities and cache disabled consistently. About 4.2 GiB headroom;
no new build tree. Record baseline forbidden-Renderer failure as optional
expected evidence, never a completion gate. No new test file or CMake change
is necessary for this preamble-only slice; use existing behavior tests and the
existing compiler-boundary tool with explicit task evidence.

## Forbidden changes
- Removing other imports from headline-symbol absence or changing body bytes.
- Moving, splitting or hiding interfaces, changing headers/CMake/test lists.
- Claiming a compile-time speedup from module counts or unmatched old build logs.
- Dropping current features, weakening gates, pushing or exceeding the deadline.

## Follow-up discovery
Claude observed apparently unused mutation identity/state declarations in the
body and other unproven imports. They require a separate complete consumer and
name-owner audit; they are not authorized removals in this task. The current
unordered_map alias receives its standard declaration transitively; this slice
does not change that supply. Claude's bounded plan and root's compiler-graph
cross-check are under `/tmp/intrinsic-overnight-20260915/bug196/`; the plan is
`claude-next-summary.txt`. No code from that plan has been applied yet.
