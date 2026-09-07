Review of the plan follows, with established methods marked as such and hypotheses flagged.

## Flaws that threaten correctness

- **Disk test is incomplete if it only checks Euler characteristic.** Region growing on the dual graph produces pinched vertices where two faces share a vertex but no edge. Established fix: require every vertex's incident patch faces to form a single fan, exactly one boundary loop, and V minus E plus F equal to one. Run this on the merged candidate before any solve.

- **Positive signed area is necessary, not sufficient, for injectivity.** Established result: for a disk, locally injective triangles plus a simple boundary polygon give a globally injective map. So your two checks together are correct. Test boundary simplicity with segment intersection on a uniform grid hash. Do not skip it for small charts.

- **LSCM does not guarantee injectivity even on disks.** Folds appear on elongated or highly curved patches. Treat a fold as a rejection, but count fold rejections separately from stretch rejections. If folds dominate, that is a solver problem, not a segmentation problem.

- **Max-over-triangles thresholds strand charts.** One sliver triangle on a noisy mesh can veto an otherwise excellent union. Hypothesis: use an area-weighted high percentile for the gate and report the true max alongside it. Keep your listed thresholds for round 1 but log how often the max alone caused rejection.

- **Per-edge dihedral is a noisy curvature estimate.** Normal turn per physical distance is the right quantity, since it approximates curvature and is tessellation-independent in the limit. But slivers make individual edge dihedrals unreliable. Hypothesis: smooth normals over a fixed physical radius before computing the cost, then compare features-on against the smoothed variant across resolutions.

- **Seam-length ranking alone strands small patches.** Seam removed per perimeter favors merging small patches early, which is good, but a small patch surrounded by charts already near the stretch limit fails every union and stays stranded. With ~128 initial patches you can afford to evaluate the actual merged LSCM distortion for every adjacent pair. Rank by validated merged distortion, with the seam term and the feature penalty as secondary terms. This is the standard greedy pattern from hierarchical face clustering, with distortion replacing planarity error.

- **Invalidation must be lazy and complete.** When a chart changes, every queue entry referencing it is stale. Your rejection cache keyed on both endpoint versions handles this correctly. Add a post-convergence boundary relaxation pass that lets stranded small charts donate faces to neighbours. That is a hypothesis, keep it bounded to a fixed number of sweeps.

## Solver choices

- **LSCM pinning.** Established and simplest: pin two boundary vertices chosen as the farthest pair by geodesic distance along the boundary or by two BFS passes. This fixes the similarity freedom and avoids ill-conditioning from close pins. Pin positions at the origin and unit distance, then area-normalize per chart afterwards.

- **Harmonic to a fixed circle.** Established, bijective by Tutte's theorem with convex boundary, but distortion is far higher than free-boundary LSCM for anything not disk-shaped. It rejects charts LSCM would accept. Use it only as a fallback initialization for an ARAP or SLIM refinement if fold rates turn out high. Not for acceptance.

- **Orthographic proxy.** Cheap, but it rejects exactly the developable folded sheet you want to accept. Use it at most as a ranking hint. At your chart counts LSCM solves are cheap enough that round 1 should use LSCM directly for every candidate.

- **Separate language from algorithm.** Report solver-invariant costs: number of LSCM solves, total factorized degrees of freedom, number of disk checks, and merge acceptance ratio. Wall-clock goes in a separate column. Those counts transfer to C++, the seconds do not.

## Tests and stopping criteria

Round 1 tests:

- **Developable controls.** Open cylinder, cone, and a sheet with a sharp crease must merge to one chart with stretch near one. This is the folded-sheet promise made testable.
- **Cube.** Five faces are developable. Expect at most two charts with features soft.
- **Sphere and torus at three resolutions.** Chart count and total seam length should be stable within roughly a fifth. Every torus chart must pass the disk test.
- **Sliver retriangulation.** Same shape, degraded tessellation. Feature cost and chart count should barely move.
- **Final audit.** Re-verify all five acceptance criteria on every final chart with an independent code path.
- **Determinism.** Fixed tie-breaking so two runs give identical atlases.

Pivot triggers:

- **Fold rejections above a small fraction of solves.** Add Tutte initialization plus ARAP refinement.
- **Stranded tiny charts above a tenth of the final count.** Switch ranking fully to validated distortion and enable boundary relaxation.
- **Max-only rejections dominating.** Move the gate to the percentile metric.
- **Chart count varying strongly with resolution.** The feature cost is the suspect, not the merger. Rerun with features off to confirm.
- **Frog worse than xatlas on seam length at equal max stretch.** Report the merge-only ceiling honestly and only then consider face-level moves.

The plan is sound in structure. The two changes that matter most before round 1 are the fan-based disk check and ranking by actual merged distortion rather than seam length alone.
