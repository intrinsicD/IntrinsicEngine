---
id: RUNTIME-251
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural cleanup verified by compiler metadata and existing editor lifecycle tests; no timing claim.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-251 — Reuse editor snapshot context assembly

## Goal
Continue operator-directed duplication and compilation cleanup with Claude.
Reuse the existing context adapters to build snapshot contexts, remove private
query scaffolding and keep implementation dependencies out of snapshot wrappers.

## Plan and reuse
Baseline `3a35d2f63`. `EditorWorkspaceSession::Impl::PrepareFrame` and
`PrepareEditorWorkspaceSnapshotFrame` construct the same four-context aggregate
and cache borrow. Share that projection in the existing
`Runtime.EditorFeatureContextAdapters.cpp`, accepting the already-prepared
processing context. Declare it through the narrow private attachment module.
This lets the Public.cpp wrapper drop the broad editor bindings header and
imports used only by that header. Preserve preparation after guards/callbacks
and statistics publication after building the frame.

Store the immutable snapshot context directly in the query's shared pointer;
its private State holds nothing else. Move the query-access helper definition
into Public.cpp and combine its sole-caller Resolve/ContextOrEmpty chain without
exposing a new public borrow. Retain attachment checks, unbound fallback and
per-query statistics overrides. Remove only verified unused interface imports.
Do not remove the scene-interaction State: its weak callbacks, epoch validation
and shutdown responsibilities justify it. No new production files or services.

Claude supplies plan/implementation and a fixed-source review; one writer at a
time owns this checkout. The operator's standing authorization applies.
Reintroduce a separate query State only if it gains real additional ownership.
BUILD-007 remains responsible for matched compile timings.

## Review and structural evidence
Claude planned, implemented the five-file slice as sole writer, and reviewed the
fixed source diff plus regression test. Root retained the original post-callback
pointer check and corrected comments that overstated cache lifetime and header
isolation. The adapter definition is inside the existing `extern "C++"` block;
its two callers use the private attachment declaration, so no duplicate broad
header declaration is needed. The removed Resolve helper had one caller.
Per-frame bindings are freshly constructed before their statistics pointer is
installed; the review's possible stale-pointer concern does not apply.

Five production files shrink from 3,891 to 3,815 lines. The Public.cpp compiler
map drops from 169 dependencies to 101 with no added dependency. The primary
snapshot interface remains at 99: removed direct imports are still reached
through required public context types. These are structural counts, not timing
evidence. TextureBake remains a required visualization-context dependency;
the new boundary check excludes only verified removed implementation owners.
A deliberate negative control rejects the required private attachment module.

The added public-entry regression preserves a query copy after its original
context/handle are destroyed, checks separate inspector/domain statistics
overrides, and checks default models after attachment expiry. Existing session,
cache, entity-selection and presentation tests cover the shared construction.

The first focused run passed 256/257 cases, including the new query regression.
Its architecture importer list rejected the newly required context-adapter import
of the private attachment declaration. Added exactly that owning implementation to
the explicit list (28 to 29), retaining the check against every other importer;
the compiler-boundary guard separately protects the now-narrow wrapper. This is
an in-slice test expectation update, not an unrelated failure or quarantine.
Claude approved this narrow test correction in a second fixed-diff review;
the final native focused run passes all 257 cases.

## Acceptance criteria
- [x] Two snapshot context construction sites reuse one existing adapter owner.
- [x] Query scaffolding and unused dependencies removed with lifetime intact.
- [x] Fixed-diff review, CPU/sanitizer and relevant Vulkan checks pass.
- [x] Footprint/dependency counts and docs synchronized; retire completed work.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorUi|SandboxEditorSession|SandboxProcessingPanels|SandboxEditorPresentation|EditorCompilationLocality)' --no-tests=error --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```
Use existing Clang 23 ci/ci-asan/ci-ubsan/ci-vulkan/dev trees. Keep BUG-178 cache
and BUG-180 Vulkan leak limitations; no new trees under BUG-195 disk pressure.
Local review and build logs: `/tmp/intrinsic-interface-cleanup-20260915/`.

## Verification and retirement
Completed 2026-09-15. PR/commit: enclosing retirement commit on baseline `3a35d2f63`.
Endpoint: verified structural cleanup; no backend or capability promotion.
BUILD-007 retains matched full/incremental compile-time measurements.

- Canonical `ci` configured with Clang 23 and `IntrinsicTests` rebuilt. Full CPU:
  4,627 selected, 4,626 passed, zero failures, one expected unsanitized
  `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` skip.
- Focused editor UI, session, processing panels, presentation and compiler
  boundaries: 257/257 each on native, isolated ASan and isolated UBSan. Both
  sanitizer producers rebuilt after the importer-list correction. Serial CTest;
  focused sanitizer coverage is distinct from a full sanitizer gate.
- Actual Vulkan saliency/mask recovery, inspector transform pixels and selection
  outline: 3/3 passed after rebuilding the acceptance producer. Existing BUG-180
  requires `ASAN_OPTIONS=detect_leaks=0`; no leak-cleanliness claim.
- Developer `ExtrinsicSandbox` rebuilt. Strict clean-workshop, task state and
  maturity checks, links, test layout, root hygiene and skill checks passed.
  Source-documentation audit: zero objective errors, five declaration-comment
  advisories outside this cleanup. Module inventory refreshed, unchanged at 418.

Final sweep: one snapshot-context reuse intent; no new dependency-layer or
compatibility exception. Source review covered linkage, attachment expiry,
copy ownership and statistics timing. Architecture docs and canonical reuse
route are synchronized. Clean-workshop rows 1–3 pass (allowed dependency edges
and no downward type exposure), 4–7 are not applicable (no new renderer/pass/
recipe behavior or capability promotion), and 8 passes with no new exception.
Claude planned, implemented and reviewed repository source under the standing
authorization; the read-only review and narrow test-fix review found no remaining
blocking defect.
