---
id: METHOD-025
theme: I
depends_on: [METHOD-022, RUNTIME-176, UI-036]
maturity_target: ParityProven
---
# METHOD-025 — Progressive SLIM optimized CPU backend and comparison benchmark

## Goal
- Evaluate one Progressive Parameterizations `cpu_optimized` path for SLIM.
  Expose it only when it reaches the SLIM reference solution within frozen
  tolerance, preserves injectivity, and meets a useful-speedup gate, with every
  bounded claim backed by a paired reference/optimized baseline.

## Non-goals
- No optimized ARAP path. Progressive Parameterizations targets
  foldover-free symmetric-Dirichlet minimization; ARAP optimizes a distinct
  objective. ARAP remains `cpu_reference`-only unless a separate task names a
  concrete acceleration that preserves its objective.
- No public method change: the candidate must converge to the same map/energy
  as `METHOD-022` within tolerance. If it does not, it is a distinct method
  candidate and cannot ship as this optimized backend.
- No GPU backend — that is `METHOD-026`; this task is CPU-only.
- No acceleration of ARAP or the linear strategies (LSCM/SCP/BFF); this
  optimized backend targets SLIM only.

## Context
- Paper/method: Ligang Liu, Chunyang Ye, Ruiqi Ni & Xiao-Ming Fu,
  "Progressive Parameterizations", ACM TOG 37(4), SIGGRAPH 2018. The paper
  constructs progressively less-distorted reference triangles and optimizes
  foldover-free symmetric-Dirichlet energy; it does not propose Anderson
  acceleration and is not an ARAP-objective backend.
- Owner/layer: `src/geometry`; the optimized path applies only to the typed
  `Slim` strategy alternative. This task introduces its real execution policy
  and requested/actual telemetry when the second CPU implementation lands; it
  adds no family-wide token to ARAP/LSCM/SCP/BFF.
- Backend policy: the METHOD-022 SLIM reference stays the parity oracle.
  `cpu_optimized` is explicit and strategy-scoped, with measurable fallback
  and parity diagnostics.
- Benchmark policy: per the benchmark workflow, a speedup claim requires a baseline comparison on declared fixtures; the comparison benchmark records reference and optimized runtime and the parity delta.

## Control surfaces
- Config/UI/Agent: this task extends the config/result model delivered by
  retired `RUNTIME-176` and the panel delivered by retired `UI-036` with
  `cpu_optimized` only for SLIM after the implementation exists; no
  placeholder choice is exposed beforehand.

## Backends
- Backend axis: adds SLIM `cpu_optimized` with parity to `cpu_reference`;
  `gpu_vulkan_compute` is deferred to `METHOD-026`.

## Slice plan
- **Slice A — parity/measurement contract.** Freeze aligned-UV/energy metrics,
  injectivity guards, tolerances, decline/fallback semantics, stable datasets,
  warmup/order/statistic, and baseline identity before optimization.
- **Slice B — progressive SLIM candidate.** Add the smallest paper-faithful
  progressive-reference policy and prove parity/determinism.
- **Slice C — injectivity/crossover evidence.** Preserve every SLIM guard and
  define the input-size decline policy without an ARAP or family-wide branch.
- **Slice D — paired evidence.** Run alternating reference/optimized
  measurements, validate results, and commit the baseline before any speed
  claim.

## Right-sizing
- The real second implementation justifies explicit backend selection on
  SLIM only. Use its existing typed params/result; do not add a family-wide
  backend interface, optimizer service, or acceleration registry.

## Required changes
- [ ] Add the progressive/accelerated optimized path and an explicit
      reference/optimized execution policy to `SlimParams`, reusing GEOM-064
      primitives; keep the reference path selectable.
- [ ] Parity: on the shared fixtures the optimized result matches the SLIM
      reference symmetric-Dirichlet energy and similarity-aligned UVs within a
      documented tolerance, and preserves injectivity.
- [ ] Deterministic: identical `(mesh, params, backend)` produce bitwise-identical output across runs and thread counts.
- [ ] Report `ActualBackend == cpu_optimized` (and honest fallback to `cpu_reference` when the optimized path declines an input, e.g. a mesh below a size threshold).
- [ ] If both evidence gates pass, add `cpu_optimized` and the comparison
      benchmark to the SLIM `method.yaml`; otherwise leave its backend list
      reference-only and retain only the validated negative report.
- [ ] Leave `ArapParams` and its config/UI surface without a
      `cpu_optimized` request. An unsupported ARAP/backend pair fails preview
      rather than silently running a different objective.

## Tests
- [ ] Extend `tests/unit/geometry/Test.SlimParameterization.cpp`
      (`unit;geometry`) with `cpu_optimized` parity cases: same energy/UVs
      within tolerance, injectivity preserved, backend telemetry asserted.
- [ ] Runtime/config coverage rejects `Arap + cpu_optimized` and round-trips
      `Slim + cpu_optimized` identically through Editor, AgentCli, and
      Programmatic sources when the gate passes.
- [ ] Determinism of the optimized path.
- [ ] Convergence diagnostics report progressive stages, local/global solves,
      accepted steps, and final energy under comparable stopping rules. Do not
      assert raw iteration-count superiority between algorithms with different
      work per stage; useful acceleration is decided by the paired benchmark.
- [ ] Freeze similarity alignment, UV/energy parity tolerances, and optimized
      decline/fallback rules before implementation; numeric mismatch is a
      regression, never an excuse to fall back.

## Docs
- [ ] Executable comparison manifest
      `benchmarks/geometry/manifests/slim_progressive_vs_reference_smoke.yaml`
      (`benchmark_id: geometry.slim_parameterization.progressive_vs_reference.smoke`)
      on a stable built-in SLIM dataset, with `intent: performance`,
      identical params, at least one warmup and five alternating paired
      measurements, declared median statistic, and metrics
      `runtime_ms`/`quality_error_l2`.
- [ ] Emit schema-valid results for `cpu_reference` and `cpu_optimized` and
      commit a baseline report naming commit, preset/build type, compiler,
      host, dataset, params, warmup/order/statistic, runtime deltas,
      parity/flip diagnostics, and backend identity.
- [ ] Freeze the useful-acceleration gate before implementation: SLIM must
      reach a paired median optimized/reference runtime ratio `<= 0.80` on the
      stable smoke dataset with parity intact and zero flips. A miss records
      negative evidence and leaves `cpu_optimized` unexposed.
- [ ] Update the SLIM method README backend-status table
      (`cpu_optimized` → `METHOD-025`) and note progressive-reference
      limitations (crossover size, tolerance). Record the deliberate absence
      of an ARAP optimized backend in the ARAP README.
- [ ] Update `docs/methods/index.md` and the parameterization roadmap optimized-backend note.

## Acceptance criteria
- [ ] The SLIM candidate produces a reference-parity, injective map with honest
      telemetry; a miss remains reference-only with negative evidence. ARAP is
      unchanged and exposes no optimized choice.
- [ ] The comparison benchmark validates, runs, and records reference-vs-optimized runtime and parity; any speedup statement cites it.
- [ ] Result JSON validates for both backends; any speedup statement is bounded
      to the declared dataset and measurement context.
- [ ] The exposed optimized SLIM path meets the frozen `<= 0.80` paired-median
      runtime ratio with parity/injectivity intact; a miss does not ship as an
      "optimized" choice.
- [ ] If the candidate misses either gate, retire with the validated
      comparison report and no `cpu_optimized` public choice or scaffold.
- [ ] Default CPU gate passes; layering holds (`geometry -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'SlimParameterization|Parameterization|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No solution change versus the reference beyond documented tolerance; no performance claim without the comparison benchmark baseline.
- No GPU work in this task; no `std::rand` or global RNG state.

## Maturity
- Target: `ParityProven` for the optimized SLIM CPU backend on the declared
  comparison dataset if it passes. Otherwise retire as a negative result
  without claiming this maturity.
  `gpu_vulkan_compute` is owned by `METHOD-026`.
