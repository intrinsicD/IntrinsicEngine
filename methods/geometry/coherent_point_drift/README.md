# Coherent Point Drift

CPU reference for rigid (default), affine and nonrigid Coherent Point Drift
(Myronenko & Song 2010) in `src/geometry/Geometry.Registration.CoherentPointDrift.*`:
one streamed E-step, three M-steps, a `Solver` for step-by-step runs and an iteration
observer. Formulation, units and failure states: [paper.md](paper.md).

## ICP or CPD?

- ICP (`Geometry.Registration`) matches each source point to its nearest target point and
  is fast (spatial indices, O(N log M) per iteration), but hard correspondences make it
  sensitive to the initial pose, noise and outliers.
- CPD weighs every source/target pair (soft correspondences), models outliers with `w`,
  shrinks its matching radius (sigma) as it converges, and also fits affine and smooth
  nonrigid motion. The reference costs O(N*M) per iteration and O(M^3) for nonrigid
  solves; accelerated E-steps and low-rank solves are METHOD-049.
- Both are local: neither recovers large rotations of symmetric shapes on its own.

## Parameters in practice

- `OutlierWeight` 0.05-0.3 for scans with clutter or partial overlap; 0 only when every
  target point has a counterpart.
- `EstimateScale` false for scans in the same units.
- Nonrigid `Beta` (kernel width, normalized units) sets how far motion propagates;
  `Lambda` sets how strongly it is smoothed. Defaults 2 and 3 follow the paper's code.

## Complexity and memory

| Variant | Per iteration | Memory |
| --- | --- | --- |
| Rigid, affine | O(N*M) E-step + O(N+M) | O(N+M) |
| Nonrigid | O(N*M) + O(M^3) LU | O(M^2) kernel, M <= 8192 |

## Evidence

- Tests: `tests/unit/geometry/Test.CoherentPointDrift.cpp` (rigid/affine/nonrigid
  recovery, outlier robustness, reflection handling, GEOM-058 E-step agreement,
  determinism and step mode, termination and failure states).
- Benchmark: `geometry.coherent_point_drift.reference.smoke`
  ([manifest](../../../benchmarks/geometry/manifests/coherent_point_drift_reference_smoke.yaml)),
  ground-truth RMS error per variant; no speed claim.
