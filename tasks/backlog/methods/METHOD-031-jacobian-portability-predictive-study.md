---
id: METHOD-031
theme: I
depends_on: [REVIEW-003, METHOD-029]
maturity_target: Operational
---
# METHOD-031 — Cross-artifact Jacobian portability prediction

## Goal

- Determine whether local parameter-Jacobian disagreement between the retained
  analytic C++ CPU executable and offline Slang SPIR-V executed on Vulkan
  predicts held-out forward divergence better than base-point forward agreement
  alone.

## Non-goals

- No new custom derivative; `METHOD-029` owns that independent hypothesis and
  its adopt/reject result is not a prerequisite for this study.
- No MaterialX importer, arbitrary material graph, additional shader language,
  runtime compiler, hot reload, renderer integration, differentiable frame
  graph, optimizer, or production portability framework.
- No claim that Jacobian agreement is sufficient for portability, transfers to
  untested compilers/devices, or proves later optimization behavior.
- No new compiler wrapper, artifact registry, backend interface, or telemetry
  service.

## Context

- Owning scope: a conditional evidence slice in
  `methods/rendering/material_derivatives/` over the exact fixed corpus retained
  by `METHOD-029`. The only compared artifacts are its independent analytic C++
  CPU executable and its offline-compiled Slang SPIR-V executed on Vulkan.
- Promotion is gated first by `REVIEW-003` and then by completion of
  `METHOD-029`. Dependency retirement alone is insufficient: this task starts
  only when the parent report records `portability_seed = retained` with corpus,
  source, executable, SPIR-V, reflection, compiler, and parity evidence.
- A rejected custom derivative does not kill this study when the validated
  analytic corpus, native Slang derivative, deterministic artifacts, and actual
  Vulkan execution survive independently.
- The hypothesis is deliberately predictive rather than a parity redefinition:
  canonical truth remains the analytic CPU result, and ordinary value/gradient
  parity failures remain failures regardless of predictor quality.

## Start/kill rule

- [ ] Before creating code, manifests, or artifacts, audit the `METHOD-029`
      report. Start only if `portability_seed = retained` names immutable corpus
      and artifact hashes and proves analytic/finite-difference agreement,
      CPU/Slang primal parity, native-AD parity, deterministic offline
      compilation, and actual operational Vulkan execution.
- [ ] If the retention marker is absent/rejected, any required artifact is
      missing, or its parity evidence is outside tolerance, write a concise
      no-start report and retire this task without a new method surface,
      compiler path, benchmark, or follow-up.
- [ ] Do not use `custom_derivative = adopt|reject` as a start condition.

## Right-sizing

- Reuse the exact parent corpus, C++ executable, Slang sources, SPIR-V,
  reflection, and Vulkan execution fixture. Add only pure comparison/statistics
  functions, fixed split records, and one evidence runner.
- Keep scalar predictors and statistical tests explicit. Do not add a learned
  predictor, generic experiment framework, graph representation, compiler
  abstraction, or backend registry.

## Backends

- Artifact axis: `cpu_analytic_executable` is canonical truth;
  `slang_spirv_vulkan` is the only comparison artifact. Every GPU result reports
  operational backend/device and exact compiler/source/SPIR-V/reflection hashes.

## Required changes

- [ ] Freeze a versioned evaluation record containing expression ID, base-point
      parameters, perturbation ID/vector/units, CPU and Vulkan forward values,
      CPU analytic and Slang-native Jacobians, artifact identities, and explicit
      invalid/non-finite diagnostics.
- [ ] Define disjoint calibration and held-out sets by expression family,
      base-point seed, and perturbation seed. No expression family or
      perturbation used to choose normalization/statistics may occur in the
      held-out report set.
- [ ] At each base point, compute the forward-only baseline score as normalized
      CPU/Vulkan value disagreement and the Jacobian score as normalized
      Frobenius disagreement; record Jacobian norm, angle, rank, and conditioning
      diagnostics without silently regularizing singular/zero cases.
- [ ] Apply fixed held-out perturbations and define the outcome as normalized
      CPU/Vulkan forward disagreement at the perturbed point. The prediction
      uses base-point information only.
- [ ] Preregister the primary statistic as held-out Spearman rank correlation
      between each score and later forward divergence. The Jacobian hypothesis
      passes only when `rho_jacobian >= 0.60`, exceeds the forward-only
      correlation by at least `0.15`, and a 10,000-resample grouped paired
      bootstrap with a fixed seed places the 95% lower confidence bound of the
      improvement above zero.
- [ ] Preregister the secondary statistic as AUROC for predicting whether later
      forward disagreement exceeds the existing parent parity tolerance. Require
      Jacobian-score `AUROC >= 0.75` and at least `0.05` improvement over the
      forward-only baseline; report confidence intervals and class balance.
- [ ] Freeze normalization, grouping, invalid-case policy, sample count, split,
      parity threshold, bootstrap seed, and statistics before executing the
      held-out Vulkan run. Report all outcomes, including negative and
      sensitivity-invalidated results.
- [ ] Add stable benchmark
      `rendering.material_jacobian_portability.comparison` and a dedicated
      opt-in `IntrinsicMaterialJacobianGpuBenchmarkSmoke` fixture emitting
      schema-valid result JSON from actual Vulkan execution. Keep correlations,
      confidence intervals, AUROC, invalid counts, and artifact identities in
      diagnostics when they are not allowed manifest metrics.

## Tests

- [ ] Default CPU tests cover record validation, normalized score arithmetic,
      zero/near-zero denominators, rank ties, grouped split disjointness,
      Spearman/AUROC hand cases, deterministic bootstrap intervals, and
      preregistered decision logic.
- [ ] Test malformed dimensions, non-finite values/Jacobians, zero-rank
      Jacobians, singular conditioning, missing artifact hashes, duplicate
      perturbations, split leakage, and insufficient class balance as explicit
      fail-closed outcomes.
- [ ] Opt-in `gpu;vulkan` tests execute the retained Slang SPIR-V at base and
      held-out perturbed points, compare it with the analytic CPU executable,
      and assert an operational Vulkan backend rather than skip/substitution.
- [ ] Prove the held-out runner cannot mutate the frozen split, normalization,
      parity threshold, statistic definitions, or bootstrap seed.
- [ ] Run and strictly schema-validate the actual GPU benchmark result.

## Docs

- [ ] Add a bounded evidence report naming the parent retention record, exact
      artifacts, parameter units, split, predictors, statistics, confidence
      intervals, sensitivity cases, and positive or negative verdict.
- [ ] State explicitly that the result compares one analytic C++ executable
      with one Slang-to-SPIR-V Vulkan artifact and establishes no general shader,
      material, renderer, compiler, or cross-device portability capability.
- [ ] Update `benchmarks/rendering/README.md` with the stable benchmark ID,
      actual-GPU fixture, split identity, baseline, and artifact requirements.

## Acceptance criteria

- [ ] The start gate either retires the task without implementation or cites a
      complete retained `METHOD-029` corpus/artifact record independent of the
      parent's custom-derivative verdict.
- [ ] Calibration and held-out expression/point/perturbation groups are
      disjoint, frozen before evaluation, and reproducible from stable IDs.
- [ ] An actually run Vulkan comparison reports the preregistered Spearman,
      paired-bootstrap, and AUROC statistics against the forward-only baseline,
      regardless of whether the hypothesis passes.
- [ ] A positive verdict satisfies every frozen numeric threshold; a negative
      or sensitivity-invalidated verdict opens no portability framework or
      renderer/compiler follow-up.
- [ ] No new compiler path, arbitrary graph, engine integration, or generic
      experiment/backend abstraction lands.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MaterialJacobianPortability|MaterialDerivative' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicMaterialJacobianGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -R 'MaterialJacobianPortability|IntrinsicMaterialJacobianGpuBenchmarkSmoke' -L 'gpu' -L 'vulkan' --timeout 180

python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/IntrinsicMaterialJacobianGpuBenchmarkSmoke --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Starting after a rejected/missing parent retention record or substituting a
  newly compiled corpus/artifact to bypass the gate.
- Using the parent's custom-derivative verdict as evidence for or against the
  Jacobian predictor.
- Tuning normalization, thresholds, split, predictors, statistics, or
  perturbations after inspecting held-out results.
- Adding MaterialX ingestion, arbitrary graphs, another compiler target,
  runtime compilation, renderer integration, or generic portability tooling.
- Treating predictor quality as a replacement for ordinary CPU-reference
  parity or as proof of causality.
- Mixing mechanical file moves with semantic changes.

## Maturity

- Target: `Operational` evidence for the bounded
  `cpu_analytic_executable`-versus-`slang_spirv_vulkan` comparison on an
  actually operational Vulkan host; CPU statistics/replay logic is
  `CPUContracted`.
- This maturity is specific to the evidence runner and artifacts. It is not an
  engine, renderer, material, compiler, or general portability capability.
