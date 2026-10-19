---
id: BUILD-013
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
# BUILD-013 — Split template-heavy translation units and concentrate their instantiations

## Goal
Reduce total compile work, which bounds the CI build, in the heaviest translation
units without changing behavior or test assertions. Template instantiation
(JSON, `std::function`, generic helpers) and very large files are the target, not
module structure.

## Context (descriptive, local, not claim-grade)
Measured 2026-10-19 at `3302d06e3`, `build/ci` Debug, Clang 23, 16 jobs:
- The CI `full-cpu` build takes about 1,957 s for 2,650 Ninja steps on 4 vCPUs.
  The local Ninja-log compile sum is about 142 CPU-minutes, while the
  duration-weighted interface critical path is about 115 s. The build is bounded
  by total work, not by the module chain (interface work is `BUILD-014`).
- Top producers in the 2026-10-18 nightly `compile_hotspots` report:
  `Test.SandboxProcessingPanels.cpp` 104 s (5,812 lines, 63 imports),
  `Test.SandboxEditorMeshMethods.cpp` 83 s, `Test.SandboxEditorPresentation.cpp`
  76 s, `Test.SandboxEditorSessionLifecycle.cpp` 64 s, `Test.SandboxAgentServer.cpp`
  64 s, `Runtime.AgentOperations.Operations.cpp` 53 s,
  `Runtime.RenderExtraction.Internal.cpp` 52 s, `Sandbox.MeshProcessingPanels.cpp`
  41 s (5,496 lines).
- `-ftime-trace` of `Runtime.AgentOperations.Operations.cpp` (25.7 s locally):
  function instantiation 14.6 s, including `nlohmann::basic_json` 9.0 s, the local
  `ExplicitOperation` template 7.1 s and `std::function` 3.0 s. Header parsing is
  1.4 s, module loading 0.3 s and the backend 1.4 s. Nested trace totals are not
  additive.
- Reuse, do not re-derive: `BUILD-009` protocol and tooling
  (`tools/analysis/benchmark_compile_iteration.py`, `compile_hotspots.py`, schema-v2
  manifests), `BUILD-010` single-instantiation pattern and `BUILD-011` globally
  attached codec owners (`extern "C++"` declaration headers plus one ordinary
  translation unit).

## Scope
- Select producers from a fresh ranking at a frozen revision. Candidate measures:
  split oversized test files by fixture or domain, split agent operations by
  operation family, move JSON- and `std::function`-heavy generic helpers behind
  non-template functions in one owner, and add explicit instantiation where many
  units instantiate the same template identically.
- Non-goals: no behavior or assertion change; no module-interface import change
  (`BUILD-014`); no build or cache backend (`BUILD-006`, `CI-016`); no blanket
  file-size rule; no Pimpl or type erasure without a separate decision.
- Stop rule per slice: if the frozen gate is missed, diagnose once and abandon
  the slice unless a concrete correction is supported. Keep negative results.

## Acceptance criteria
- [ ] A fresh hotspot ranking at a frozen source revision names the selected
      producers, the measures for each and the stop rule before any edit.
- [ ] Timed comparisons are results-bearing: each slice follows
      `experiment-preflight` through the `BUILD-009` protocol, with a frozen
      manifest and gates (clean compiler CPU sum, clean wall time, and the touched
      file's edit scenario) set before the first timed sample.
- [ ] Split test files keep the exact case inventory (`ctest -N` name list
      before and after) and the `TestGateRouting`/`WorkflowConcurrency` source
      baselines.
- [ ] The default CPU gate, a minimum-Clang-20 build of the touched targets and
      an independent review pass; results are recorded as bounded observations
      (an ARA claim row only if a result is claimed).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests --parallel 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
