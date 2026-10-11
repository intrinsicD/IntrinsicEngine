Reviewed commit `aeb05205bdf69d96f77bef3bd332f0cc7de92df0` for supplied packet digest `6bffaa05201d09fa03cbd3892be4044b1a01333f0278efd5fd27c4b981d3a4df`. The four source hashes match `binary-bindings.json`; `native.diff` matches the frozen native changes. I did not independently recompute the packet digest.

The execution plan is:

| Cell, in execution order | What it measures |
|---|---|
| `points_1` | Four-iteration Bayesian CPD prefix; fixed point-landmark identities, with source landmark positions following deformation. |
| `box_1` | Two-iteration prefix; random landmarks from the union of current target/source bounding boxes, padded by one internal sigma and resampled by draw. |
| `box_2` | Identical replay of `box_1`. |
| `points_2` | Identical replay of `points_1`. |

All use the same synthetic full fixture, 100,000 points per set, seed 1, eight threads and CPU affinity 0–7. These are deterministic replay pairs, not independent statistical replicates. The fixture applies bending, similarity transformation, noise, and clutter at every twentieth target index.

Nyström starts at 256 landmarks and doubles after rejection, subject to the cumulative half-dense cost budget. For these dimensions, 256, 512 and 1024 fit; 2048 does not. Rejection can lead to dense/truncated fallback, and rejection memory suppresses subsequent attempts at smaller sigma².

Each cell captures the lowest-index invalid target row from its **first failed target pass**. With shifted source weights \(w_i\), it measures
\(S=k(x,Z)W^+\sum_i w_i k(Z,y_i)\).
The capture condition is nonpositive or nonfinite \(S\), before adding the outlier component. Chunk traversal plus atomic minimum correctly selects the lowest invalid index under the uncancelled execution path; thread completion precedes serialization.

The analyzer checks bitwise native row and \(P a\) replay, then compares extended-precision dot products, saved eigensystem factors, re-diagonalization of the saved Gram matrix, and rebuilt Gaussian kernels using matched rank or their own cutoff. It also computes the exact Gaussian row through log-sum-exp, signed source contributions, cancellation, spectral diagnostics, landmark coverage, projection residuals, and exact inlier mass.

Affinities, eigenvalues, mass shares, residuals, relative errors and condition ratios are dimensionless; logarithms are natural logs. Snapshot sigma² uses normalized coordinate units squared; distances divided by sigma are dimensionless. The report labels iteration sigma² as world units squared. Endpoint RMS uses fixture/world units. Times are seconds; cumulative completed-child maximum RSS is KiB. Neither is a performance comparison.

Expected degenerate signatures include:

- **No captured row:** unresolved; this can mean positive target sums, another rejection stage, or suppressed attempts.
- **Kernel truncation/poor coverage:** zero native kernels, large landmark distances, and potentially substantial exact row mass despite negligible approximation.
- **Signed approximation failure:** negative reconstructed mass despite positive exact Gaussian affinity. Agreement across arithmetic paths supports persistence, not exact-arithmetic proof.
- **Cancellation/rank sensitivity:** large positive/negative contributions, large cancellation ratio, cutoff-mask changes or sign disagreement.

A selected row cannot establish failure prevalence, clutter enrichment, convergence quality, or superiority between variants. Its failed small-landmark attempt may subsequently succeed at a larger count. Extended precision cannot recover upstream coordinate precision or uniquely separate conditioning, coverage and truncation effects.

Concrete implementation findings:

1. **Execution gates disappear under Python optimization.** [Driver lines 139–161](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/diagnose.py:139) use `assert` for approval, manifest and output checks. With inherited `PYTHONOPTIMIZE`, even the manifest subprocess expression is omitted. Required gates should use explicit exceptions or explicitly reject optimized execution.

2. **Successful early convergence aborts the remaining plan.** The driver requires the observed iteration count to equal the configured maximum. A successful shorter result therefore becomes a technical failure. Layer B must establish whether exact-length prefixes are necessary; otherwise this conflicts with continuing after scientific outcomes.

3. **Snapshot validation is incomplete.** [Analyzer line 85](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/analyze.cpp:85) does not validate target coordinates, log constants, mask values, or several saved vectors/matrices; positive infinity also passes the sigma² positivity check. Malformed operands can reach calculations or reporting instead of a clear rejection. Deliberately nonfinite captured failure values should remain distinguishable from malformed inputs.

4. **Failure-stage logging is incomplete.** [EStep early returns](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1430) for empty landmarks, eigensolver failure and invalid spectral maximum share a generic failed-attempt record. They lack distinct stage tags despite Layer A’s broader logging description.

The analyzer’s self-checks cover positive reconstruction, well-conditioned negative cross-affinity, missing coverage and near-dependence. They do not exercise snapshot capture, parser rejection, report classification or driver failure handling end to end. The report’s assertion that the Bayesian objective has zero extra prior contribution is not independently established by the permitted implementation files.

Ponytail pass: the standalone analyzer and reused trace parser are proportionate. No additional framework is warranted. The per-target `failedSums` array could be per-chunk; that is optional simplification.

Skipped: execution, CI/network, binary verification and Layer B; runtime correctness and intent alignment remain unverified.  
**RECONSTRUCTED — no experiment PASS issued.**