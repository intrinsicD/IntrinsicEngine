**Recommendation: a sharpened version of B. Run one pre-registered falsification test on a withheld tapering-limb fixture, with R3 parameters frozen. Do not build A or C.**

**Why not A.** The frog failures you describe are topological: a central back patch and a torso wrap that join unrelated areas. Boundary-length regularization with fixed per-label interior seeds cannot change the region set, only its outline. You would smooth wrong regions and add three or four knobs while the conclusion stays identical. The dual-component gate is a selector that returns METHOD-039's own answer when 039 already succeeds, and it was designed after the sculpt observation. It is a held-in no-regression rule and should be stated as such. A produces cosmetic change and no new scientific information.

**Why not C.** Shapira's Gaussian mixture plus graph cut is a full method with its own model selection and smoothness weight. One round cannot separate "the signal fails" from "the fit was misconfigured." The neck fixture would likely yield a waist band class, and you would have no budget left to interpret that.

**Why the fixture test is real progress.** R3 already gives a mechanistic hypothesis: persistent thickness maxima are protrusion detectors, and seams appear only at thickness saddles between two maxima. A limb whose thickness decreases monotonically from body to tip has no second maximum, so the method predicts no seam at the junction. A limb with a mid-length bulge has a maximum at the bulge, so the method predicts a seam at the bulge, not at the junction. Both predictions explain the frog result: thigh-like patches, front-back patches, no whole limbs. Confirming them turns an anecdotal visual into a stated failure mode with evidence.

**The exact test.**
- Two synthetic fixtures, never seen during R1 to R3: a sphere with one attached cone limb of monotone tapering radius, and the same sphere with a limb carrying a single bulge at mid-length.
- Same resolutions as R3 and one diagonal flip, plus one vertex-reordered copy of each.
- R3 pipeline unchanged, parameters frozen at the R3 values. Any parameter change converts the test into a fourth fitting round.
- Baseline 039 run alongside on the same meshes.
- Pre-registered predictions, written before running: tapered limb gives one region under R3 and two under 039 with the 039 seam at the junction. Bulged limb gives a seam at the bulge under R3, at the junction under 039.
- Reported metrics: region count, and seam position along the limb axis normalized by limb length, per mesh.

**What the outcome licenses.** If predictions hold, the claim is: thickness basins detect local protrusions and place seams only between competing maxima, so they are not a part detector for tapering limbs. Thickness remains a candidate secondary cue for future work, but that is a future task, not a fifth iteration. If predictions fail, R3 is stronger than the frog visual suggested, and the frog problem is elsewhere. Either way the final round ends with a decided question.

**Documentation guardrails.** Synthetic part counts stay as reported. Sculpt is exactly five because of the held-in hard-edge guard, not because of thickness. Frog visuals stay exploratory with no anatomy pass. Do not report the fixture result as a robustness proof of anything beyond the two fixtures tested.
