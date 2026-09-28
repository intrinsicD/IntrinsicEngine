---
id: GEOM-111
theme: I
depends_on: [METHOD-012]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, parity tests against the ported references, and CI.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# GEOM-111 — Unified progressive point sampling (`Geometry.PointSampling`)

## Goal
- One geometry-layer API that orders the points of any point set so that every prefix is a
  usable subsample, with the method selectable (`PointSamplingMethod`), deterministic and
  CPU-first. It is the single sampling entry point for every consumer (CPD subsampling and
  landmarks, consolidation seeds, the standalone subsampling operation) and the CPU parity
  reference for later Vulkan ports.
- Port the operator's sampling research (repo
  `GPU-Accelerated-Progressive-Poisson-Disk-Sampling-via-Phase-Parallel-Spatial-Hashing`,
  own code) from CUDA to CPU C++. GPL-3.0 QuickFPS-GPU derivatives are never ported.

## Context
- Engine state: three naive O(N k) farthest-point copies in CPD, `RandomSubsample` in
  `Geometry.PointCloud.Utils`, voxel centroids, and the METHOD-012 progressive Poisson
  reference as a runtime-only method package (geometry cannot link it).
- Slice plan: (1) this task: module, `Random`, exact `FarthestPoint` via the hole sieve
  (Morton block tree, pruned updates, order bitwise equal to brute-force FPS, optional
  importance weights maximizing w_i clear_i), and `ProgressivePoisson` moved into geometry
  (the method package keeps its docs and forwards); (2) GEOM-112 completes the Poisson port;
  (3) GEOM-113 ports the approximate FPS family and the baselines; RUNTIME-289 wires the
  selection into every consumer; RUNTIME-274 is the standalone operation and panel;
  METHOD-055 owns the Vulkan hole sieve.

## Acceptance criteria
- [ ] `Geometry.PointSampling`: `PointSamplingMethod`, `Params` (method, seed, first index, weights, per-method settings), `Order(points, params, count)` returning original indices in progressive order plus per-sample diagnostics; invalid input fails closed.
- [ ] Exact hole-sieve FPS: order bitwise equal to a brute-force float64 FPS with the same tie rule on 2-D/3-D fixtures (coincident points included); weighted variant equal to a brute-force weighted scan; pair-count reduction reported.
- [ ] Progressive Poisson available through the API with results identical to METHOD-012's `ProgressivePoissonReference::Compute`.
- [ ] CPD's farthest-point subsampling and landmark selection use the module (results unchanged: same FPS order and tie rule), removing the naive copies.
- [ ] Module docs, README and inventory updated; smoke benchmark with a manifest.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointSampling|CoherentPointDrift|ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
