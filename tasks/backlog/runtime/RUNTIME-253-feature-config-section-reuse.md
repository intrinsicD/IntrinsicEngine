---
id: RUNTIME-253
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
- [ ] Share section construction and registration, preserving field order and
  fresh defaults on every call. Move the callback once via an rvalue reference.
- [ ] Share Get lookup/schema/version checks before default serialization or
  validation; pass a lazy captureless default serializer and raw validator
  function pointer, avoiding a new std::function on each Get.
- [ ] Decode only `EngineConfigState::Valid`, rejecting every other state,
  including usable fallback; move the canonical payload into the result.
- [ ] Keep Set as raw serialization followed by upsert, without validation.
  Preserve sorted section names and unrelated sections.

## Tests
- [ ] Baseline and final checks for five-family isolation, registration schema
  triples/defaults, wrong schema/version and fallback rejection, and raw-invalid
  Set/Get asymmetry with repeated-upsert preservation of unrelated sections.
- [ ] Existing config/editor integration and callbacks remain covered; add only
  missing cases instead of duplicating existing field/parser tests.
- [ ] Full native CPU and focused isolated ASan/UBSan; fixed-source Claude review.

## Docs
- [ ] Update the existing canonical owner-route entry and task evidence. Record
  actual production footprint with no compile-time claim; BUILD-007 owns timing.

## Acceptance criteria
- [ ] Three local helpers, fifteen callers, no added production file/interface.
- [ ] Public config behavior preserved under baseline and final regressions.
- [ ] Verified, reviewed, locally committed and retired with sealed evidence.

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
