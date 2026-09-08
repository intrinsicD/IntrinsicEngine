**Verdict:** no correctness blocker in the refinement core. The frog cohort supports "collective moves escape jagged minima under audit" and "the distortion limit rejects many of them". It does not support "principal curves help beyond shortening". Sculpt is uninformative for this method.

**Unverified:** `repack.py`, the native runner, and scipy's handling of int64 capacities in `maximum_flow`. I rely on your reported passing tests for those.

## Correctness findings

- **Dead branch in frozen_bands.** The `all(cores)` guard can never fail, because every chart's core, or the whole chart, is already in `pinned`, so each core set is nonempty by construction. The `no_fixed_core` status your test observes comes from an empty `mutable`, not that branch. Harmless, but the comment promises a check that does not exist. Fix: delete the branch or turn it into an assertion.
- **Integer rounding in binary_cut is untested.** Weights below one ten-millionth of the pair total are floored to one unit, so the integer problem can differ from the float one. The float recheck covers energy, but the oracle test uses weights 1 to 24 and never exercises this. Fix: add a wide dynamic-range case and assert the returned energy is within a documented tolerance of the exhaustive optimum.
- **Chart shrinkage is unbounded above the core.** The length arm drove one frog chart to 14 percent of its initial area. Global cores prevent loss, not collapse. Fix: log per-chart area ratio in each accepted event now. Do not add an area floor on this cohort, since that would be a parameter tuned on the guidance's own outcome.
- **Rejected events lack diagnostics.** A `distortion` or `disconnected_chart` rejection does not record which chart failed. Cheap to add, useful for the viewer.

## Overclaims to avoid

Frog, common guided objective and support on additional seams:

| arm | common energy | curve support | native support | max stretch |
|---|---|---|---|---|
| reference | 10.556 | 0.108 | 0.066 | 1.350 |
| length | 7.870 | 0.115 | 0.076 | 1.346 |
| native | 6.796 | 0.113 | 0.316 | 1.342 |
| curves | 6.832 | 0.133 | 0.279 | 1.349 |
| shuffled | 8.454 | 0.109 | 0.061 | 1.338 |

- **The native arm beats the curves arm on the common objective** without optimizing it. The curve field's effect is inside the spread between controls. State this as "no measurable contribution from principal curves at scale 2", not as a partial win.
- **Every positive field descends its own energy**, including the shuffled one. The only evidence that real fields are not noise is the common objective, where shuffled lands between reference and length. Packing utilization differences are noise. The shuffled arm has the best utilization.
- **Roughly seventy percent of additional seam length still runs through low-evidence edges** in the guided arms. Do not call the result feature-aligned.
- **Sculpt shows one twelve-face move**, identical across three arms. Region borders make up 384 of 398 seam edges, so almost nothing is movable. The dominant remaining sculpt cost is the 29 internal cut edges from region opening, which this method cannot touch.
- **No optimality claim, even per pair.** The cut minimizes an integer-rounded border surrogate with fixed cores, then a filter applies. That is a monotone descent step, nothing more.

## Test gaps

- No test where a third chart's face is fixed by its current label after an earlier accepted move. The middle-chart test only checks freezing, not the interaction the global-core fix addresses.
- No test that a pair rejected under one revision key is retried after a neighbour changes.
- `edge_evidence` is untested for missing edges and out-of-range soft values.
- `curve_support` falloff, tangent cosine, and scale-index validation are untested numerically.
- The aggregate script has no consistency assertion. Reference initial energy equals curves initial energy on both meshes, which is the right sanity check to encode.

## Your changes to the plan

- **Global cores:** correct and necessary. Side effect: cores are hop-based from all rims, including region borders, so small charts get near-empty cores and shrink toward them. That is the source of the area-ratio numbers.
- **Initial pair bands:** correct for boundedness. But you cannot see whether bands bind. Forty-odd `no_border_descent` events per frog arm suggest saturation by round two. Record how many moved faces touch the band boundary in each accepted event.
- **Scale 2 on both meshes:** defensible against tuning, but sculpt curve support near 0.02 means sculpt cannot distinguish "curves unhelpful" from "wrong scale". Report that as bounded, not negative.
- **Surrogate descent then full-energy acceptance:** sound. The cost is that moves lowering internal cuts while raising border length are never proposed. Frog internal cuts rose from 5 to 8 or 9 under acceptance, so the accounting is doing real work.

## What to show

Show the frog native arm beside the reference, with the length arm as the shortening control. It matches the curves arm on every audited metric, wins the common objective, has the lowest guided-arm max stretch, and depends only on engine-native evidence. Keep the curves arm in the comparison table only. For sculpt, show the reference alone and say the method has no purchase there.

## Remaining quality problem

Seams remain mostly on unsupported edges. About forty percent of border-descending proposals die on the 1.35 stretch limit, so the limit, not the guidance, decides where seams settle. Charts shrink toward cores. Repacking introduces nine to thirteen reflected charts on frog where the unpacked result has none, which will mirror tangent-space textures. Sculpt's remaining cost is internal cuts from region opening, outside this method's scope.
