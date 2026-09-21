---
id: BUILD-010
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; source diff, matched local measurements and verification are retained
contract_schema: 1
contracts: [repo.source-documentation]
---
# BUILD-010 — Compile canonical property storage instantiations once

## Goal
Reduce repeated property allocation/storage template compilation across existing
consumers without restricting arbitrary property types or changing behavior.
The operator explicitly prioritizes duplicate-code/compilation work this session.
This source optimization is separate from BUILD-006's backend comparison and
RUNTIME-270's property-binding product contract.

## Acceptance criteria
- [x] The existing Geometry.Properties implementation owns common instantiations;
      generic custom types, bool storage, type identity and revision behavior remain valid.
- [x] A diagnostic pilot proves importer instantiation suppression before the
      matched target experiment; abandon if suppression fails or owner cost dominates.
- [x] Matched Clang23 ci Debug Null/headless, cache-disabled, four-job ABBA results
      include clean runtime-contract closure, consumer edits and shared-owner edit.
      Accept >=2% clean median saving or >=3% and >=0.5 s consumer incremental saving,
      with <=2% clean regression and non-overlapping before/after ranges for the
      accepted improvement. These local samples are descriptive, not statistical
      proof. Retain negative/negligible results and source identities.
- [x] Focused behavior checks, default CPU gate, structural checks and independent
      Claude fixed-diff review pass; commit the bounded source/evidence and retire.

## Design and baseline
Baseline source: `367a38fb4`. Canonical owner: `Geometry.Properties.cppm/.cpp`.
Start with explicit `PropertyRegistry::Add<T>` and `Internal::PropertyStorage<T>`
instantiations for the nine `PropertyValueKind` types. Keep generic definitions
and all algorithms; no new translation unit, type-erasure layer or public overload.
Retained mesh/clustering traces show 0.856/0.603 s of non-overlapping Add/storage
instantiation; 118 source files use Add/GetOrAdd. JobHarness's .239 s parsing cost
ranks below this; the prior mock-lifetime experiment remains rejected (C105).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests --parallel 4
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|PropertyCoherence|PropertyBinding|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
```

## Verification checkpoint
Canonical ci IntrinsicTests builds; 226 focused cases and the full CPU selector
pass (4,863 passes, one expected GLFW/LSan skip; 159.28 s). Clang20 separately
builds/links IntrinsicGeometryTests and passes all 16 property-contract cases.
Claude Fable 5.1 fixed-diff review has no blockers. Canonical Add symbols are
weak COMDAT definitions in the owner and undefined references in inspected
consumers; no consumer storage vtables. API, module name, layouts and template
bodies remain unchanged. No new dependency edge or translation unit.
Layering, test layout, task policy, docs sync, links and workshop automation pass.
Workshop manual rows: public API direction passes; renderer/pass/recipe and
scaffold closure rows are not applicable; no temporary exceptions. Module
inventory regenerated unchanged (429 modules). No sanitizer/GPU execution claim.

## Completion — 2026-09-21
Reached CPUContracted, the intended development-cost/CPU correctness endpoint.
PR/commit: `a9e0bc149` (implementation); the enclosing evidence/retirement
commit closes the batch.
Accepted via the consumer gate: MeshMethods -4.96% (-1.198 s), ClusteringMethods
-3.97% (-0.855 s), with non-overlapping ranges. Clean -1.85% is below its 2% gate;
owner edit +10.10% (+0.513 s); scene serialization and no-op are not claimed as
useful gains. Four local non-claim-eligible results validated; Claude independently
audited the raw measurements and accepted the accounting. All 1,157 configured
commands and dependency fingerprints match. Source/test verification above covers
the exact integrated C++ diff; only evidence/task/docs changed afterward.

[Report and limits](../../ara/evidence/tables/build010_canonical_property_measurement.md)
(C106). No deferred work or new follow-up task. Other executables, Release,
sanitizer/GPU and runtime timing are outside this bounded task.
