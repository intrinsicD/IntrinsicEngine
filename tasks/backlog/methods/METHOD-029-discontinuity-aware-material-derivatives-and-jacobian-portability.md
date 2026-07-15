---
id: METHOD-029
theme: I
depends_on: [REVIEW-003, GRAPHICS-123]
maturity_target: Operational
---
# METHOD-029 — Discontinuity-aware material derivatives

## Goal

- Determine whether one footprint-aware custom derivative improves fixed-budget
  optimization through discontinuous material expressions relative to native
  Slang AD and finite differences, while remaining bounded against an analytic
  double-precision C++ oracle.

## Non-goals

- No Jacobian-portability or cross-artifact predictive study; that conditional
  follow-up is owned by `METHOD-031`.
- No MaterialX dependency, importer, arbitrary material graph, production
  material-system migration, or executable graph framework.
- No differentiable frame graph, inverse-rendering application, scene-gradient
  writeback, optimizer framework, runtime shader compiler, hot reload, UI, ECS,
  or live renderer integration.
- No claim that a custom derivative is unbiased at a mathematical
  discontinuity or useful outside the fixed corpus and footprint model.

## Context

- Owning scope: a method package under
  `methods/rendering/material_derivatives/`, containing a fixed local
  expression corpus, canonical C++ reference evaluator, and bounded offline
  Slang/Vulkan comparison path. It adds no asset or graphics-layer dependency.
- Promotion is gated by `REVIEW-003`. `GRAPHICS-123` must first prove the
  bounded offline Slang-to-SPIR-V/reflection path and Vulkan execution; this
  task may add explicitly listed corpus kernels through that path but must not
  generalize it into a shader platform.
- Slang supports forward/reverse automatic differentiation and custom
  derivatives, with restrictions around resource operations and side effects:
  [Slang automatic differentiation documentation](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/07-autodiff.html).
- Antialiased/discontinuity-aware differentiation has direct prior art, notably
  [Aδ](https://gfx.cs.princeton.edu/gfx/pubs/Yang_2022_AAF/). This task tests a
  bounded optimization hypothesis and makes no novelty claim.
- Canonical derivative truth is the analytic double-precision C++ expression
  on smooth regions, cross-checked with a central-finite-difference convergence
  sweep. Slang native/custom derivatives are comparison backends, never truth.

## Slice plan

- **Slice A — CPU corpus and oracle.** Define the fixed expression corpus,
  analytic values/derivatives, footprint semantics, and finite-difference
  sensitivity before compiling or executing Slang.
- **Slice B — offline artifact and Vulkan parity.** Compile explicitly listed
  primal/native-AD/custom-derivative entry points through the bounded
  `GRAPHICS-123` path and prove actual Vulkan value/gradient parity against the
  CPU oracle where the analytic derivative is defined.
- **Slice C — optimization killing test.** Run identical fixed-budget recovery
  problems with native AD, finite differences, and the custom derivative and
  apply the preregistered loss, stall, and bias gates.
- **Slice D — retention decision.** Record the custom derivative as positive or
  negative independently from the base-corpus/artifact verdict. Retain the base
  corpus and artifacts for `METHOD-031` only if their own validation gate passes.

## Right-sizing

- Use 10–20 checked-in, method-local pure expressions and plain evaluation
  functions. Do not introduce a graph AST, generic autodiff wrapper, material
  compiler abstraction, optimizer service, or backend registry.
- Reuse the offline compiler invocation and ordinary SPIR-V execution pattern
  from `GRAPHICS-123`. A missing capability blocks the Vulkan slice; it does not
  justify a parallel compiler/runtime path.

## Backends

- Backend axis: analytic double-precision `cpu_reference` is canonical;
  finite differences are an independent check; offline Slang native AD and the
  custom derivative execute as SPIR-V on Vulkan and report parity deltas against
  the CPU reference.

## Required changes

- [ ] Create `methods/rendering/material_derivatives/` with `method.yaml`,
      `paper.md`, expression-corpus schema, parameter units/ranges, footprint
      semantics, derivative conventions, and explicit failure diagnostics.
- [ ] Check in 10–20 bounded, textureless expressions composed only from
      constants, arithmetic, procedural coordinates, smooth nonlinear terms,
      and declared discontinuities such as `step`, branch, `fract`, checker,
      and clamped thresholds. Keep the corpus local; do not parse MaterialX.
- [ ] Implement double-precision C++ forward evaluation and analytic
      derivatives for the declared corpus, plus a central-finite-difference
      convergence sweep that records a stable step interval rather than one
      magic epsilon.
- [ ] Specify one footprint-aware custom derivative for the declared
      discontinuities, including its footprint units, limiting behavior,
      expected bias, undefined cases, and fail-closed zero/non-finite handling.
- [ ] Compile explicitly listed primal, native-AD, and custom-derivative Slang
      entry points offline to SPIR-V with deterministic reflection/artifact
      identity. Execute every compared Slang path on an operational Vulkan
      device and record backend, compiler, source, SPIR-V, and reflection hashes.
- [ ] Define the optimization killing test: recover a known UV translation and
      threshold from fixed targets using the same optimizer, initialization,
      iteration/evaluation budget, and stopping rule for native AD, finite
      differences, and the custom derivative.
- [ ] Freeze numeric gates before evaluation: the custom derivative must escape
      every declared zero-gradient stall, lower median final forward loss by at
      least 20% versus the better competing derivative on at least two
      discontinuous fixtures, introduce no greater than 5% loss regression on
      any smooth control, and remain within a declared footprint-bias envelope.
- [ ] Add stable benchmark
      `rendering.material_derivatives.custom_derivative.smoke` and a dedicated
      opt-in `IntrinsicMaterialDerivativeGpuBenchmarkSmoke` fixture that emits
      schema-valid result JSON from actual Vulkan execution. Record gradient
      errors, forward loss, stalls, bias, iterations/evaluations, backend, and
      compiler/artifact identity.
- [ ] Record two independent verdicts: `custom_derivative = adopt|reject` and
      `portability_seed = retained|rejected`. The latter may be `retained` after
      a negative custom-derivative result only when analytic/finite-difference
      agreement, CPU/Slang primal parity, native-AD parity, deterministic
      artifacts, and Vulkan execution all pass their frozen tolerances.

## Tests

- [ ] Validate analytic derivatives against finite differences on smooth
      regions and explicitly test undefined/one-sided behavior at discontinuities.
- [ ] Test the footprint-aware derivative on subpixel checker/step fixtures,
      shrinking/expanding footprints, boundary-aligned samples, zero footprint,
      and non-finite inputs.
- [ ] Default CPU tests cover corpus determinism, analytic values/derivatives,
      convergence-step selection, optimizer budget accounting, invalid
      parameters, and the frozen decision logic without requiring Vulkan.
- [ ] Opt-in `gpu;vulkan` tests execute the primal, native-AD, and custom
      derivative SPIR-V, compare each with CPU truth, and prove the requested
      backend was operational rather than skipped or substituted.
- [ ] Run the fixed-budget optimization comparison and assert the preregistered
      loss, stall, smooth-control, and bias gates.
- [ ] Run and strictly schema-validate the actual GPU benchmark result.

## Docs

- [ ] Document corpus scope, expression/footprint units, derivative semantics at
      discontinuities, finite-difference sensitivity, custom-derivative bias,
      compiler/artifact identity, Vulkan parity tolerance, and the positive or
      negative optimization verdict.
- [ ] Record `portability_seed = retained|rejected` with the exact corpus and
      artifact hashes and failed/passed validation rows. This record is the only
      start evidence `METHOD-031` may consume.
- [ ] Update `benchmarks/rendering/README.md` with the stable ID, actual-GPU
      fixture, baseline definitions, and artifact identity requirements.

## Acceptance criteria

- [ ] Every compared value/derivative is traceable to the analytic C++ oracle,
      finite-difference sweep, fixed expression, compiler identity, and SPIR-V
      artifact; actual Vulkan execution is cited.
- [ ] The custom derivative passes its frozen optimization-loss, stall,
      smooth-control, and bias gates or is retained as a documented negative
      result with no integration work.
- [ ] A negative custom-derivative verdict neither forces nor forbids
      `METHOD-031`: only the separately recorded `portability_seed` validation
      determines whether that conditional study may execute.
- [ ] No MaterialX/parser, arbitrary graph, frame graph, renderer, optimizer
      framework, runtime compiler, or production material surface is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MaterialDerivative|Slang' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicMaterialDerivativeGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -R 'MaterialDerivative|IntrinsicMaterialDerivativeGpuBenchmarkSmoke' -L 'gpu' -L 'vulkan' --timeout 180

python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/IntrinsicMaterialDerivativeGpuBenchmarkSmoke --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Adding MaterialX ingestion, an arbitrary material/expression graph, or
  silently approximating unsupported expressions.
- Adding a differentiable frame graph, scene-parameter gradient sink, inverse
  renderer, general optimizer, or production material migration.
- Treating finite differences or a generated backend as canonical truth.
- Adding a new compiler/runtime linkage path instead of consuming
  `GRAPHICS-123`, or claiming Vulkan evidence without an actually run test.
- Starting the Jacobian-portability experiment inside this task.
- Mixing mechanical file moves with semantic changes.

## Maturity

- Target: `Operational` for the bounded offline-Slang-to-SPIR-V Vulkan evidence
  path, with the expression corpus and analytic oracle `CPUContracted`.
- This proves only the named method experiment; it creates no engine renderer,
  material, or differentiable-rendering capability.
