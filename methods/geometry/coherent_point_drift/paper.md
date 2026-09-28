# Paper Intake — Coherent Point Drift

## Citation

- **Title:** Point Set Registration: Coherent Point Drift
- **Authors:** Andriy Myronenko, Xubo Song
- **Venue / Year:** IEEE TPAMI 32(12), 2010
- **DOI:** 10.1109/TPAMI.2010.46
- **URL:** https://doi.org/10.1109/TPAMI.2010.46

Related work reviewed for scope: Hirose, *A Bayesian Formulation of Coherent Point
Drift*, TPAMI 2021 (METHOD-050); Greengard & Strain 1991 and Yang et al. 2003 (fast
Gauss transform) and the paper's low-rank Gram approximation (METHOD-049). Framework24's
`bcg_coherent_point_drift*.h` headers were used only as a feature checklist; no code
was copied, and the implementation follows the paper's equations.

## Core claim

Registering two point sets is maximum-likelihood fitting of a Gaussian mixture whose
centroids are the moving (source) points to the fixed (target) points. EM alternates
soft correspondences (E-step) with a closed-form transform update (M-step); for
nonrigid motion a Gaussian-kernel regularizer makes nearby centroids move coherently.

## Mathematical formulation

Target X = {x_n}, n = 1..N (fixed); source Y = {y_m}, m = 1..M (moving); D = 3.
Mixture density with a uniform outlier component of weight w in [0, 1):

p(x) = w / N + (1 - w) / M * sum_m N(x | T(y_m), sigma^2 I).

E-step (eq. 7): P(m | x_n) = exp(-|x_n - T(y_m)|^2 / 2 sigma^2) /
(sum_k exp(-|x_n - T(y_k)|^2 / 2 sigma^2) + c), c = (2 pi sigma^2)^{D/2} w/(1-w) M/N.
Sufficient statistics: P1 = P 1 (M), Pt1 = P^T 1 (N), PX = P X (M x D), Np = 1^T P1.
The implementation streams P row by row with a log-sum-exp shift (it never stores P) and
reports NLL = -sum_n log p(x_n) at the parameters entering each iteration.

M-steps (A = sum_mn P_mn (x_n - mu_x)(y_m - mu_y)^T with P-weighted means mu_x, mu_y):
- **Rigid** (§4): A = U S V^T, C = diag(1, 1, det(U V^T)) (identity when reflections are
  allowed), R = U C V^T, s = tr(S C) / sum_m P1_m |y_m - mu_y|^2 (1 without scale
  estimation), t = mu_x - s R mu_y, sigma^2 = (sum_n Pt1_n |x_n - mu_x|^2 - 2 s tr(S C) +
  s^2 sum_m P1_m |y_m - mu_y|^2) / (Np D).
- **Affine** (§5): B = A (sum_m P1_m y^_m y^_m^T)^{-1}, t = mu_x - B mu_y,
  sigma^2 = (sum_n Pt1_n |x^_n|^2 - tr(A B^T)) / (Np D).
- **Nonrigid** (§6): T(Y) = Y + G W with G_ij = exp(-|y_i - y_j|^2 / 2 beta^2);
  (d(P1) G + lambda sigma^2 I) W = PX - d(P1) Y; sigma^2 = (sum Pt1 |x|^2 - 2 tr(PX^T T) +
  tr(T^T d(P1) T)) / (Np D). The objective adds lambda/2 tr(W^T G W).

Stopping: relative objective change |L_k - L_{k-1}| <= tol |L_k| (Converged), sigma^2
at or below the floor (SigmaFloor; the floor replaces the value instead of collapsing to
zero or NaN), or the iteration cap (IterationCap).

## Inputs and outputs

- Inputs: target and source float3 spans in world units (any canonical point domain:
  mesh vertices, graph nodes, point-cloud points, derived face centers or midpoints),
  `Params` (variant, w, MaxIterations, Tolerance, InitialSigma2, Sigma2Floor,
  NormalizeInputs, EstimateScale, AllowReflection, Beta, Lambda).
- Normalization (default on): each set is centered on its own mean and both are divided
  by one shared scale (the larger RMS radius), so the relative scale is preserved and
  Beta, Lambda, InitialSigma2 and Sigma2Floor are dimensionless. Outputs are always in
  world units (sigma^2 in world units squared).
- Outputs: `Result` with status, termination, iterations, sigma^2, NLL, matched weight
  Np, rigid rotation/scale/translation or affine matrix/translation as `Transform`, the
  transformed source positions (every variant), objective and sigma^2 histories, and
  backend identity `cpu_reference`.

## Degenerate/edge cases

- Empty inputs: `EmptyInput`. Non-finite coordinates: `NonFiniteInput`.
- w outside [0, 1), zero iterations, negative tolerance or sigma values, non-positive
  beta/lambda: `InvalidParameters`.
- Nonrigid with more than 8192 source points: `TooLarge` (O(M^2) memory, O(M^3) solve).
- Affine with a weighted source spanning fewer than three dimensions, or a nonrigid
  system without a finite solution: `SingularSystem`.
- All target mass assigned to the outlier term, or a non-finite update: `NumericalFailure`.
- Coincident sets: sigma^2 reaches the floor and the run ends with `SigmaFloor`.
- Failures publish no transform or positions.

## Implementation notes

- CPU reference: single-threaded, deterministic, structure-of-arrays doubles, no stored
  P matrix. Per iteration: N*M distance/exponential evaluations; rigid/affine M-steps are
  O(N + M); nonrigid adds a dense LU (Eigen `PartialPivLU`) of the M x M system.
- The E-step is verified against GEOM-058: with w = 0 the first NLL equals
  `Geometry::GaussianMixture::LogLikelihood` of the equal-weight isotropic mixture. The
  CPD E-step does not call the GEOM-058 per-point `Responsibilities` because that surface
  has no uniform outlier term and allocates per point, which would dominate the O(N*M)
  loop.
- EM is local. From an identity start at the data-derived sigma^2, a mirrored target
  settles at a proper rotation even with reflections allowed (confirmed with an
  independent NumPy implementation); a small InitialSigma2 near the right pose recovers
  the reflection.
- The reference uses no spatial index; the truncated METHOD-049 E-step below carries the
  required tail bound (`docs/architecture/spatial-index-consumers.md`).

## Accelerated backends (METHOD-049)

Opt-in through `Params::EStep` and `Params::LowRank`; the reference stays canonical and
every optimized run reports its backend (`cpu_dense_parallel`, `cpu_truncated`,
`cpu_ifgt`, `cpu_auto`), the policy each iteration used, and its error bound.

**Blocked single pass.** Target rows are split into B fixed blocks (B depends only on N
and M, at most 32). Each row computes its shifted terms e_m = exp((d_min^2 - d_m^2) /
2 sigma^2) once, as the reference does (d_min comes from a kd-tree query with the same
arithmetic, so the largest term is exactly 1), normalizes them by its denominator and
adds P_mn, P_mn x_n to its block's partial P1/PX; partials are reduced in block order.
Results are therefore bitwise identical for any thread count and differ from the
reference only by summation order. Dense rows use this form only while a block's partials
(4 M doubles) stay within 512 KiB, because each dense row writes all of them; larger
sources, and any case beyond `PartialBudgetBytes`, use a two-pass form (denominators
over targets, then P1/PX over sources, reading shared arrays), which evaluates each
kernel term twice. Truncated rows touch only their kept sources and stay single-pass.

**Truncation bound.** For row n let d_n = min_m |x_n - y_m| and keep the sources with
|x_n - y_m|^2 <= r_n^2 = d_n^2 + 2 sigma^2 ln(M / tol). Every dropped term is at most
exp(-r_n^2 / 2 sigma^2) = (tol / M) exp(-d_n^2 / 2 sigma^2), i.e. tol/M times the largest
kept term, so the dropped mass is at most ((M - k_n)/M) tol times the kept mass
(k_n kept terms). The row denominator (kept mass plus the outlier constant c) therefore
has relative error at most ((M - k_n)/M) tol / (S_n + c e^{-a_max}) <= tol, where S_n >= 1
is the shifted kept sum. The run reports the maximum over rows and iterations
(`EStepErrorBound`); kept responsibilities inherit the same relative bound. Neighborhoods
come from a balanced double-precision kd-tree over the moving source, rebuilt per
iteration. `Auto` counts the kept pairs of 64 evenly spaced rows and truncates when they
are under 40% of all pairs; otherwise it uses the fast Gauss transform if its plan is
clearly cheaper, else the dense rows.

**Exponential.** Optimized rows use a branch-free exp for non-positive arguments
(magic-constant rounding, Cody-Waite reduction, degree-13 Taylor polynomial on
|r| <= ln 2 / 2; relative error below 3e-16, arguments below -708 give 0). The row kernels
(distances, exponentials, scatter) are compiled for AVX2 and a baseline target
(`target_clones`), so they vectorize on either; sums stay in index order.

**Low-rank nonrigid.** G ~= Q L Q^T from a Nystroem approximation: max(2k, k + 32)
farthest-point landmarks Z, W = G(Z, Z) = U S U^T on its positive spectrum, F = G(Y, Z) U
S^{-1/2} (formed in row blocks, never stored), eigenpairs of F^T F give the k leading
(Q, L). The M-step solves (d(P1) Q L Q^T + a I) W = F_rhs, a = lambda sigma^2, with the
Woodbury identity W = (F_rhs - d(P1) Q (a L^{-1} + Q^T d(P1) Q)^{-1} Q^T F_rhs) / a in
O(M k^2); T(Y) = Y + Q L (Q^T W) and the coherence term is lambda/2 tr((Q^T W)^T L
(Q^T W)). `KernelApproximationError` is sqrt(sum |g - g~|^2 / sum |g|^2) over 32 exact
kernel rows. This lifts the nonrigid limit to 1,000,000 source points.

**Fast Gauss transform (`FastGauss`, IFGT).** Both passes as improved fast Gauss
transforms (Yang et al. 2003): sources grouped by farthest-point clustering, the kernel
factored as exp(-|dt|^2/h^2) exp(-|ds|^2/h^2) exp(2 dt.ds/h^2) with the last factor's
Taylor series truncated below total degree p (coefficients 2^|a|/a!), and clusters farther
than r_y from a target skipped. Per unit of source weight the error is at most
(2 r_x r_y / h^2)^p / p! + exp(-(r_y - r_x)^2 / h^2) (Raykar et al. 2005): the remainder
of exp(z) is at most |z|^p/p! e^|z|, and e^(|z| - |dt|^2/h^2 - |ds|^2/h^2) <= 1. The plan (K, p,
r_y) minimizes (sources + targets K) terms under that bound, aimed at tol times the
previous E-step's 1% quantile of den_n / M (pass 1, weights 1) and of P1_m / sum_n w_n
(pass 2, weights w_n = 1/den_n and x_n / den_n). A posteriori each denominator's
relative error is M e1 / den_n and each P1 entry's is e2 sum_n w_n / P1_m; entries above
tol are recomputed exactly, so the reported bound (denominator plus P1, at most 2 tol) is
rigorous. Measured on the scaling fixture at tol 1e-6 the plans need p = 20-24 (up to
2300 terms per cluster and channel) and exceed the dense cost at 10^4 points; `Auto`
therefore rarely selects it in 3-D, and the explicit policy is slower than dense there
(`geometry.coherent_point_drift.accelerated`). E-step Nystroem is not offered because its
error has no a-priori bound; the permutohedral lattice (GEOM-060) is the remaining
candidate for wide kernels.