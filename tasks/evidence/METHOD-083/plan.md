# Implementation and comparison plan

1. "Bitte pass unsere implementieren entsprechend dieser Punkte des Original Codes an. Dann wiederhole den Vergleich, auch mit dem Original."

Serves the operator-directed CPD/BCPD investigation beyond Framework24 P0. The shortest
end-to-end slice changes the existing E-step and its existing settings lane, then compares
actual saved endpoints with the author's executable. No new backend framework or copied solver.

## Inspected reference and scope
Author repository `https://github.com/ohirose/bcpd`, pinned commit
`b7b6d01bb703e154f652b6a798c1eacd39b83ca3`. `base/gaussprod.c` uses a `1e-6` diagonal ridge
and Cholesky solves. `register/bcpd.c` draws one random permutation over source+target,
follows those point identities, and switches to local Gaussian sums below a sigma threshold
when enabled. It also floors P1 at1e-20, applies abs before the sigma square root, and stops
on sigma/absolute sigma change after a minimum loop count. Its default independent per-set
normalization differs from our shared scale. These differences must not be hidden in comparison.

An optional operator question asks whether to copy the P1/abs shortcuts too. Pending a
different answer, use the robust default: keep exact-row repair, nonfinite/sign checks,
sampled error checks and fallback; do not conceal invalid responsibilities. Existing
nonnegative sufficient-statistic behavior is a correctness contract, not a convergence result.

## Smallest proposed implementation (all steps serve Q1)
1. Replace only the E-step's materialized spectral pseudoinverse with LLT solves of
   `W + 1e-6 I`. Reuse Eigen already linked; do not change deformation-kernel approximation.
2. Add a union-random landmark source to the existing source enum. Use the canonical seeded
   random permutation owner; fixed identities follow moving source points. Preserve FPS and
   box choices so the requested comparison and existing UI capabilities remain available.
3. Add a sigma threshold for the Nyström-to-neighbor transition (0 disables, default .2 in
   solver coordinate units). Route to the existing tolerance-controlled Truncated evaluator;
   its conservative tail handling stays intact instead of silently claiming upstream's
   hard radius cap has the same error guarantee. Keep requested/actual backend reporting.
4. Reuse existing shared config field table, runtime parameter mapping and panel. Add enum
   round-trip/invalid-value checks and threshold validation. Update docs and module inventory.
5. Analytic E-step oracle explicitly assembles the regularized kernel and any exact row
   replacements. Tests distinguish ridge removal, union vs forced half-per-cloud sampling,
   fixed moving identities, and switching at the threshold. Run before/after or mutations,
   then the focused accelerated/Bayesian and affected runtime suites. Independent code review.

Do not silently retune omega/lambda/beta, change all solvers' stopping semantics, or change
the underlying fixture after seeing results. Original numerical shortcuts, hard radius
cutoff and native stopping remain documented differences unless the operator selects them.

## Reuse and reach
Owner: `Geometry.Registration.CoherentPointDrift.EStep.cpp::NystromRows/BuildLandmarks/Evaluate`.
Existing `Geometry.PointSampling` owns uniform permutations; inspect the direct random owner
before adding code. Existing `AddExactRows` owns consistent repair; existing Truncated path
owns spatial queries and bounds. Public Params/Settings, runtime config codec/schema,
registration operation and Sandbox panel all need the new enum/threshold. No dependency,
layout, backend ownership, asset, topology or publication change. No user files are replaced.
Potential breakage: numerical trajectories, seed interpretation for the new source only,
and config validation/default behavior. Regression controls bind these intentionally.

## Experiment design to freeze after code verification
Reuse METHOD-082's stored full/partial fixtures, all three seeds, complete endpoint metrics,
truth/identity/spread and observed/hidden subset controls, replay checks and trace reporting.
Preserve prior artifacts. Reuse the native diagnostic runner's loading/trace shape where it
fits; its fixture generator alone cannot guarantee identical serialized inputs across engines,
so add the smallest explicit input adapter. Build a pinned, unmodified upstream executable
with recorded compiler/OpenMP/BLAS bindings. Test its final configuration with pass/fail only.

Compare old/adapted Auto and existing FPS/Box arms, the new union-random arm, and upstream
with Nyström+KD search; include an upstream no-Nyström control where its enforced budget can
support a meaningful endpoint. Use a common explicitly normalized coordinate frame and
disable internal normalization to avoid conflating code changes with differing length units.
Separate native stop labels from shared post-hoc quality metrics; identical tolerance numbers
are not equivalent stopping rules. A deterministic small full-rank fixture checks input,
coordinate conversion and shared mathematical updates before scientific interpretation.

The frozen packet must resolve exact cells, effective rank, seeds, normalization, native
stop settings, maximum5000 iterations, generous enforced time/memory limits, all raw traces
and HTML output. Use two fresh Codex preflight reviewers per the operator's no-Claude choice.
No run before both PASS. Unexpected scientific outcomes remain in the report; no adaptive
parameter search. An independent raw referee must accept the final bounded interpretation.

Done means the implementation and requested comparison are reviewed and recorded, even if
partial-overlap quality remains poor; it never means hiding timeouts or declaring universal parity.
