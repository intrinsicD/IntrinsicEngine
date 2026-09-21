---
id: RUNTIME-272
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog planning; implementation evidence is the diff, tests and matched compile measurements
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# RUNTIME-272 — Compile shared property-domain test fixtures once

## Goal

Remove repeated topology construction and property-set access from eight runtime
contract tests while preserving their independent method inputs and assertions.

## Evidence and reuse decision

At `7bdffefb3`, `Properties` and the topology portion of `Make` near lines
35–75 repeat in `tests/contract/runtime/Test.NormalEstimation.cpp`,
`Test.KernelDensityOperations.cpp`, `Test.PointSpacingOperations.cpp`,
`Test.OutlierAnalysis.cpp`, `Test.KeypointAnalysisOperations.cpp`,
`Test.BilateralFilterOperations.cpp`, `Test.DensityWeightOperations.cpp` and
`Test.DescriptorAnalysisOperations.cpp` in that same directory.

Each builds a two-triangle mesh, complete four-vertex graph or five-slot point
source, then selects the requested canonical property domain. The per-method
sample values, directions, deleted samples and numerical expectations differ.
`tests/support/EditorFeatureTestContext.hpp/.cpp` already owns geometry
fixtures, but its broad header is shared by many unrelated tests. Use a narrow
`tests/support/PointDomainFixture.hpp/.cpp` pair in the existing
`EditorFeatureTestSupportObjs` support target to compile topology once without
adding domain-discovery imports to that broad header. This compilation boundary
is the concrete reason for the new files. The builder takes only `(scene, domain)`:
positions/cardinality are identical across the eight copies, so no policy flags
or speculative tuning parameters are justified. Make `Test.RegistrationDomains.cpp` the ninth consumer of the shared
property-access helper, replacing its local copy without changing its
separate registration topology setup.
Existing `AddTriangleMeshSource`, `AddGraphSource` and `AddPointCloudSource` are
not drop-in replacements: their topology or extra render components differ.
Preserve the domain fixtures' exact topology, population and absence of added
render components. UI-037 and RUNTIME-270 retain production readiness/binding
work; this task changes tests and their support only.

Operator direction on 2026-09-21 authorizes this cleanup outside the standing
Framework24 work-selection priority. All eight element domains and name/type
substitutability remain binding; no new method integration is proposed.

## Acceptance criteria

- [ ] All eight topology callers and the ninth property-access caller use the
      compiled common helpers;
      delete their copied bodies. Keep differing positions, property names,
      normals, masks, fault injection and method configs visible at each test.
- [ ] Preserve exact vertex/edge/halfedge/face counts, resized face-property slots,
      five-slot cloud setup, deleted NaN sample behavior, unrelated `keep` values,
      source revisions and history/undo expectations. Add no render components.
      Move the existing property-access const_cast unchanged; no unrelated
      const-correctness rewrite.
- [ ] Keep all existing assertions and domain/backend iterations, including
      negative preview/apply and stale/cancelled-job cases. Shared fixture setup
      does not call the method under test to construct expected answers.
- [ ] Reuse existing test-support CMake ownership; remove now-unused heavy
      imports from callers only after checking other tests in each file. Preserve
      standard-header-before-module ordering fixed by BUG-205; build the affected
      producer and runtime-contract target with supported Clang 20 as well as ci.
- [ ] Preserve existing compiler-dependency guardrails, test names, labels and
      registration. Add a concise canonical owner-route row for the new fixture,
      synchronize skill mirrors and document its narrow header boundary.

## Size and compilation acceptance

- [ ] Record before/after physical lines for the complete affected implementation,
      helper, declaration, caller and CMake set, including newly added files.
      Require a net reduction; report added regression tests separately and also
      report the total diff. Moving bodies or compressing formatting is insufficient.
- [ ] Reuse `tools/analysis/benchmark_compile_iteration.py` with a task-specific
      manifest `benchmarks/ci/manifests/runtime272_reuse_compile.yaml`, stable ID
      `build.reuse.runtime272.v1`, exact clean before/after revisions, identical
      dependencies, Clang >=20, ci-derived Null/headless preset, disabled ccache,
      fixed jobs and at least three alternating samples per arm. Use the focused
      target below with tests enabled. Measure clean,
      no-op and the following edit probes: the same migrated test implementation in both arms, and a shared fixture
      declaration edit using the runner's per-arm paths
      (before: EditorFeatureTestContext.hpp; after: PointDomainFixture.hpp).
      Explicitly report the different declaration scopes and rebuild fan-out;
      the baseline fixture header is a compile-cost comparison, not a claim
      that the duplicated topology used to live there. Also record the new
      PointDomainFixture.cpp edit cost separately.
      Record wall time, compiler-duration sum and observed rebuild fan-out.
      Stop if the clean-build median regresses by more than 2%; for no-op
      and edit probes allow at most max(2% of the before median, 100 ms).
      Apply the same relative limit to compiler-duration sums (zero-work
      no-op must stay zero); do not relax limits after viewing results. Preserve all raw runs; inconclusive evidence
      keeps this task open. On a confirmed gate failure, restore only this
      task's implementation changes, retain the raw runs and a negative decision
      record, and retire explicitly as rejected, not implemented. Do not discard
      unrelated work. Do not infer compile speed from line count.
- [ ] Run focused tests and the default CPU gate; preserve test names/labels and
      existing assertions. Validate the measured manifest/results. Any repeatable
      performance claim follows AGENTS.md §8/§8b; this task asserts no speedup.

## Verification

The compile manifest and disposable source worktree are implementation deliverables;
create the worktree from the measured revision before invoking the runner. Keep
compiler timing separate from concurrent builds/tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'NormalEstimation|KernelDensity|PointSpacing|OutlierAnalysis|KeypointAnalysis|BilateralFilter|DensityWeight|DescriptorAnalysis|RegistrationDomains|CompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci -B build/ci-runtime272-clang20 --fresh -DCMAKE_C_COMPILER=/usr/bin/clang-20 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-20 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20 -DEXTRINSIC_BACKEND=Null -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_ENABLE_CCACHE=OFF -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build build/ci-runtime272-clang20 --target EditorFeatureTestSupportObjs IntrinsicRuntimeContractTests
ctest --test-dir build/ci-runtime272-clang20 --output-on-failure -R 'NormalEstimation|KernelDensity|PointSpacing|OutlierAnalysis|KeypointAnalysis|BilateralFilter|DensityWeight|DescriptorAnalysis|RegistrationDomains' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-runtime272/source --build /tmp/intrinsic-runtime272/build --output /tmp/intrinsic-runtime272/results --manifest benchmarks/ci/manifests/runtime272_reuse_compile.yaml
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-runtime272/results/results --manifests-root benchmarks --strict
python3 tools/agents/sync_skills.py --write
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Planning review

Codex and Claude Code reached consensus on 2026-09-21 before this task was
filed. The [shared planning review](../../evidence/GEOM-099/claude-planning-review.md)
records the agreed scope, corrections, rejected job-lifecycle candidate and
compile-regression stop rules. Implementation and timing evidence remain due.
