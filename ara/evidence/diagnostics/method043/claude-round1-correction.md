Acknowledged.

- **Shapira test:** My earlier claim was wrong. Shapira 2008 section 3 compares the hit normal to the source normal with a 90 degree cutoff. Our source-antipodal convention with a 45 degree cutoff is a deliberate narrower variant, so oblique sphere rays beyond 45 degrees are correctly rejected. An exit-normal dot ray test would be a separate variant, not a fix.
- **Closed inputs:** Take the nearest hit first, then apply source-antipodal rejection. Valid counts report exactly this.
- **Sampling:** 30 Fibonacci equal-solid-angle samples replace the three discrete rings before any testing, removing artificial two-ring sigma and median ties. No axis ray. A separate exact-axis query test checks diameter, not the cone field.
- **Tests:** No equating of average oblique length with exact slab thickness.
- **Belt arithmetic:** Relative diameter change is h over r, so 12 percent, not 24. We measure the actual field, no assumed prominence.
- **Budget:** Round 1 is thickness reference only. No data-dependent changes until Round 2. Source review follows Round 2. I performed no execution.
