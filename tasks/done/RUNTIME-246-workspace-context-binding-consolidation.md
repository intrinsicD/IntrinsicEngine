---
id: RUNTIME-246
theme: J
depends_on: [RUNTIME-245]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive cleanup and follow-up tracking; no new performance or capability claim.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-246 — Consolidate workspace context binding conversion

## Goal
Continue the operator-authorized cleanup with Claude and retire completed work.
Replace four single-caller reverse conversions and their temporary broad records
with one direct conversion in the existing compiled owner.

## Reuse and review decision
All callers use the combined workspace context. Preserve the forward feature
adapters and shared implementation owner; no new module, helper layer or public
API is needed. Scene owns identity, geometry owns processing, visualization owns
presentation and recipe owns rendering state. Job commands fall back from
geometry to visualization; config state and callables fall back from geometry
to recipe. Availability flags, cache pointers and invalidation precedence remain
unchanged. One initial short-circuit guard pass replaces redundant checks; guard
invocation counts are not an API guarantee. Production guards read attachment
epochs. Any expired feature still makes the combined result inert.

## Acceptance criteria
- [x] Capture the verified dirty baseline and plan with Claude.
- [x] Implement one direct conversion without compatibility wrappers.
- [x] Verify regression tests against the previous implementation and final source.
- [x] Resolve independent fixed-diff review and pass relevant final gates.
- [x] Record results and retire with a concrete implementation commit.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Use CCACHE_DISABLE=1 for the open BUG-178 compiler-cache issue. Reconcile both
isolated sanitizer presets with grouped registration and serial CPU gates.
Archive exact commands and fixed review in
`build/analysis/runtime246-workspace-bindings-2026-09-14/`.

## Remaining owners
BUILD-007 owns matched engine-source compile timing; BUILD-006 owns the separate
build-backend/cache experiment. BUG-193 owns GPU pacing and watchdog margin.
BUG-178, BUG-180, BUG-188, METHOD-044, METHOD-045 and REVIEW-004 stay open.

## Results — 2026-09-14
The seven new public-model regression cases pass on the original implementation
and on the final source in all three build variants. Full native CPU: 4,606
passed and one expected ASan-only lifecycle skip (4,607 total, 125.75 seconds).
Full grouped ASan: 2,985 passed, no skips (554.10 seconds). Full grouped UBSan:
2,984 passed and the same expected lifecycle skip (2,985 total, 264.88 seconds).
These are correctness-gate durations, not matched performance measurements.

Two existing production files lose 142 physical / 132 nonblank lines; no new
production file, module or interface. All seventeen monitored compiler closures
are unchanged, including the private attachment at 18. All 1,362 frozen source
and build inputs match the final gates. Module inventory remains 418. Strict
workshop, task, layering, kernel, root, layout and skill checks pass. Source
documentation reports zero errors and three retained ownership-comment advisories.

Claude independently reviewed the fixed source and found no blockers. Root
removed redundant narrative comments. Tests exercise independent expired guards,
scene identity, job fallback and config state/callable availability. Source review
checks the remaining single-owner fields, thirteen flags, callable precedence and
invalidation ordering; no private test seam was added merely to inspect callbacks.
The initial task-format gate found RUNTIME-245's noncanonical verification heading;
root corrected it and repeated the strict gate successfully.

Scope/layering/tests/docs review passes. Clean-workshop rows 1–3 and 8 pass;
4–6 are not applicable because renderer/pass/recipe behavior does not change.
Row 7 retains named follow-ups and original limits for the retired capabilities.
No new Vulkan run or measured compile speedup is claimed.

## Completion — 2026-09-14
Completed locally and retired. Implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
BUILD-007 owns matched timings; remaining engine/product work stays open.
