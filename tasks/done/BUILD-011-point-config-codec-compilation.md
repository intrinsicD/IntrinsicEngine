---
id: BUILD-011
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; fixed source diff, matched local measurements and verification retained
contract_schema: 1
contracts: [runtime.processing-compilation-locality, repo.source-documentation]
---
# BUILD-011 — Share point-processing config codec compilation

## Goal
Reduce repeated JSON parsing and instantiation in the ten point-processing
config implementations using the existing globally attached codec pattern.
The operator explicitly prioritizes duplication/compilation work this session.
This source batch is independent of BUILD-006 and RUNTIME-270.

## Acceptance criteria
- [x] Ten existing codecs compile in three runtime-private owners; remove the
      superseded implementation units, preserve exact function bodies, schemas,
      diagnostics and module-owned records. No forwarding wrappers or new API.
- [x] Matched ABBA target builds include complete clean closure, no-op, individual
      normal, density and descriptor codec edits, shared JSON declaration-header edits and
      the unchanged shared-helper implementation. Four jobs, cache disabled,
      Clang23 ci Debug Null/headless; retained commands and source identities.
      Accept >=15% header-edit improvement, <=2% clean regression, reduced clean
      compiler-duration sum, and <=50% individual codec-edit regression.
      Improved ranges must not overlap; local descriptive results only.
- [x] Default CPU gate, focused config tests, minimum Clang20 build/link and
      focused tests, layering/docs checks and independent Claude review pass.
- [x] Commit implementation and evidence, then retire this bounded task.

## Selection and stop rule
Selection baseline `cf60c60e7`; matched baseline `5dd61178c` includes the
independent BUG-205 minimum-compiler fixture repair in both arms. Retained BUILD-010 timings rank this family above the
remaining small project template instantiations; the EnTT attempt stays rejected.
Fresh Registration/NormalEstimation traces show about 0.98 s JSON header parsing
and 0.74 s comparison instantiation per source (nested totals are not additive).
Reuse FeatureConfigCodecs.Detail's ordinary translation unit and globally
attached declarations; retain the existing PointConfigJson shared helpers.
At selection, ten point-config sources repeated this work. Mesh curvature/geodesics
have different binding/validation owners and are outside this point cohort.
Three groups preserve neighborhood, feature and normal/registration locality.
No semantic helper cleanup is mixed into this relocation. Stop if module
attachment fails; if measured costs miss the gate, diagnose once and abandon
unless a concrete correction is supported. Do not roll into unrelated work.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests --parallel 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|Config|Registration|NormalEstimation|Density|Spacing|Outlier|Bilateral|Keypoint|Descriptor|PointConstruction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
```

## Source verification
Canonical ci IntrinsicTests builds; final default CPU gate has 4,863 passes,
zero failures and one expected GLFW/LSan skip (155.70 s). Fresh Clang20
runtime-contract and sandbox-integration executables link; 193 focused runtime
and 25 config integration cases pass. Thirty tooling tests pass. Five relocated
compiler-boundary guards retain their forbid lists and pass. Claude Fable 5.1
source and guard reviews are resolved. Layering, layout, task, links, docs sync
and workshop checks pass; manual API/layer rows pass and rendering/scaffold
rows are not applicable. Module inventory is unchanged at 429 modules.
The pre-existing fixture build failure is isolated in BUG-205 and repaired in
both measurement arms. No sanitizer/GPU execution claim.

## Completion
Retired 2026-09-21 at CPUContracted. PR/commit: `7a177d289`; evidence and retirement
are in the enclosing commit. C108's four matched samples pass every frozen gate:
shared-header 15.079 → 10.413 s, clean 407.035 → 402.482 s, and individual
codec edits 3.15–9.89% slower. Clean improvement is small and local; no broader
speedup claim. Full costs, regressions, raw samples, final checks and Claude's
independent results audit are in the [measurement report](../../ara/evidence/tables/build011_point_config_codecs_measurement.md).
No deferred work in this scope; BUILD-006 and RUNTIME-270 remain independent.
