# Candidate screen and publication transfers

This supports `proposal.md`; it is not an implementation plan adopted by the
operator. Search cutoff: 2026-09-24. Sources: original HKTex paper/supplement and
upstream code, author/project pages, SIAM/NIST/Microsoft Research publications,
arXiv, and author-hosted papers. Searches used HKTex compilation/barycentric
evaluation; partial evaluation; random-access vector graphics; Bernstein range
enclosures; polynomial heat convolution; reduced basis; variable projection;
EWA, Mip-Splatting, mip-NeRF and normal-distribution filtering. No patent or
exhaustive thesis audit was performed. An author book-page fetch timed out once;
its indexed page and author publication list corroborated the basic mechanism.

No global novelty claim or numerical probability of novelty is justified. The
strongest prior-art threats are ordinary shader specialization, Nehab–Hoppe's
cell streams, finite-element interpolation identities, adaptive approximation,
Ptex/Htex/Mesh Colors and standard numerical model reduction. Success is useful
engineering even if all components are known.

## Independent candidate lanes

Each claim below is a hypothesis or deduction awaiting implementation validation.
N0/N1 denote known mechanisms; N2 denotes an exploratory combination, not a claim
that nobody has used it. Three representation-changing candidates were generated
but downgraded because a distinct novelty claim did not survive the subtraction
test. No N4 evidence discovery occurred: no experiment was run.

| ID / lane | Candidate and hypothesis | Foundation / irreducible adaptation | Cheapest killing test and negative value |
|---|---|---|---|
| A1 / recombination | Frozen-kernel compiler removes spectral work per sample without changing the selected reference function | N1: partial evaluation + HKTex; specialize heat and distance on a face, preserve full selection/postprocessing | Compare CPU outputs/ranks at boundaries and random points; failure identifies an omitted dependency rather than proving HKTex impossible |
| A2 / recombination | Certified cell lists replace global nearest-source search | N1: branch-and-bound + convex quadratics; keep nearest-k supersets rather than center-containing faces | Broad overlapping kernels and distance ties; long lists expose the cost region where this is unhelpful |
| A3 / recombination | Inner color solves reduce fitting work | N1: variable projection; respect clipping, bounded values and base-color rank ambiguity | Equal-error fit curves with fixed sample/seed budgets; a negative result still identifies the true nonlinear bottleneck |
| A4 / recombination | Directional moments improve filtered normal appearance | N2: LEAN + typed HK fields; object-space transport and actual BRDF require adaptation | Grazing specular sweeps against supersampling; failure bounds applicability rather than just renormalizing away variance |
| B1 / assumption surgery | Replace full spectral basis with sparse polynomial operator actions | N2-T: cortical heat convolution; anisotropic mass-scaled operators and source derivatives | Anisotropy/mesh-quality sweep versus chosen reference; disagreement may require a new generator and refit |
| B2 / assumption surgery | Replace fixed 49-grid preparation with a query-adaptive reduced basis | N2-T: parametric PDE reduction; certify unseen source/orientation queries | Held-out extreme anisotropies and clustered eigenvalues; basis explosion kills the intended savings |
| B3 / assumption surgery | Replace global metric recomputation during animation with rest-domain attachment | N0: standard material attachment; separate transported appearance from current-metric diffusion | Stretch/remesh controls; a semantic mismatch rejects this mode for intrinsic simulation fields |
| B4 / assumption surgery | Optimize deployment cost rather than number of kernels | N1: codec rate-distortion allocation; actual compiled records and expensive cells enter the objective | Hold quality fixed and compare bytes/list tails and measured GPU time; no improvement rejects the surrogate |
| C1 / primitive attempt | A texture query returns a footprint integral plus error estimate rather than point color | N3 attempt, downgraded N2: footprint-aware fields already exist; HKTex's nonlinear composition is the unresolved adaptation | Rapid zoom and thin subtriangle patterns; exposes aliasing or an unaffordable integration hierarchy |
| C2 / primitive attempt | A texture cell carries a candidate-completeness and color-range certificate | N3-T attempt, downgraded N2-T: verified numerics changes the unit of deployment to an evaluable region with a bound | Adversarial candidate switch surfaces and outward-rounding tests; failure shows certificates are loose or unsound |
| C3 / primitive attempt | Exact property plus bounded HK residual becomes the appearance representation | N3 attempt, downgraded N1: residual coding and typed fields are known; maintain exact property authority explicitly | Zero-residual fixture must reproduce exact domain data; extra overhead without finer content rejects that use |
| C4 / primitive attempt | Edits produce certified affected-region updates rather than unconditional global refits | N3 attempt, downgraded N2: incremental computation; global heat tails force impact bounds | Move a broad kernel across a thin handle; widespread invalidation rejects assumed locality |
| D1 / evidence design | Compile identity and ranking map | Vary faces, source positions, powers, ties, coverage and clamps; observe worst error and rank mismatches | Independent direct spectral oracle, double precision, exact affine/quadratic controls, pinned source; any mismatch blocks optimization claims |
| D2 / evidence design | Compiled-cost phase diagram | Vary detail frequency, kernel overlap, mesh resolution, curvature and source count; observe memory and candidate tails | Match reconstruction error and count every artifact; broad kernels on coarse faces are a deliberate negative control |
| D3 / evidence design | Footprint and temporal stability map | Vary scale, grazing angles, face crossings and occlusion; observe color error and flicker | Converged independent supersampling, disjoint fitting/test views and fixed exposure; failure chooses a sampled hybrid over a misleading no-mip claim |

The most surprising useful outcome in D2 would be compiled incidence count growing
with geometric tessellation rather than signal complexity, despite unchanged
images. That would motivate shared-vertex/region storage or a different runtime
representation. D3 could reveal that top-k selection itself creates temporal
discontinuities; that would motivate a separately fitted smooth-selection model,
not blaming the antialiasing code. Neither observation has occurred here.

## Six donor-to-recipient transfers

These include compiler theory, numerical verification, medical imaging, inverse
problems, codec optimization and parametric simulation. The first three provide
mechanisms beyond the usual texture-representation toolkit. Each mapping states
at least four structural roles and where the analogy fails.

| Transfer / donor | State and observation | Operator and invariant | Broken correspondence / required invention | Prediction, barrier and counterexample |
|---|---|---|---|---|
| E1: partial evaluation | General program + static input → fitted kernel + fixed mesh; output equivalence → color/rank parity | Specialize constants → compile coefficients; preserve program meaning → preserve reference expression | Dynamic kernel edits, KNN memberships and floating-point reordering remain; need candidate certificates | No spectral-dimension loop during drawing; storage expansion and broad support may outweigh saved arithmetic |
| E2: verified range analysis | Polynomial domain boxes/simplexes → barycentric cells; true extrema → kernel-distance bounds | Subdivide and enclose → safe culling; sound enclosure → no excluded nearest-k source | Nonlinear sigmoid, rounding and ranking are outside a raw polynomial bound; compose interval bounds and retain ties | Zero false culls in adversarial tests; bounds may be too loose to improve cost |
| E3: cortical diffusion | Mesh signal and sparse Laplacian → kernel source and anisotropic surface operator; smoothing result → fitted footprint | Polynomial matrix-function action avoids eigenbasis; controlled operator approximation must survive mass scaling | Published donor is not the interpolated truncated HKTex family; needs a new validated generator | Lower preprocessing storage at some degrees; anisotropy/poor conditioning can force high degree or break positivity |
| E4: separable inverse problems | Linear amplitudes and nonlinear exponential parameters → colors and kernel shapes; residual → texture fit error | Eliminate linear variables inside nonlinear solve; fit objective must remain the same | Clipping, robust losses, inverse lighting and bounds invalidate naive unconstrained least squares | Fewer wasted color-gradient iterations; solve overhead or unstable rank may erase advantage |
| E5: rate-distortion coding | Blocks and coding modes → cells and kernel/residual choices; reconstruction error and rate → visual error and compiled bytes | Allocate modes under a Lagrangian; hard fidelity constraints remain mandatory | Bytes are not rendering cost; need an additional measured-cost surrogate and visibility workload | Smaller expensive-cell tail at equal quality; surrogate failure can worsen GPU divergence |
| E6: reduced-order simulation | Parametric solution family → anisotropic heat-query family; residual estimator → reconstruction/operator error | Build a greedy snapshot basis; validate online approximation on held-out queries | Global kernel motion and truncation/alignment discontinuities may defeat small rank; DEIM positivity is not guaranteed | Some parameter ranges admit compact preparation; broad family/high-frequency demand kills reduction |

The causal mechanisms survive without donor jargon: precompute fixed work,
discard only impossible contributors, apply an operator without constructing its
full basis, solve the easy variables exactly, allocate cost where error demands
it, and reuse low-dimensional structure only where observed. None licenses a
claimed HKTex speedup without an executed comparison.

E2 and D1 transfer a validation protocol, not only an acceleration algorithm.
C2 is the candidate that changes the formulation from a point-sampling asset to
an asset carrying region-level guarantees; novelty remains unestablished.

## Scientific-value screen (subjective 0–5 priorities, not probabilities)

| Candidate | Apparent novelty | Falsifiability | Explanatory value | Importance | Feasibility | Cheap first test | Interpretable result | Baselines | Useful negative | Publication potential |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A1+A2 compiler/candidates | 1 | 5 | 5 | 4 | 4 | 5 | 5 | 5 | 5 | 3 |
| C1 footprint hierarchy | 2 | 5 | 4 | 5 | 2 | 3 | 4 | 5 | 5 | 4 |
| B1 polynomial generator | 1 | 5 | 4 | 3 | 3 | 3 | 4 | 4 | 4 | 2 |
| A3 variable projection | 1 | 5 | 3 | 3 | 4 | 4 | 5 | 5 | 4 | 2 |
| B4 deployment objective | 1 | 5 | 4 | 4 | 3 | 3 | 4 | 4 | 5 | 3 |
| C2 certified asset | 2 | 5 | 5 | 4 | 2 | 3 | 4 | 4 | 5 | 4 |

Pareto choices: A1+A2 for quickest validation and systems value; C2 for theory;
C1 for high risk/high reward; D2 for informative negative results. Success might
support a useful compiler/filtering system; partial success may support albedo-only
or static-asset use; failure can still provide a reproducible cost/filtering
boundary. Publication is not guaranteed and is not the operator's requested goal.

First experiment: D1, followed immediately by D2 before constructing a large
runtime architecture. Preserve negative outcomes and compare at matched quality.
