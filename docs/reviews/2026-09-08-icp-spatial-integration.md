# ICP spatial integration review

The accepted scope combines canonical-domain registration (`RUNTIME-207`),
shared config/UI controls (`UI-040`), persistent target indices and batched
Vulkan nearest correspondences. CPU KD-tree remains the default. The existing
CPU trimming, robust weights, transform solve and convergence stages are shared
by all correspondence providers. No new registration formulation is exposed.

## Ownership and concurrency review

- Geometry owns `MakeICPQueries`, `AdvanceICP` and the synchronous callback driver.
  The GPU/CPU boundary is a batch of compact target indices, with no runtime or
  RHI dependency in geometry.
- Runtime keeps the existing geometry-processing command, job and history owner.
  A serializable section provides operands, properties and tuning. Copied
  catalogs and side-effect-free readiness are shared by UI and typed callers.
- `SpatialIndexCache` remains its own concrete runtime module/service. Its
  immutable CPU leases survive eviction. GPU batches retain their target entry,
  reuse buffers, record through JobService frame participation, and read back
  asynchronously after producer submission. Shutdown waits for device work and
  delivers outstanding transfer sinks before releasing resources.
- CPU workers prepare/solve from snapshots. GPU registration parks the completed
  preparation job through `IsReadyToApply`; the device-owner thread advances one
  shared CPU solve step after readback. No nested frame, worker GPU call, or
  worker wait for GPU completion is introduced.
- Binding/deletion revisions, values and transforms are revalidated before
  publication. Requested/actual backend and fallback reasons are explicit.
  Result backend execution labels are shown only once a result exists.

## Verification

- `ci`, Clang 23, `IntrinsicCpuTests`: 4,349 selected tests: 4,348 passed
  with host display access, no failures, and one expected unsanitized
  LeakSanitizer-control skip.
- `ci-vulkan`, combined ASan/UBSan: both `PointLBVHGpuSmoke` tests passed on the
  local Vulkan device, including the final display-off cohort (16.74 s and
  46.72 s). Registration compares each variant with its matching
  CPU reference. A previous test incorrectly compared point-to-plane against
  point-to-point with radial, poorly conditioned normals; the oracle and
  fixture were corrected without changing the solve or loosening tolerances.
- The canonical 64 source/target domain combinations preserve source geometry
  and support undo/redo. Additional tests cover config round-trip/preview/apply,
  stale mixed-domain bindings/deletions, target metric/cache reuse, callback
  failure, and shared menu aliases.
- Method/benchmark manifests, schema-v2 result validation, strict layering,
  task policy, test layout, and documentation links pass.
- Display-off verification exposed the 30 s cohort timeout. An unchanged
  diagnostic run completed all comparisons in 46.715 s, with approximately 13 s
  per GPU registration. `BUG-179` records the present-pacing diagnosis and
  the case-specific timeout plus internal wall-clock limit; assertions and
  the benchmark's 5 s threshold remain unchanged.
- That direct diagnostic enabled LeakSanitizer separately from the normal
  GPU cohort and reported 240 retained bytes after all assertions passed.
  `BUG-180` owns allocation-ownership isolation. These results do not claim
  whole-process leak freedom.
- An intermediate incremental Clang serialization crash disappeared on the
  subsequent cache-disabled build. `BUG-178` owns isolated tooling diagnosis;
  no source workaround is justified by that observation.

The final publication review found that the former column-length decomposition
removed negative source scale. Publication now composes the rigid translation
and rotation directly, preserving signed, nonuniform and zero scale. A targeted
matrix/undo/redo regression covers this boundary.

## Comparison scope and limits

The CPU smoke measures complete KD-tree runs, LBVH build plus run and warm LBVH
runs. The framed runtime smoke measures request-to-publication for both CPU and
Vulkan, including transfers, scheduling and the CPU solve. Both produce sealed
schema-v2 output marked dirty-source and non-claim-eligible. The runtime fixture
uses 1,024 live rows and one measured warm run. Its GPU result is insufficient
for a performance qualification or a default change. The final display-off
attempt preserved a failed 5 s timing disposition (12,999.1 ms warm GPU run),
with zero transform error and one CPU/GPU target build each. See
[registration](../architecture/registration.md) and the
[method note](../../methods/geometry/registration/paper.md).

Existing workflow limits remain explicit: registration uses entity TRS,
without newly adding ancestor-transform composition; rerunning a trajectory
step starts from the current source transform. Undo restores the earlier input
pose when comparing trajectories from that pose. Point-to-plane retains its
minimum-constraint and conditioning limits. GPU accelerates correspondences;
the solve remains CPU. Framework24-wide behavior/performance parity is still
owned by `REVIEW-004`.

## Clean-workshop scorecard

| Check | Disposition |
| --- | --- |
| Promoted imports respect layers | pass — strict scan, zero allowlist entries |
| CMake links respect layers | pass — module sources added to existing owners |
| Public types do not point into higher layers | pass — geometry sees spans, callbacks and numeric results |
| Renderer growth has an owner | pass — existing LBVH workspace; runtime owns cache/batches |
| New passes use typed IDs | n/a — no new rendering pass |
| Recipe edges are resource-driven | n/a — frame recipe unchanged |
| Maturity claims have bounded evidence | pass — local CPU and actual Vulkan checks; no blanket parity claim |
| Temporary exceptions have owner/expiry | pass — no new layering exception or compatibility shim |
