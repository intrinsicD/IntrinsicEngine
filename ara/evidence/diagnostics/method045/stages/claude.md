Here is my read of the diagnosis against the six questions, followed by a bounded next step.

**Were all initial and intermediate clusters visualized?** No. Both atlas arms export only the initial label array and the final one. The atlas merge loop is fully ordered, so an accepted-merge sequence could be logged cheaply, but nothing records it today. The native baseline exports initial and final states plus a merge history whose order relative to refinement is unknown, so native intermediate stages cannot be replayed either.

**Alignment with curvature-extremum lines.** Not measured. The detached principal and mean extrema already carry confidence, strength, sharpness, residual, and scale agreement, but no overlap between those curves and any chart border, native barrier, or lost edge has been computed. Exposing them as overlays is correct as a first step. It is not yet evidence for or against alignment.

**Exact merge criteria.** The feature arm ranks a candidate pair by shared border length over the square root of the smaller chart area, divided by one plus the feature weight times the length-weighted mean dihedral turn. That score only orders the heap. Admissibility is separate and consists of a disk-topology check and the stretch and anisotropy limits after a fresh LSCM solve. The protected arm ranks by shared border length over the square root of the smaller area only, restricts candidates to charts inside the same source region, and uses the same admissibility test after explicitly cutting the union open. Neither arm compares distortion before and after a merge. Any union under the limits is accepted.

**Moving seeds.** Nothing moves. Seeds are placed once, propagated once, invalid patches are bisected, and there is no seed relocation or cut smoothing. Native refinement is the only stage that adjusts borders, and it ran on one of the two meshes.

**Does distortion merging destroy good clusters?** The evidence says the damage happens at different stages on the two meshes.

| Mesh | Baseline edges lost before merges | Lost during merges | Of which native hard |
|---|---|---|---|
| frog | 41 of 55 | 0 | 0 |
| sculpt | 5 of 384 | 58 | 58 |

On sculpt the merge stage removes hard-feature borders because the feature weight only ranks and can never veto. On frog the propagation stage has already discarded most borders before merging starts, so a merge-side fix would not help there. The protected arm's 64 erased soft edges on frog cannot be called "good clusters destroyed" because nothing in the packet says those soft edges were correct; native itself flagged them as low confidence.

**Does the evidence inform decisions?** It settles two things and leaves two open. Settled: ranking-only feature influence is insufficient where native marks a hard barrier, and frog's losses need a propagation-stage change rather than a merge-stage change. Open: whether the extrema curves coincide with native hard barriers or with the edges frog loses, and whether the erased soft edges mattered.

**Recommended bounded next formulation.** Do not add every native signal, and do not hard-lock the extrema curves. The extrema are noisy by their own residual and confidence fields, and the sculpt evidence implicates native hard barriers specifically, not curvature curves generally. Three bounded pieces:

- **One admissibility change, feature arm only.** Treat native hard-barrier edges as a merge veto. Leave soft edges as ranking influence. Rerun sculpt and report final chart count, distortion, and hard edges lost. Success is zero hard edges lost with chart count and distortion within the current limits.
- **Measurement only, no decision change.** Compute overlap of extrema curves, thresholded by their own confidence, against native hard edges and against the 41 frog edges lost in propagation. This tells you whether the curves are a candidate oracle before any code consumes them.
- **Trace the atlas merge sequence.** Log each accepted pair in order with the two rejection reasons. This is the intermediate-state visualization that is actually available, unlike the native stages.

Leave frog's propagation stage for a separate formulation once the overlap measurement says whether curvature curves or native barriers explain the 41 lost edges.
