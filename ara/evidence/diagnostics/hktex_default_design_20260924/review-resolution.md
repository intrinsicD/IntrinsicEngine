# Discussion corrections and evidence boundary

The retained Claude responses are raw deliberation, not independently verified
implementation results. `proposal.md` is the corrected recommendation.

| Issue raised in discussion | Disposition |
|---|---|
| Frozen-kernel affine heat and quadratic distance | Retained as algebraic derivations under fixed source/basis/configuration. Numerical and full-pipeline parity still need testing. |
| Shared distance coefficients | Retained: squared embedding lengths on triangle edges are kernel-independent, while vertex-to-source distances are kernel-specific. |
| Static lists cannot preserve the paper | Corrected: static candidate **supersets**, followed by the original query-time rankings, can preserve the selected exact-KNN semantics. Static active sets need not equal membership everywhere. |
| Safe candidate bound | Retained: exclude only when the source's lower distance bound exceeds the k-th smallest upper bound. Conservative rounding and ties are required. |
| Tiny denominator in Eq. 7 | Reference-code inspection found a floor of 1. Pin that code rather than silently reproducing the displayed equation. |
| Gaussian and nonlinear power order | Corrected: the inspected path weights heat with the Gaussian and then applies `power_diffused_diracs`. Moving the exponent before the Gaussian changes the function. |
| Mandatory per-pixel evaluation of all 50 candidates | Qualified: that is a straightforward parity path, not a universal lower bound. Fixed-order cells and certified final-color specialization can avoid work. |
| Ranking ambiguity means acceptable parity | Rejected. Small distance changes can produce large color jumps. A deterministic semantic oracle and an output tolerance are separate requirements; ambiguity cannot waive the latter. |
| Wendland window as a parity-preserving fix | Rejected for the first route: it changes the model and needs refitting. A radial Wendland expression in sqrt(q) is not generally a polynomial in barycentrics. |
| Separate min-distance and max-heat tests exactly characterize overlap | Corrected to conservative necessary tests; their extrema need not occur at the same location. |
| Sigmoid-threshold example suggesting all negative inputs qualify | Retracted during discussion. The rescaled soft step is zero at zero; signed values and power domains still need explicit handling. |
| Per-kernel widening or filtered-weight ratios solve antialiasing | Rejected as a proof. The final composition must be integrated or approximated with a stated error contract. |
| Uniform signal approximation alone solves aliasing | Corrected: a uniform bound transfers to a normalized footprint integral only if the approximant is actually integrated, not point-sampled. |
| A simple vertex color pyramid captures arbitrary fine detail | Rejected. The coarse cache must summarize the complete surface signal, including subtriangle content and crossing cells/faces. |
| Streaming four basis nodes solves preparation memory | Qualified: alignment, persisted bases, preparation working sets and I/O remain; measure all of them. |
| Polynomial heat actions or a tufted Laplacian preserve paper fits | Rejected as automatic parity. Both may define a different generator and require refitting; scalar robustness does not establish anisotropic/transport robustness. |
| Every geometry edit needs a full refit | Qualified: retained rest-domain appearance can survive deformation. Recomputing the metric or changing topology has different invalidation/transfer obligations. |
| Kernel edits are local | Qualified by the old/new ranking footprint and possible global influence. Color-only edits need not change geometric records. |
| LEAN directly solves object-space normal filtering in the engine | Rejected as established capability. Directional moment fitting, frame transport and BRDF mapping require their own reference and tests. |

## Evidence audit

- Executed: literature retrieval, source inspection at a pinned upstream revision,
  and actual Claude CLI consultation with explicit model/effort.
- Not executed: HKTex fitting, compiler implementation, numerical parity probe,
  timing/memory benchmark, mesh-corpus trial or GPU rendering.
- No engine default or source/build files changed. No operational, performance,
  compression or general compatibility result is asserted.
- Future artifacts must count spectral preparation separately from compiled
  runtime bytes and distinguish exact reference evaluation, bounded approximation,
  a refitted HKTex-derived model, and sampled hybrids.
- Research-manager recording keeps recommendations staged. Structural repository
  checks validate the notes' structure, not the mathematical or runtime claims.

## Publication audit boundary

Verified donor publications support mechanisms, not the proposed integration's
performance. The candidate screen records what transfers and where it breaks.
Unverified extra citations suggested by Claude are not needed for the preferred
route and are not treated as established evidence. No novelty claim is made.
