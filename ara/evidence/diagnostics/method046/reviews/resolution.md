Claude completed a mesh-free source and aggregate review. No correctness blocker was found in the refinement core; no independent native rerun or anatomical visual review was performed by Claude.

Resolved implementation findings:
- Removed the redundant per-pair core branch; the status now accurately says no_mutable_faces. Global fixed cores already supply the invariant.
- Added an exhaustive binary-cut control spanning 17 orders of magnitude. The floating border-energy error bound is 2*m*sum(weights)/1e7; every actual move still requires strict floating full-seam energy descent and unchanged UV limits.
- Every candidate records the affected chart-area ratios relative to the starting atlas and how many moved faces neighbor fixed faces. Rejections identify the failed chart.
- Added missing/native-confidence validation and exact curve falloff, tangent and scale-index controls. The suite passes 12 tests.
- The collector checks shared reference/curve initial energy, unique pair/revision attempts, every stage's region purity and chart count, accepted energy descent, and independent packed-corner seam accounting.

Review interpretations narrowed against the actual evidence:
- Native guidance wins the common guided objective (6.795892 versus 6.831836); curve guidance has higher curve support (0.132976 versus 0.113408), shorter added seams (9.246411 versus 9.327315), and higher square occupancy (59.5685% versus 56.0669%). The statement that native matches curves on every metric is false. These are a tradeoff, not proof of anatomical superiority or a reason to hide one arm.
- “No measurable contribution from principal curves” is too broad: they alter labels and the measured support. Independent anatomical benefit remains unestablished. A training score and one shuffled realization cannot establish semantic quality or statistical significance.
- Packing outcomes are deterministic measurements under frozen inputs. Calling the differences noise is unsupported without repeated/perturbed placement evidence; report them as observed tradeoffs.
- A claim that 70% of seams have low evidence has no declared threshold. Do not repeat it. Confidence is not a probability.
- Whole-chart packing reflections are explicitly accepted by the existing atlas contract. Mixed orientation, overlap and invalid boundaries are rejected. A downstream tangent-space defect was not tested; do not infer one from reflected chart counts alone.
- Sculpt is a preservation control with one small boundary change, not evidence of broad feature-guided improvement. The current solver recomputes internal cuts after a border move but does not independently optimize their placement.

Remaining limits:
- The global cores stop disappearance and unbounded drift; they do not enforce balanced areas. Frog length-only reaches a minimum chart-area ratio 0.14345, guided arms 0.483198. No area floor was fitted to these outcomes.
- The six-hop bands and fixed initial chart pairs restrict reach; exact geodesics, transported tangents, arbitrary remeshing, other scales and an anatomical boundary oracle remain untested.
- The integer cut optimizes a border surrogate. UV constraints and full internal-cut accounting filter it afterward; there is no optimality claim for the complete constrained problem.
- Rejection/revision behavior is exercised and reconciled on the cohort. A dedicated synthetic accepted third-pair interaction/cache-retry test remains absent; the analytic global-core invariant test is narrower.
- Merge ranking remains unchanged. This slice corrects extra UV boundaries after protected merging and cannot restore source features erased in an earlier unrelated atlas run.
