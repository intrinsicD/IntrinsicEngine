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
- Spatial indices are not used: truncating Gaussian responsibilities needs a proven tail
  bound (METHOD-049), see `docs/architecture/spatial-index-consumers.md`.
