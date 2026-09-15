---
id: RUNTIME-253
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T00:06:17Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# RUNTIME-253 — Reuse feature config section handling

## Goal
Replace repeated Get/Set/registration mechanics across five config families with
three plain helpers inside their existing shared codec implementation.

## Non-goals
No public API, module/import, schema, validation, diagnostic, default, algorithm,
backend or user behavior change. No new source file or performance claim.

## Context
Operator-directed cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. RUNTIME-252 is retired
on main in `29d823f99`, with its evidence sealed by `c96ebd721`. Claim the clean
current main revision as the implementation baseline. Standing source-sharing authorization applies.
The settled source-reviewed proposal is three anonymous-namespace functions:
`MakeConfigSection`, `FindValidatedCanonicalPayload`, `MakeSectionRegistration`.
There are five callers of each mechanism and ten section-construction callers.
Reuse `Core::Config::FindEngineConfigSection` and `UpsertEngineConfigSection`;
retain typed family decoders and all numerical parsers/serializers verbatim.
No traits, templates, new wrappers or public helper surface. A future family
can call these helpers if it shares this exact section contract; differing
validation semantics must remain explicit.

## Slice plan
1. Add missing public-entry tests to existing `Test.SandboxConfigSections.cpp`
   and run them on the unchanged implementation before refactoring.
2. As sole writer, Claude adds three helpers to the existing codec TU and routes
   the fifteen public functions through them. Root reviews the exact diff.
3. Claude reviews frozen source; build, test, fix confirmed defects, update the
   existing owner route and retire with completion evidence.

## Required changes
- [x] Share section construction and registration, preserving field order and
  fresh defaults on every call. Move the callback once via an rvalue reference.
- [x] Share Get lookup/schema/version checks before default serialization or
  validation; pass a lazy captureless default serializer and raw validator
  function pointer, avoiding a new std::function on each Get.
- [x] Decode only `EngineConfigState::Valid`, rejecting every other state,
  including usable fallback; move the canonical payload into the result.
- [x] Keep Set as raw serialization followed by upsert, without validation.
  Preserve sorted section names and unrelated sections.

## Tests
- [x] Baseline and final checks for five-family isolation, registration schema
  triples/defaults, wrong schema/version and fallback rejection, and raw-invalid
  Set/Get asymmetry with repeated-upsert preservation of unrelated sections.
- [x] Existing config/editor integration and callbacks remain covered; add only
  missing cases instead of duplicating existing field/parser tests.
- [x] Full native CPU and focused isolated ASan/UBSan; fixed-source Claude review.

## Docs
- [x] Update the existing canonical owner-route entry and task evidence. Record
  actual production footprint with no compile-time claim; BUILD-007 owns timing.

## Acceptance criteria
- [x] Three local helpers, fifteen callers, no added production file/interface.
- [x] Public config behavior preserved under baseline and final regressions.
- [x] Verified, reviewed, locally committed and retired with sealed evidence.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j2
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(SandboxConfigSections|PointCloudConsolidationConfig|RuntimeConfigControl|SandboxEditorUi|SandboxEditorPresentation|EditorCompilationLocality|ConfigCompilationLocality)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(SandboxConfigSections|PointCloudConsolidationConfig|RuntimeConfigControl|SandboxEditorUi|SandboxEditorPresentation|EditorCompilationLocality|ConfigCompilationLocality)' --no-tests=error --timeout 60 --parallel 1
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes
No parser/validator body edits, default caching, accepting fallback in Get,
validation in Set, public/module changes, new files or weakened gates.

## Maturity
Verified structural cleanup, no backend or capability promotion. Existing
BUG-178 cache, BUG-180 leak and BUG-195 disk limitations remain. No GPU execution
change. Reuse supported Clang 23 preset trees with serial build variants.

## Review and verification progress
Claude implemented the tests and three helpers, then independently approved the
fixed source/test diff. Root removed redundant field/copy assertions and clarified
the owner route. The preserved parser/serializer/validator/request block is
byte-identical; includes/imports/public signatures are unchanged. Actual source
footprint is 2,721 to 2,701 lines (20 removed), not a timing claim. The early review
prompt used a provisional 2,710 count; the final structural artifact is authoritative.

Four new public-entry tests pass on the baseline and refactored code (10/10 config
section tests each). Focused ASan and UBSan each pass 257 cases. Existing
`BootAndLiveApplyUseTheAppOwnedRegistryThroughNullRun` supplies non-empty callbacks
for all five families through the Sandbox registry and asserts delivery; no duplicate
callback test is needed. Fresh serialization is preserved by source inspection.

Initial full CPU: 4,633 selected, 4,631 passes, one expected unsanitized skip and
one known BUG-134 timing assertion failure (`LastEndFrameMicros` 11 versus callback
12). Adapter/test sources are unchanged, and source/history establish that the
compared phases are disjoint. The existing BUG note records recurrence and the
separate repair obligation; a passing recheck will not retire BUG-134. Preserve
this receipt as a historical failed artifact and require the fresh full selector
before this cleanup can close. No test, selector, assertion or timeout is weakened.

## Verification and retirement
Completed 2026-09-15. PR/commit: enclosing retirement commit on baseline
`3ab89b073`. Endpoint: verified structural reuse, no capability promotion.

The final unchanged full CPU selector passes: 4,633 selected, 4,632 passes,
zero failures and one expected unsanitized GLFW/LSan control skip. Both isolated
sanitizer focused suites pass 257/257 after rebuilding their producers; this is
focused coverage, not full sanitizer coverage. No GPU execution path changed.
The initial BUG-134 failure remains an immutable historical artifact; the final
full-CPU receipt is the current gate, following the established RUNTIME-224/229
evidence pattern. BUG-134 stays open; its invalid cross-phase assertion is the
next separately scoped repair, with all valid rendering checks preserved.

Clean-workshop rows 1–3 pass (no new imports, links or public surfaces), 4–7 are
not applicable (no rendering/passes/recipes or maturity promotion), and 8 passes
with no new exceptions. Existing config/UI/agent behavior is preserved; the
shared rules now have one implementation. No new production file or module.
The source/test diff was independently approved by Claude; all remaining
function-pointer concerns are resolved by successful native and sanitizer builds.
The source review artifact records why extra callback/copy tests were not added.

Completion receipts, source-review scope and unchanged-block/footprint evidence
are in `tasks/evidence/RUNTIME-253/`. Final task/docs review, report and post-commit
seal bind the locally committed source. BUILD-007 and C92 remain open.
