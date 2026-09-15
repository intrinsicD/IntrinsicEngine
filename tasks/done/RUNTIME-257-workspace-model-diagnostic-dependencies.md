---
id: RUNTIME-257
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T03:04:29Z"
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

Claude read all 3,555 lines of
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
- [x] Replace the renderer facade import with both direct diagnostic owners.
- [x] Remove the unused mutation-template include without changing any body.
- [x] Bind before/after source hashes and actual rebuilt compiler dependencies.

## Tests
- [x] Record baseline/final body and other-production hashes; run existing compiler-boundary tool against Models.cpp with Renderer forbidden after rebuild.
- [x] Focused editor/model and locality checks, full CPU, focused ASan and UBSan pass.
- [x] Promoted-Vulkan runtime target compiles; no GPU execution claim.

## Docs
- [x] Update the existing runtime diagnostic-owner paragraph without overstating permanent CTest coverage.
- [x] Record fixed-source Claude review, relevant receipts, retirement and exact source seal.

## Acceptance criteria
- [x] Only the existing Models.cpp preamble changes in production; Renderer leaves its rebuilt module closure and no body/header changes.
- [x] Direct imports resolve all diagnostic names and existing model/renderer behavior passes relevant verification.
- [x] Completed slice is reviewed, locally committed, retired and sealed without a timing or full-engine completion claim.

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

## Completion — 2026-09-15
- Endpoint: **Retired**, implementation-only diagnostic dependency cleanup.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-257/seal.yaml` binds the exact source revision.
- Models.cpp imports RenderDiagnostics and RenderCommandRouter directly and
  drops its unused private mutation header. Its size changes from 3,555 to
  3,554 lines; every body/declaration byte from extern C++ onward is unchanged.
  All 876 other tracked production files, headers and build/test lists are
  unchanged. No new production file, module, wrapper or interface.
- The rebuilt compiler module map changes from 196 to 176 entries: Renderer and
  19 associated modules leave, none enter. This is a structural dependency
  reduction, not a compile-time benchmark. The explicit compiler-boundary check
  passes; its pre-change forbidden-Renderer failure remains optional evidence.
- Claude independently approved the fixed source subject to verification.
  All gates passed: 235 focused native editor/locality cases, 4,636 full CPU
  cases plus one expected unsanitized GLFW/LSan control skip, 202 focused ASan
  cases and 202 focused UBSan cases. Promoted-Vulkan runtime compiled. No GPU
  execution or full sanitizer-suite claim. Structural checks pass.
- Root confirmed the architecture paragraph's upstream CTest reference and
  distinguished public-interface CTest coverage from this implementation's
  per-change compiler check. Actual import ownership and body hashes are bound
  in source review and structural-counts.json; no research claim is added.
- Initial Claude CLI lacked Edit and stopped without changes. The corrected
  scoped Edit run applied both files before reaching its summary turn cap;
  root verified the exact patch and used a separate final reviewer. Neither
  CLI limitation substitutes for build/test evidence.
- Clean-workshop rows 1–3 pass (narrowed allowed imports, unchanged ownership and
  public surfaces); 4–7 are not applicable (no frame/pass/protocol/maturity
  change); row 8 passes, no exception. Existing failures, feature availability,
  lifetimes and concurrency behavior are unchanged. BUILD-007/C92 own timing.
