---
id: GEOM-065
theme: I
depends_on: [REVIEW-003]
---
# GEOM-065 — Invariant-aware scientific-field mip pyramids

## Goal

- Determine whether CPU-generated mip pyramids optimized for the decoded
  scientific quantity preserve unit-normal direction, discrete-label
  boundaries, and signed-scalar zero sets better than conventional averaging
  at equal storage.

## Non-goals

- No texture asset payload, KTX, streaming, RHI upload, sampler, renderer,
  runtime, ECS, UI, or Vulkan integration.
- No new file, module, export, or API under `src/geometry`; this task produces
  method/evidence-local code only until a real engine consumer exists.
- No neural representation, automatic differentiation, general image library,
  arbitrary decoder callback, or mip-generation service/registry.
- No replacement of conventional texture mipmaps or default renderer policy.
- No publication/performance claim without declared baselines and validated
  quality evidence.

## Context

- Owning scope: a self-contained evidence package under
  `methods/geometry/scientific_field_mips/` plus geometry benchmarks/tests. It
  may consume public `core`/`geometry` data types but exports no engine module
  and adds no `src/geometry` surface.
- Promotion is gated by `REVIEW-003`; the task is intentionally isolated from
  asset/graphics architecture until the CPU evidence establishes a consumer.
- Nonlinear neural/material decoders motivate measuring error after decoding
  rather than averaging raw channels. Relevant prior work includes
  [NeuMIP](https://cseweb.ucsd.edu/~viscomp/projects/NeuMIP/assets/neumip_final.pdf)
  and [Random-Access Neural Compression of Material Textures](https://research.nvidia.com/publication/2023-08_random-access-neural-compression-material-textures).
  This task transfers the measurement principle to scientific fields and makes
  no neural-compression or novelty claim.
- Existing `RHI.TextureUpload` can describe full mip chains, while current
  promoted asset/cache paths intentionally do not require multi-mip payloads.
  This task must not use that dormant transport capability as justification for
  cross-layer implementation.
- `GEOM-060` high-dimensional filtering may become a later optimized
  implementation only after positive reference evidence; it is not a
  prerequisite for the smallest CPU experiment.

## Slice plan

- **Slice A — private CPU evidence.** Define explicit 2D fixtures and compare
  conventional and invariant-aware pyramid construction without adding a
  public module.
- **Slice A decision gate.** If semantic error does not improve under the
  predeclared non-regression rule, record the negative result and stop; do not
  add asset/graphics or generalized field APIs.
- **Slice B — evidence-local CPU contract.** Only after Slice A passes, retain
  three explicitly named method-local functions/records for the proven
  semantics; do not add an engine module or public geometry API.
- **Slice C — benchmark evidence.** Add smoke and bounded comparison cases;
  future GPU/asset consumption requires a separate reviewed task.

## Right-sizing

- The only supported semantics are `UnitNormal`, `DiscreteLabel`, and
  `SignedScalar`, represented by three explicitly named method-local
  records/functions rather than one tagged generic field type, polymorphic
  decoder, callback, or image graph.
- Keep the first implementation serial, deterministic, and correctness-first.
  Do not add a backend selector until a second implementation exists.
- Reintroduction trigger: a separately reviewed asset, renderer, or scientific
  consumer must cite positive evidence and an exact required contract before
  any code moves into `src/geometry` or gains a public module surface.

## Backends

- Backend axis: deterministic CPU reference only. No optimized/GPU or asset
  consumer is owed by this evidence task.

## Required changes

- [ ] Define checked-in/generated analytic fixtures for a smoothly rotating
      unit-normal field with a sharp crease, discrete labels with thin regions
      and diagonal boundaries, and signed scalar fields with circles, narrow
      channels, and near-touching zero sets.
- [ ] Implement conventional baselines explicitly: component averaging plus
      normalization for normals, declared majority/box policies for labels,
      and box averaging for signed scalars.
- [ ] Implement deterministic invariant-aware CPU candidates using the smallest
      per-block/local search necessary to minimize the declared decoded loss;
      cap work and return explicit diagnostics on infeasible/ambiguous blocks.
- [ ] Freeze byte-identical level/storage budgets and these per-semantic primary
      metrics over the checked-in held-out fixture/level pairs: mean P95 angular
      error for `UnitNormal`, mean boundary IoU for `DiscreteLabel`, and mean
      symmetric zero-set Hausdorff distance measured in source-texel diagonals
      for `SignedScalar`.
- [ ] Freeze the positive thresholds: `UnitNormal` passes with at least 15%
      relative reduction in its primary angular error; `DiscreteLabel` passes
      with at least `+0.05` absolute boundary-IoU improvement; `SignedScalar`
      passes with at least 20% relative reduction in zero-set Hausdorff distance.
- [ ] Apply the two-of-three killing rule without post-hoc metric selection: at
      least two semantics must meet their positive threshold. The remaining
      semantic must satisfy its fixed guardrails—normal angular error increases
      by at most 5%; label boundary IoU and thin-label recall each decrease by
      at most `0.02`; or scalar zero-set distance increases by at most 5% and
      sign-disagreement rate by at most `0.005` absolute. Any byte/storage
      mismatch or guardrail failure kills promotion.
- [ ] If Slice A passes, retain only three method-local explicit functions with
      plain dimensions/spans, owned pyramid levels, semantic-specific
      params/results, backend identity `cpu_reference`, and structured
      diagnostics under `methods/geometry/scientific_field_mips/`.
- [ ] Add stable benchmark IDs
      `geometry.scientific_field_mips.reference.smoke` and
      `geometry.scientific_field_mips.comparison`. Use allowed manifest metrics;
      emit per-level semantic errors, storage bytes, operation counts, ambiguity
      counts, and baseline deltas as diagnostics.

## Tests

- [ ] Test analytic constant fields, exact 2x2 reductions, odd dimensions,
      non-power-of-two extents, one-texel axes, and full chains to 1x1.
- [ ] Test unit-normal length/direction preservation, crease behavior, and
      antipodal/zero-sum ambiguity diagnostics.
- [ ] Test label boundary/thin-region preservation with deterministic tie
      handling and explicit invalid-label behavior.
- [ ] Test signed-scalar sign and zero-set behavior, including all-positive,
      all-negative, exact-zero, narrow-channel, and near-touching fixtures.
- [ ] Reject zero extents, arity/byte mismatches, non-finite normals/scalars,
      invalid parameters, overflowed storage calculations, and cap exhaustion
      without partial success.
- [ ] Compare all invariant-aware results to the declared conventional
      baselines at byte-identical storage; assert the per-semantic numeric
      thresholds/guardrails and run/schema-validate both benchmark IDs.

## Docs

- [ ] Document field semantics, loss definitions, baseline policies, ambiguity
      handling, numeric killing-test result, complexity/storage bounds, and
      limitations in the method/evidence package; do not advertise an engine
      geometry capability.
- [ ] Update `benchmarks/geometry/README.md` with stable IDs, fixture policy,
      quality-metric mapping, and baseline definitions.
- [ ] Confirm the generated module inventory is unchanged because this task adds
      no public module.
- [ ] Record explicitly that positive CPU evidence does not mean asset/RHI or
      visible-renderer support exists.

## Acceptance criteria

- [ ] All three field classes have deterministic analytic fixtures, conventional
      baselines, frozen primary metrics, numeric guardrails, and fail-closed
      invalid-input tests.
- [ ] The held-out equal-storage comparison yields a frozen pass or kill result
      under the 15% normal, `+0.05` label, and 20% scalar thresholds plus the
      declared non-regression guardrails.
- [ ] Pass or fail, the task leaves no public field framework and opens no
      asset/RHI integration task automatically.
- [ ] A passing slice delivers only `CPUContracted` method/evidence-local
      functions and validated benchmark evidence; `src/geometry`, renderer
      defaults, and layering remain unchanged.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'ScientificFieldMip|FieldPyramid|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Adding or modifying asset payloads, KTX/mip ingest, GPU residency, RHI,
  renderer sampling, runtime scheduling, ECS, UI, or Vulkan code.
- Adding or modifying `src/geometry`, exporting an engine module/API, or moving
  the evidence-local functions into production code without a separate task
  and present consumer.
- Adding neural/AD dependencies or a generic image/decoder/backend framework.
- Opening integration work before the held-out CPU evidence passes and a new
  review explicitly accepts that scope.
- Changing conventional texture-mipmap defaults.
- Mixing mechanical file moves with semantic changes.

## Maturity

- Target after positive evidence: `CPUContracted` for the method/evidence
  package only; this task confers no engine geometry capability.
- The evidence-local CPU endpoint is intentional; no `Operational` follow-up
  is owed.
