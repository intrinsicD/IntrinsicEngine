---
id: METHOD-030
theme: I
depends_on: [REVIEW-003]
---
# METHOD-030 — Neural render proxy path-replay reference

## Goal

- Establish a deterministic imported path-record and `GatherLight` CPU replay
  oracle, then determine whether a deliberately small CPU neural proxy can
  compress that transport while preserving held-out lighting values and
  gradients well enough to justify any later rendering integration.

## Non-goals

- No engine path tracer, ray-tracing RHI work, Vulkan kernel, frame-graph pass,
  renderer integration, or runtime scene-gradient channel.
- No neural radiance cache: this task replays static light-agnostic path records
  and must not be merged with the online GI/cache scope of `GRAPHICS-049`.
- No dynamic geometry, camera, material, or visibility model; those violate the
  bounded proxy assumptions being tested.
- No ML framework dependency, training service, generic neural-network API, or
  production asset format.
- No renderer-integration follow-up unless both replay and proxy evidence are
  positive under the predeclared killing criteria.

## Context

- Owning scope: a self-contained method package under
  `methods/rendering/neural_render_proxy/`. Imported fixtures and CPU-only
  evaluation must not introduce new engine-layer dependencies.
- Promotion is gated by `REVIEW-003`; the task deliberately does not depend on
  a future engine path tracer because importing a small reference record is the
  right-sized first experiment.
- Neural Render Proxies record light-agnostic path transport once, then train a
  scene-specific network for interactive lighting evaluation and gradients:
  [Disney Research project](https://studios.disneyresearch.com/2026/07/01/neural-render-proxies-for-interactive-and-differentiable-lighting/)
  and [paper PDF](https://studios.disneyresearch.com/app/uploads/2026/06/Neural-Render-Proxies-for-Interactive-and-Differentiable-Lighting-Paper.pdf).
- Prior-art and positioning must include
  [Neural Light Transport](https://arxiv.org/abs/2008.03806) and
  [Neural Radiance Caching](https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing).
  This task tests a compact reference/evidence path and makes no novelty claim.
- The reference is static geometry, camera, and materials. Light parameters are
  the only differentiated variables.

## Slice plan

- **Slice A — paper intake and contract.** Create the method package and freeze
  the paper claim, static-scene assumptions, record units/layout, supported
  lights and differentiated parameters, provenance, dataset split, diagnostics,
  and failure states before implementing replay.
- **Slice B — path-record replay oracle.** Define and import a tiny versioned
  record, implement deterministic `GatherLight`, validate value/linearity and
  analytic/finite-difference lighting gradients, and measure storage scaling.
- **Slice C — reference correctness.** Land analytic and regression tests for
  import, replay, derivatives, malformed input, and storage bounds in the
  default CPU gate.
- **Slice D — validated reference benchmark.** Add and run the stable replay
  benchmark, validate its manifest/result JSON, and record the reference
  baseline. If any Slice B-D gate fails, retire before proxy code begins.
- **Slice E — optional small CPU proxy.** Only after Slices A-D pass,
  train/evaluate one
  fixed-family dense CPU proxy on the imported record and compare held-out
  lighting values, gradients, model bytes, and deterministic retraining.
- **Slice E decision gate.** A failed proxy is a valid negative result. A
  positive result permits a separately reviewed future consumer task; it does
  not open renderer work automatically.

## Right-sizing

- Use plain records plus free functions and one fixed small dense-network
  implementation local to the method. Do not introduce model/session/trainer
  interfaces, registries, services, asset types, or backend tokens.
- Check in only a tiny synthetic/imported fixture with documented provenance;
  no external dataset or path-tracing pipeline is required for PR-fast tests.
- Keep proxy code absent until the reference tests and benchmark result are
  committed and validated; the conditional second backend does not justify a
  framework around the reference.

## Backends

- Backend axis: deterministic `cpu_reference` replay oracle, followed only on a
  positive reference gate by one method-local `cpu_optimized` proxy whose
  diagnostic identity is `cpu_proxy_fixed_mlp` and whose value/gradient deltas
  are always reported against `cpu_reference`. No GPU, Vulkan, or renderer
  backend is owed.

## Required changes

- [ ] Create `methods/rendering/neural_render_proxy/` with `method.yaml`,
      `paper.md`, source/provenance notes, assumptions, record units/layout,
      training/evaluation split, and explicit failure states.
- [ ] Define a versioned, CPU-only imported path-record fixture containing the
      minimum path throughput/geometry terms required to evaluate parameterized
      sphere/point/directional emission without tracing new paths.
- [ ] Implement deterministic `GatherLight(record, lights)` as the canonical
      replay oracle, with stable summation order and explicit rejection of
      malformed, non-finite, unsupported-version, or material/camera-varying
      records.
- [ ] Validate replay against analytically constructed records, including
      additive-light superposition and intensity scaling. Require relative
      replay/linearity error `<= 1e-10` in double precision on the canonical
      fixtures.
- [ ] Implement analytic lighting derivatives for the supported light
      parameters and validate them against a central-finite-difference sweep;
      require maximum relative gradient error `<= 1e-4` away from documented
      nondifferentiable configurations.
- [ ] Measure serialized and resident bytes over 1x/2x/4x replicated records;
      require monotone linear scaling with no hidden quadratic allocation and
      report bytes per recorded path/vertex. Failure closes the task before
      proxy work.
- [ ] Add, run, and schema-validate
      `rendering.neural_render_proxy.path_replay.smoke` after the replay
      correctness tests pass. Its manifest/result must record the canonical
      fixture, `cpu_reference` identity, replay/gradient errors, bytes,
      path/vertex counts, diagnostics, and status.
- [ ] Record a proxy-opening checkpoint that cites a passing default CPU test
      run and the validated replay benchmark result. Only after that checkpoint
      exists may one fixed-size CPU MLP be implemented with deterministic
      initialization/training order and no external ML dependency.
- [ ] Predeclare Slice E gates: held-out forward relative L2 error `<= 5%`,
      held-out lighting-gradient relative L2 error `<= 10%`, deterministic
      repeated training within documented tolerance, and model bytes smaller
      than the replay record on the canonical fixture. Report failures without
      tuning thresholds after seeing the held-out result.
- [ ] Only after the proxy is implemented, add and run
      `rendering.neural_render_proxy.cpu_proxy.smoke`; report
      `cpu_optimized`/`cpu_proxy_fixed_mlp` identity, parity deltas against the
      replay oracle, record/model bytes, training steps, and held-out errors.

## Tests

- [ ] Test record validation, deterministic serialization/import, stable replay
      order, multiple-light superposition, zero lights, zero-contribution paths,
      and fixed-camera/material rejection.
- [ ] Test analytic record values and lighting gradients against finite
      differences across the declared stable step range.
- [ ] Test record storage scaling and cap exhaustion without partial output or
      unbounded allocation.
- [ ] Before opening Slice E, prove that all reference correctness/regression
      tests pass and the path-replay benchmark manifest and result validate.
- [ ] If Slice E opens, test deterministic initialization/training, held-out
      separation, forward/gradient errors, model serialization round-trip, and
      constant/zero-signal behavior.
- [ ] Test malformed dimensions, non-finite weights/records, unsupported record
      versions, invalid light parameters, and training divergence as explicit
      failures.
- [ ] Run and schema-validate every benchmark ID that the passing slices create.

## Docs

- [ ] Document the exact static-scene assumptions, record semantics, numerical
      summation policy, derivative domain, storage results, and decision-gate
      outcomes in the method package.
- [ ] Record the distinction from NRC and other online caches and list the
      strongest prior-art threats without claiming engine capability.
- [ ] Update `benchmarks/rendering/README.md` with stable IDs, fixture
      provenance, metric mapping, and baseline definitions.
- [ ] If any executed slice is killed, preserve a concise negative-result
      report and explicitly state that no renderer integration follows.

## Acceptance criteria

- [ ] Slice A freezes the paper/method contract before replay implementation.
- [ ] Slices B-D produce a deterministic, versioned imported-record replay
      oracle whose analytic/regression tests pass and whose replay benchmark
      manifest and result validate, or a reproducible negative result closes
      the task before proxy work.
- [ ] Slice E runs only after the passing reference test/benchmark checkpoint
      exists and either meets all held-out
      value/gradient/compression criteria or records an untuned negative result.
- [ ] No path tracer, Vulkan/framegraph/renderer path, NRC conflation, external
      ML dependency, or generic neural framework is introduced.
- [ ] No renderer-integration follow-up is opened without positive evidence
      from both the replay reference and optional proxy and a new review
      decision.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'NeuralRenderProxy|PathReplay|GatherLight|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Adding an engine path tracer, ray-tracing RHI, Vulkan shader, frame recipe,
  renderer/runtime integration, or live-scene gradient writeback.
- Reusing or extending NRC infrastructure as a shortcut for this static replay
  experiment.
- Adding an ML dependency or general model/training/backend abstraction.
- Starting replay implementation before Slice A intake/contract completion.
- Starting proxy implementation before the replay reference, correctness tests,
  and validated path-replay benchmark all pass and are cited by the checkpoint.
- Opening an integration follow-up on incomplete or negative evidence.
- Mixing mechanical file moves with semantic changes.

## Maturity

- If intake or replay is killed, retire with no promoted method surface and an
  explicit negative-result record. If Slices A-D pass but the optional proxy is
  killed or omitted, the final target is `CPUContracted` and no `Operational`
  follow-up is owed.
- If the `cpu_proxy_fixed_mlp` backend lands, the target is `Operational`; it is
  `ParityProven` only when the canonical held-out benchmark result satisfies
  every declared value, gradient, determinism, and compression threshold and
  reports deltas against `cpu_reference`. None of these states imply renderer
  or runtime capability.
