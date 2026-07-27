---
id: RUNTIME-195
theme: F
depends_on: [RUNTIME-194]
maturity_target: Retired
---
# RUNTIME-195 — Unified GPU-result readback

## Status

- Promoted to active on 2026-07-27 after retired `RUNTIME-194` supplied the
  single `JobService` lifecycle.
- Slice A completed on 2026-07-27. The initial census found three production result
  paths: K-Means owns three `AsyncBufferReadback` instances, Progressive
  Poisson owns the only direct compute-result `IDevice::ReadBuffer` call, and
  `GpuReadbackJob` has test consumers only. Renderer selection readback remains
  excluded by the task contract.
- `Graphics.GpuTransfer` now owns ordered, copied multi-range batches with
  exact generational-handle/range revalidation, cancellation, and exactly-once
  consumption. Generic `JobService` contracts prove parked publication,
  dependent release, bounded main-thread delivery, and world-cancellation
  cleanup without adding a second runtime service. Focused CPU evidence:
  `GpuTransferFacade.*` 8/8 and `GpuReadbackJob.*|GpuResultReadbackJob.*` 6/6.
- Slice B1 completed on 2026-07-27. K-Means replaced its three
  `AsyncBufferReadback` instances with one `Graphics.GpuTransfer` batch;
  `KMeansGpuResultReadback` owns only range selection and typed validation/
  parsing, while the private JobService GPU participant retains frame-command
  and shutdown ownership. CPU contracts passed 15/15. The sanitizer-enabled
  `ci-vulkan` parity smoke passed 1/1 on an NVIDIA GeForce RTX 3050 with driver
  590.48.01 and explicitly observed three ranges, one transfer-read barrier,
  one delivered batch, and one consumed batch. The Vulkan benchmark consumer
  also compiled against the renamed adapter. Progressive Poisson remains the
  open Slice B2 producer.

## Goal

- Provide one backend-neutral, multi-range GPU transfer/readback operation that
  parks and resumes canonical `JobService` work, migrate every compute-result
  workflow to it, and retire `Runtime.AsyncBufferReadback`,
  `Runtime.GpuReadbackJob`, and direct blocking result reads.

## Non-goals

- No renderer picking redesign. `SelectionReadback` remains a frame-correlated,
  single-shot renderer lifecycle with different ownership.
- No generic GPU command scheduler, Vulkan type leakage, implicit device-wide
  wait, or per-method readback queue.
- No automatic interpretation of bytes as a geometry property; feature owners
  parse and validate their typed results.

## Context

- `Graphics.GpuTransfer`, `Runtime.AsyncBufferReadback`,
  `Runtime.GpuReadbackJob`, K-Means readbacks, and Progressive Poisson
  `IDevice::ReadBuffer` calls currently implement divergent completion,
  cancellation, and byte-range rules.
- `GpuReadbackJob` has test consumers but no production caller. Its valuable
  waiting/cancellation contracts should become tests of the shared transfer
  path rather than justify a public feature wrapper.
- `RUNTIME-194` supplies one work lifecycle; this task supplies its one
  asynchronous GPU-result wait mechanism.

## Slice plan

- **Slice A — transfer contract.** Add multi-range request/ticket/result data,
  exact buffer generation/range validation, cancellation, and JobService
  waiting/resume integration.
- **Slice B — producer migration.** Move K-Means and Progressive Poisson first,
  then all other compute/method result paths, with actual Vulkan parity.
- **Slice C — cleanup.** Delete both runtime readback modules, direct result
  `ReadBuffer` paths, duplicate queues/parsers, and their obsolete tests/CMake
  entries after the shared path is proven.

## Required changes

- [x] Extend the existing graphics transfer seam with one backend-neutral
      multi-range readback description and copied completion result; expose no
      Vulkan types or backend internals.
- [x] Integrate readback tickets with `JobService` parked/waiting state,
      cancellation, dependency completion, world shutdown, and bounded
      main-thread delivery.
- [ ] Validate buffer identity/generation, offsets, sizes, dimensions, and
      expected byte counts before a feature parser sees data.
- [ ] Migrate K-Means, Progressive Poisson, and every subsequent GPU method
      operation to one final-result readback; keep iteration/control reductions
      on device.
- [ ] Replace `GpuReadbackJob`'s generic property-write behavior with
      caller-owned typed result processing after delivery.
- [ ] Delete `Runtime.AsyncBufferReadback`, `Runtime.GpuReadbackJob`, duplicate
      method-local readback queues, and blocking result `IDevice::ReadBuffer`
      calls only after CPU/fallback and actual-GPU tests use the new path.

## Tests

- [x] CPU/fake-device tests cover multi-range ordering, malformed ranges,
      incomplete delivery, cancellation, stale buffers, dependent work, and
      exactly-once resume/apply.
- [ ] Port the valuable `GpuReadbackJob` contracts to the shared operation and
      remove tests that only pin the obsolete wrapper.
- [ ] Opt-in `gpu;vulkan` tests prove K-Means and Progressive Poisson final
      results use the shared transfer path without a device-wide stall.
- [ ] Structural tests prove no production compute result calls
      `IDevice::ReadBuffer` and no old runtime readback module remains.

## Docs

- [ ] Document transfer ownership, JobService waiting, caller-owned parsing,
      and the explicit SelectionReadback exclusion.
- [ ] Regenerate module inventory and test-routing documentation after module
      and test target changes.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] One transfer/readback mechanism serves every runtime GPU-result
      operation and supports multiple result buffers/ranges.
- [ ] Feature owners parse/process delivered bytes; the transport owns no
      K-Means, Poisson, geometry-property, or presentation semantics.
- [ ] Both old runtime readback modules and all blocking compute-result reads
      are deleted after actual workflow parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GpuTransfer|Readback|KMeans|ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Readback|KMeans|ProgressivePoisson' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- A feature-named readback service/queue, synchronous device-wide wait, or
  Vulkan type on a public seam.
- Folding selection picking into this transfer lifecycle.
- Deleting old paths before an operational Vulkan run proves their consumers
  migrated.

## Maturity

- Target: `Retired`; CPU/fake-device contracts establish `CPUContracted`,
  `Operational` is owned by `RUNTIME-195` through an actual Vulkan workflow,
  Vulkan workflow parity establishes `ParityProven`, and deletion of both old
  modules/direct read paths closes the task.
