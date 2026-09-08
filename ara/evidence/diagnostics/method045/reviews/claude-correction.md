Correction accepted. The report records the METHOD-039 local and METHOD-040 curves sculpt label array as 5 regions and the thickness array as 24 regions. METHOD-044's five-chart result is a different artifact and I should not have treated them as the same thing. I also agree that a matched-mean sweep cannot promise an exact xatlas mean and that the earlier proposal changed four things at once.

**Does the smaller experiment hit the failure mechanism?** Mostly yes. The 64-seed atlas discards a partition that already exists and regrows charts from seeds, so chart boundaries cannot coincide with semantic boundaries even where a region would have parameterized cleanly. Seeding from baseline components and splitting only on failure changes exactly one variable and tests exactly that claim. The part it does not settle is why a region fails, and that is where the design can leak.

**Counterexamples to plan for**

- **Non-disk regions.** A region wrapping a leg is an annulus or tube. LSCM needs a disk, so such a region fails on topology, not quality. The split rule then decides the result, which quietly reintroduces a second variable. Classify topology cuts separately from distortion cuts before running the solver.
- **Passing is not good.** A region can satisfy the existing limits with stretch far worse than xatlas. Pass/fail alone will overstate success. Report max, p95 and mean per chart.
- **Labels are not components.** If one of the 5 sculpt labels is two islands, the protected atlas has at least 6 charts and "exact sculpt boundaries" still holds while the count does not. Record label count and component count as separate columns.
- **Sliver regions in thickness.** Some of the 24 regions will be thin. Each becomes its own chart, packing efficiency falls, and intra-region merging cannot help by design. That is the price of protection and should be reported as such, not smoothed by cleanup.
- **Fallback disguised as success.** If a region fails and the split machinery hands back something close to the 64-seed result, the outcome must say so.

**Checks I would require**

- Per component: Euler characteristic, boundary loop count, disk test, before any solve.
- Fail-reason taxonomy with counts: topology, solver, distortion limit, degenerate.
- Edge-set diff: chart-boundary edges minus baseline inter-region edges. Must be empty when no splits occurred. Report both set sizes.
- Identical packer, resolution and padding for all three atlases. Report texel utilization alongside chart count.
- Determinism: two runs, byte-identical output.
- Explicit outcome enum in the record: protected with no splits, protected with N topology splits, protected with N distortion splits, fallback to 64-seed.

One comparison table with rows for the 64-seed atlas, the protected-baseline atlas and native xatlas, and columns for chart count, boundary-edge agreement with the baseline, max and p95 and mean distortion, and utilization. Run it for sculpt and thickness separately.

**Simpler alternative first**

Skip atlas construction on the first pass. Run LSCM on each baseline component under the existing limits and emit only the topology class, the fail reason and the three distortion numbers. No splitting, no merging, no packing. This is read-only and decides the question directly. If all 5 sculpt components pass, the protected atlas is a formality. If several fail on topology, the real design problem is the cut strategy, and building the atlas path before knowing that would have hidden it. Thickness will likely show the sliver issue here too, before any packing cost is spent.

Only after that diagnostic would I take the protected-atlas step, and only after that would seam-shortening face moves be worth touching, with the same topology and distortion checks and identical packing.
