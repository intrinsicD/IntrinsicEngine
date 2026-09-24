# METHOD-048 round-2 review

**Scope:** I reviewed the revised text as supplied. I ran no tools, so I have not checked the stated digest (`43a3…c711`) against the file.

## Round-1 dispositions

Your three corrections to round 1 stand, and I have dropped those points:

- **Maturity:** keeping `ParityProven` is correct under the cumulative taxonomy.
- **Contract catalog:** the note that no dedicated IDs exist, while AGENTS §§1/7/8/8b still bind, is adequate.
- **Floor branch in S03b:** only the denominator derivative vanishes. The numerator term `∂C/∂α_s = c_s` remains. My round-1 wording was wrong.

Everything else from round 1 is resolved coherently:

- **B1:** S01b now runs before S04. S15 only optimizes an existing production solver.
- **B3:** S03b has the right shape: fixed-selection adjoints, stage-wise finite differences, upstream gradients, and explicit boundary rules.
- **H1:** The classification table and the "disposition available, not success" rule make the dependency chains valid, including S13→S09/S12b and S14→S13. S12a no longer depends on any candidate slice.
- **H2:** Preregistration happens once, in S00, and amendments require a fresh held-out cohort. The concrete producer field, incapable-backend behavior and operator-rejection terminal are all actionable.
- **H3–H6 and M1–M12:** addressed as dispositioned. S17's move to a tools command with native import removes the contradiction and the process-ownership problem.

## Remaining finding

### High — the strict rank gate has no exact-tie predicate and no defined data source for refinement

**Location:** Impact protocol, "Rank stability…" paragraph; S05 second bullet; S10 first bullet.

**Problem:** You kept the strict gate, which is fine. But it is not achievable as written, for two reasons.

1. **Exact ties never resolve.** "Refine … with higher precision until resolved" never terminates on an exact tie. Every refinement step produces overlapping or zero-gap intervals. "Exact ties use stable IDs" gives no procedure for *deciding* that a tie is exact. The S00 cohort deliberately includes "ties/duplicate kernels", so with a strict cap of zero unresolved samples, the task's own fixtures are guaranteed to fail S05.

2. **Compiled coefficients cannot recover the reference order.** `D_si` and `E_ij` are rounded functions of the embedding `z`. When a distance gap is below the coefficient rounding, the exact order of the compiled `q` values can differ from the exact order of the reference values. Evaluating the compiled coefficients at higher precision cannot recover the reference order. Refinement therefore has to read reference inputs, but S10 says rendering loads the compiled payload without spectral data. The task never says where S05 or S12a get the inputs they refine from.

**Replacement text:** add this to the rank-stability paragraph.

> "The oracle ranking is the exact order of the S03 reference expressions over the canonical stored float64 inputs.
> - **Outer distances** are polynomial in those inputs. Decide them with exact expansion (or rational) arithmetic; exact equality → stable-ID order.
> - **Inner contributions** are transcendental. Treat bitwise-identical parameter records plus identical query inputs as an exact tie → stable-ID order. Otherwise refine with arbitrary precision up to a preregistered S00 precision cap. Reaching the cap is an unresolved sample.
> - **Refinement data source.** Compiled evaluators refine from reference inputs, not from compiled coefficients. For S05, fall back to the S03 evaluation for that query. For S10/S12a, either store optional refinement data (vertex/source embeddings and inner-stage parameters), counted in payload and VRAM bytes, or report refinement unavailable. In that case the sample counts against eligible coverage."

Also add one line to S05's gate: exact-tie fixtures must pass via the predicate, not via refinement.

## Non-blocking nits

- **S17 precondition:** S17 is core but requires a licensed calibrated example. Make "dataset identified in S00" a precondition for starting S17. Otherwise S17 can stall indefinitely with no disposition path.
- **Verification section:** it still says "S12 registers GPU cases". Change this to S12a/S12b.
- **S18 typo:** "Preserve preserve".

**REVISE** — only the High finding above. Once that text is in, I have no remaining blocking or high objections.