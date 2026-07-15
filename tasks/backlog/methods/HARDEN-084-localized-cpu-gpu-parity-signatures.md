---
id: HARDEN-084
theme: I
depends_on: [REVIEW-003, METHOD-014, METHOD-026]
---
# HARDEN-084 — Localized CPU/GPU parity signatures

## Goal

- Test whether deterministic, spatially localized signatures make real
  CPU/GPU divergence easier to detect and localize in two concrete method
  consumers than their existing global parity metrics, and factor shared code
  only if both consumers justify it.

## Non-goals

- No generic parity framework, engine-wide instrumentation service, signature
  registry, telemetry pipeline, or new backend interface at task start.
- No replacement of canonical CPU-reference comparisons, full-output tests,
  method-specific quality metrics, or validation-layer diagnostics.
- No changes to algorithm outputs or tolerances merely to make signatures
  agree.
- No renderer/framegraph integration, shader compiler path, artifact framework,
  or dependency on the unrelated single-kernel Slang pilot.

## Context

- Owning scope: hardening/evidence around the two concrete method consumers
  `METHOD-014` (progressive Poisson CPU/Vulkan parity) and `METHOD-026`
  (parameterization-family CPU/Vulkan parity). Both must be available before
  this task starts. Bind each result to the backend identity, shader
  entry/binary hash or equivalent compiled-artifact identity, and method
  version already owned by that consumer; a missing identity is fixed in the
  owning method task rather than through a new cross-method artifact seam.
- Promotion is gated by `REVIEW-003`. This is deliberately post-stabilization
  diagnostic work, not architecture foundation.
- Existing aggregate L2/Linf/error and method-specific metrics answer whether
  outputs differ but can hide where divergence begins. The hypothesis is that
  deterministic per-partition counts, moments, residual summaries, and keyed
  sketches can identify the affected spatial/chart region without retaining a
  full second diagnostic copy.
- CPU reference remains canonical truth per the method workflow. A matching
  signature is never proof of output equality; full-output parity tests remain
  authoritative.

## Slice plan

- **Slice A — Poisson-local prototype.** Add test/benchmark-local signatures to
  `METHOD-014`, inject controlled regional corruptions, and compare detection,
  localization, bytes, and runtime against its existing global diagnostics.
- **Slice B — parameterization-local prototype.** Independently repeat the
  experiment for `METHOD-026` using chart/UV-domain partitions and injected
  vertex/triangle/residual corruptions.
- **Slice C — promotion decision.** Only if both prototypes localize genuine or
  injected divergences better than existing metrics, factor the exact common
  plain record/free-function code into the smallest shared test/benchmark
  utility. Otherwise keep useful local diagnostics local and record that no
  generic abstraction is justified.

## Right-sizing

- Two positive concrete consumers are the mandatory reintroduction trigger for
  shared code. One positive and one negative result does not justify a generic
  framework.
- Consumer-local prototypes may duplicate a small amount of experimental code;
  premature abstraction is a larger risk than short-lived test-only duplication.
- Even after two positive results, prefer a non-polymorphic helper in benchmark
  or test support. A production module requires an additional live engine
  consumer and a separate task.

## Backends

- Backend axis: compare each method's canonical CPU reference with its existing
  Vulkan compute backend. This task introduces no backend implementation.

## Required changes

- [ ] Inventory the exact existing global parity metrics, result diagnostics,
      partition data, and backend identities in `METHOD-014` and `METHOD-026`;
      freeze them as baselines before adding signatures.
- [ ] Define a Poisson-local prototype over deterministic spatial cells using
      the smallest useful tuple of count, coordinate/value moments, min/max or
      residual summaries, and keyed checksum/sketch fields.
- [ ] Define an independent parameterization-local prototype over chart or
      deterministic UV/surface partitions using comparable count/moment,
      energy/residual, flip/sign, and keyed checksum/sketch fields.
- [ ] Bind every signature result to method, requested/actual backend, dataset,
      parameters, partition definition/version, seed, and that consumer's
      existing shader entry plus binary hash or equivalent compiled-artifact
      identity. Do not add a shared compiler/artifact service.
- [ ] Add deterministic corruption injectors in test/benchmark support that
      alter one known region at a time without changing production algorithm
      code.
- [ ] Apply fixed success criteria per consumer: authoritative parity metrics
      still detect every declared injection; at least one of the signature's
      top three candidates is the true partition or an immediate graph/
      partition neighbor for every injection; false negatives and
      no-corruption false alarms are zero;
      and at most 1% of uncorrupted partitions are false-positive candidates
      over the full fixture set.
- [ ] Require zero observed collisions/non-unique signature payloads for
      distinct canonical injected cases, signature storage `<= 10%` of the full
      retained comparison output, and median signature computation runtime
      overhead `<= 10%` of the existing full-output parity comparison under the
      same fixture/backend conditions. Any cap failure is a negative result even
      when global L2/Linf catches the defect.
- [ ] Add stable benchmark IDs
      `geometry.progressive_poisson.localized_parity_signatures.smoke` and
      `geometry.parameterization.localized_parity_signatures.smoke`. Use allowed
      manifest metrics and place localization, detection, partition, collision,
      signature-byte, and backend/artifact data in diagnostics.
- [ ] After both consumer decisions, either factor only the proven common
      record/free functions or explicitly record why the prototypes remain
      local/no shared surface is created.

## Tests

- [ ] For each consumer, verify signature determinism across repeated runs and
      stable partition ordering independently of thread/dispatch ordering.
- [ ] Inject single-value, contiguous-region, sign/label/flip, and small-noise
      divergences at known locations and compare global detection with local
      attribution.
- [ ] Assert top-three/radius-one localization, zero false negatives and
      no-corruption alarms, at most 1% uncorrupted-partition false positives,
      zero observed canonical-corpus collisions, `<= 10%` retained-output
      storage, and `<= 10%` median runtime overhead for both consumers.
- [ ] Verify unchanged CPU/GPU outputs produce matching signatures within the
      method's declared numeric quantization/tolerance policy; never hide raw
      parity deltas through over-coarse quantization.
- [ ] Test empty partitions, uneven partition sizes, non-finite values, overflow,
      invalid partition descriptors, and artifact/version mismatch as explicit
      failures.
- [ ] Run CPU replay tests for signature logic in the default gate and the
      existing opt-in `gpu;vulkan` parity fixtures for actual backend-produced
      outputs on a capable host.
- [ ] Run and schema-validate both benchmark results with named CPU-reference
      baselines.

## Docs

- [ ] Document each consumer's partition/signature definition and existing
      backend/shader artifact identity, what global metrics miss,
      collision/quantization limits, top-k/false-positive results, measured
      storage/runtime overhead, and pass/kill decision in its method
      documentation.
- [ ] Update `docs/methods/backend-policy.md` only with conclusions supported by
      both consumers; do not require localized signatures globally from one
      experiment.
- [ ] Update `benchmarks/geometry/README.md` with both stable IDs and injected-
      divergence fixture policy.
- [ ] Record the right-sizing promotion decision: shared helper, consumer-local
      implementations, or complete rejection.

## Acceptance criteria

- [ ] Both concrete consumers have independent, reproducible localization
      experiments against their existing global parity metrics.
- [ ] No signature result replaces full-output CPU-reference comparison or
      weakens an existing tolerance/gate.
- [ ] Shared code is introduced only if both consumers meet their predeclared
      detection/localization/storage criteria; otherwise no generic framework
      lands.
- [ ] Every result is forensically bound to backend, dataset/config, partition
      version, and the owning consumer's existing shader/binary artifact.
- [ ] Both consumers independently meet top-three/radius-one localization,
      zero-false-negative/no-alarm, 1% false-positive, zero observed collision,
      10% storage, and 10% runtime caps; otherwise the shared-helper promotion
      is rejected.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'LocalizedParitySignature|ProgressivePoisson|Parameterization|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ProgressivePoisson|Parameterization|LocalizedParitySignature' --timeout 180
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Creating a generic signature/parity service, registry, framework, production
  module, telemetry channel, or new backend abstraction before both local
  prototypes pass.
- Treating matching signatures as proof of equality or removing full-output
  parity checks.
- Weakening tolerances, modifying algorithm output, or omitting divergent data
  to make a signature match.
- Adding unrelated renderer/framegraph integration, a shader compiler/artifact
  framework, or a parallel compilation/execution path.
- Mixing mechanical file moves with semantic changes.

## Maturity

- This is diagnostic hardening evidence only. It confers neither `Operational`
  nor `ParityProven` maturity; `METHOD-014` and `METHOD-026` retain sole
  ownership of their backend maturity and authoritative full-output parity
  claims.
