---
id: GEOM-103
theme: I
depends_on: [GEOM-081]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; the keep/remove decision owes its own benchmark and ARA evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# GEOM-103 — Measure the Vulkan smoothing backends and decide keep or remove

## Goal

Decide, with fair measurements, whether the Vulkan property-smoothing backends
earn their complexity. A GPU backend that is not faster (or has no other concrete
benefit) is removed, not kept "for later".

## Context

Operator decision 2026-09-27: pause further GPU slices until this is answered.

- **GEOM-081** (on `main`): Vulkan averaging, Taubin, bilateral and spectral heat;
  parity proven (C111), speed unmeasured.
- **RUNTIME-269 + GEOM-089 slice 1** (local branch
  `claude/runtime-269-vulkan-sparse-solve`, commit `ced6ad864`, **not merged**):
  device Jacobi CG mirroring `Sparse::SolveCG` and conjugate-gradient implicit
  smoothing on Vulkan; parity proven (C112 on that branch), speed unmeasured. Merge
  or drop it according to this task's outcome.

The sealed benchmark runtimes are frame-paced end-to-end editor times on 300-row
fixtures in a Debug build (locked desktop seat or Xephyr); they say nothing about
kernel speed. Known handicaps to fix before measuring, or the comparison is unfair:

1. All buffers are `HostVisible`, so on a discrete GPU the kernels likely read
   system memory over PCIe; use device-local buffers with staged uploads.
2. CG records 9 tiny dispatches and 4 barriers per iteration; fuse kernels where
   the CPU-mirroring control flow allows.
3. Each CG chunk waits several frames for its status readback.
4. Consumer GPUs run double precision at a small fraction of float rate
   (RTX 3050: 1/64); CG is mostly bandwidth-bound, so measure rather than assume.

Expected picture to test, not to claim: explicit filters are plausible wins on
large meshes; GPU CG must beat the CPU default for implicit smoothing, a sparse
Cholesky factored once and reused for every channel and step.

## Acceptance criteria

- [ ] Release build (`ci-vulkan-release` or equivalent) on an unthrottled display; record device, driver and build.
- [ ] Handicaps 1–3 fixed or explicitly measured as separate variants.
- [ ] Per-stage cold/warm upload, compute and readback time and device memory for 10k, 100k and 1M rows, for each explicit filter and for implicit smoothing, against the CPU filters and against CPU Cholesky and CPU CG; manifest-backed results under a `GEOM103Vulkan` selector.
- [ ] Decision recorded per backend (keep as opt-in, or remove) with ARA evidence. Removal deletes the code, tests, config values and docs in one change; keeping states the size range where it wins.
- [ ] GEOM-089's remaining slices (heat geodesics, signed heat, vector heat) are resumed only if the implicit/CG result justifies GPU solves.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged from GEOM-081 / RUNTIME-269. |
| Compatible entity sources | Unchanged. |
| RuntimeModule | Measurement and possibly removal; no new runtime surface. |
| Config/agent | `backend` values of `sandbox.property_smoothing` are kept or removed per decision. |
| UI | Backend combo kept or removed per decision. |
| Publication | Unchanged. |
| End-to-end tests | Benchmark producer and validation; existing parity suites kept only for kept backends. |

## Verification

```bash
cmake --preset ci-vulkan-release
cmake --build --preset ci-vulkan-release --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan-release --output-on-failure -L gpu -L vulkan -R 'GEOM103Vulkan' --no-tests=error --timeout 600
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan-release/benchmark-ctest/GEOM-103 --strict
```
