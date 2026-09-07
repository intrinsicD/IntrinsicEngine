The design is sound as a bounded offline check, but four things need tightening before parameters are frozen.

**Geodesic sweep biases**
- Sublevel sets of single-landmark distance are geodesic balls. They only align with a neck when the landmark sits on the axis of the lobe. On the axisymmetric controls, farthest-point sampling lands on the poles and level sets are exact parallels, so the sweep is tested in its ideal case only. On the frog, extremity landmarks tilt the level sets through the torso, inflating cut length. Record this gap rather than adding controls.
- Dual-graph Dijkstra is grid-anisotropic. Distances differ by up to a root-two factor depending on direction and diagonal choice. That is what the diagonal sensitivity is meant to measure, but it also means the belt and groove level sets are polygonal, so decorative rejection at 32x24 may be a resolution artifact rather than a method result.
- Farthest-point landmarks on a sphere have ties. Fix a tie-break rule by lowest face index, otherwise the rotation control measures tie-breaking, not geometry. Rigid rotation cannot change edge-length geodesics anyway, so that control is weak.
- For refinement passes, quantiles and landmarks must be recomputed inside the part on the restricted dual graph. Global distances crossing existing part boundaries are meaningless.

**Prominence definition**
- "Relative prominence" needs a denominator. Suggested form: for a minimum at quantile q with value N_q, take M_left as the max over the preceding 0.10 of area and M_right over the following 0.10. Prominence is (min(M_left, M_right) minus N_q) divided by N_q. Truncated windows near 0.02 and 0.98 should be excluded.
- On a sphere N falls monotonically toward the equator and the equator prominence is roughly 10 percent. A long cylinder-like ellipsoid gives roughly 12 percent at its middle. The 0.15 threshold is therefore marginal for convex shapes and the energy gate carries the real load. The ablation must record which gate rejected each candidate.
- The 5-sample smoother spans 0.05 of area and the window 0.10. Parts near the 0.02 minimum are structurally undetectable. State this as a design limit for frog digits.

**Hard-base subset feasibility**
- A geodesic ball intersected with a part can be disconnected, and the part remainder can also fragment. Legality recomputation per accepted split is necessary, as you have, but candidate generation should reject any candidate whose remainder is disconnected rather than counting fragments as free parts.
- Greedy order matters. Fix it: largest energy decrease first, then lowest face index.

**Orientation and curvature averaging**
- This is the main risk. At a mild waist the meridional curvature is negative but the circumferential curvature is positive, and mean-curvature-like estimates from face normals will be positive for rho at 0.7 and probably 0.5. The cut runs along a parallel, so the dihedral across each cut edge measures meridional bending and carries the right sign. Specify kappa as the per-edge dihedral of cut edges, then smoothed, not an area-weighted face mean curvature.
- Positive smoothing weights preserve sign only when all samples share it. Averaging near a belt shoulder mixes signs. "Convex rejection" holds only if the smoothed kappa is positive along the entire cut, so the gate should be evaluated per cut, not on a summary.
- Near the poles the sqrt(1 minus u squared) profile has infinite slope, so fan triangles are extreme and normal-based curvature is noisy there. Clustered axial sampling amplifies this.

**Synthetic model flaws**
- With rho at 0.9 there is no waist. Lobe maximum radius is about 0.87, so that case is a bumpy ellipsoid. Keep it, but label it as a convex negative control, not a lobe.
- rho at 0.7 has roughly an 18 percent radial dip. Expect it marginal on prominence and likely rejected by the curvature sign issue above.
- A 5 percent groove at 32x24 spans less than one parallel ring. Specify axial width, and accept that 32x24 may not represent it at all.
- Scale invariance holds for L over D and D times kappa, but the deltaK term and the 0.02D radius must be checked with the scale control.
