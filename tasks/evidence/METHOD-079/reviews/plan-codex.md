# Independent plan review
Reviewer: /root/nystrom_row_plan; requested gpt-6-astra/xhigh, fresh context; read-only and no-recursion instruction-only. Recorded answer (mathematical and link formatting normalized; verdict and findings unchanged):

Verdict: **revise**, with two small measurement clarifications. Scope and alignment are sound: every step serves the requested diagnosis, and no production fix is introduced.

Reviewed plan SHA256: `2ab48baedb7c7e4bd91efbd899854d45f214e6d2421a6547de702fe56b858f1d`.

1. **Separate arithmetic sensitivity from changed kernels and retained modes** (plan.md:11). Native kernels use `ExpNonPositive`, including a hard zero below exponent −708; rebuilding with `long double` Gaussian `exp` changes that behavior. Recomputed eigenvalues can also cross the spectral cutoff. Save native eigenvectors and the retained mask, then compare:
   - native operands/modes with extended-precision accumulation;
   - native Gram matrix with recomputed eigensystem;
   - reconstructed Gaussian kernels, reporting any rank/mask change separately.

   Report cutoff proximity and zeroed kernel entries. Describe 80-bit `long double` as a sensitivity check; agreement alone does not prove exact-arithmetic sign.

2. **Make target significance an explicit output** (plan.md:11). Alongside the exact sum, report the outlier component and exact target inlier mass, using the saved `LogWeightShift` consistently. For exact unshifted affinity sum A, report log(A), logC, and A/(A+exp(logC)), computed stably. This distinguishes a rejected approximation at an effectively irrelevant outlier from loss of meaningful inlier mass.

Ponytail: keep the temporary private capture, standalone offline calculation, four analytic controls, and existing runner/report reuse. No broader framework, public API, numerical fix, or extra landmark variants are needed. The deterministic “lowest invalid index” policy fits the current per-chunk early returns.

This is plan input, **not experiment authorization**. Read-only and no-recursion restrictions were instruction-only and observed.

Not checked: implementation, tests, CI, or scientific outputs; no runs or edits performed. The diagnostic may still leave the numerical cause unresolved.
