# Making HKTex a credible IntrinsicEngine default

Status: research proposal, 2026-09-24. No engine implementation, benchmark,
default change, or new capability result. The operator requested solutions and
publication-based adaptations, not merely an assessment of the original paper.
Claude Opus 5.5 at high effort is the discussion partner. Retained responses are
advisory; the qualifications below take precedence over their unchecked claims.

## Recommendation and meaning of default

Develop a **compiled HKTex evaluator** first. Keep spectral fitting as an offline
producer, specialize its fixed computations into triangle-local coefficients,
and render those coefficients. This is a concrete route to removing global KNN
and eigenbasis evaluation from the draw path while preserving subtriangle detail.
It is not yet evidence that the resulting representation is fast or small.

Pin the initial oracle to upstream commit
`9e942972c5411a364f12563eb6864520f9effe70`, especially
[the KNN texture evaluator](https://github.com/circle-group/hktex/blob/9e942972c5411a364f12563eb6864520f9effe70/hktex/modules/heat_kernel_texture_knn.py)
and its configuration. It clamps the blend denominator to at least 1 and the
final RGB to [0,1]; this differs from the displayed paper equation. Gaussian
weighting precedes `power_diffused_diracs`. Configured outer/inner counts and
index type are part of the oracle, not hard-coded universal constants.

The intended default is the producer for newly generated continuous mesh
appearance that passes the quality and cost contract. Exact scientific values
and categorical IDs remain authoritative properties; authored UV materials keep
their interpretation. A real adoption decision must publish the eligible asset
population, success rate and exceptions. Calling an automatic atlas fallback
HKTex would not meet the objective. A universal replacement of every image,
normal, label, export and UV-editing workflow is not presently justified.

## Frontier and functional problem

| Existing approach | Useful primitive | Limitation relevant here |
|---|---|---|
| HKTex reference | Fitted surface kernels; spectral geometry; point evaluation | Expensive evaluation, preprocessing, dynamic candidate selection and unestablished footprint filtering |
| Engine UV atlas and property bake | Charted image storage with raw-value and coverage contracts | Chart construction, duplicates, gutters, distortion and image resolution |
| Direct mesh attributes | Exact source-domain interpolation or face lookup | No independent fine surface signal storage |
| Htex / Mesh Colors | Local surface samples without global unwrapping | Sample-resolution/storage tradeoff; a different runtime representation |
| Vector graphics cell specialization | Local lists of analytic primitives | HKTex has global tails, ranking and nonlinear normalization |

The domain-neutral problem is to compress a bounded signal on a piecewise smooth
domain, answer point and finite-footprint queries under tight latency and memory
budgets, and update the signal without altering authoritative observations.
Geometry is mostly fixed during drawing; optimization and topology edits run on
different timescales. Discrete labels, continuous scalars and directional
statistics have different semantics.

The obvious suggestions—port PyTorch to shaders, reduce eigenmodes, quantize
everything, add mips, or replace the fitter with a neural network—do not by
themselves solve this problem. None is the recommended first move.

## 1. Compile the fixed kernel, rather than port the research evaluator

This is our algebraic adaptation, not a measured result. Fix a mesh, spectral
configuration, kernel source, anisotropy and diffusion time. Let b be a triangle's
barycentric coordinates. Barycentric spectral interpolation makes the normalized
heat term a three-value interpolation:

    H_s(b) = sum_i b_i A_s(v_i).

Let z be the fixed biharmonic embedding and z_s the source embedding. Define
D_s(v_i)=||z(v_i)-z_s||² and E_ij=||z(v_i)-z(v_j)||². Then

    q_s(b) = ||sum_i b_i z(v_i)-z_s||²
           = sum_i b_i D_s(v_i) - sum_(i<j) b_i b_j E_ij.

The identity follows by expanding the squared norm and using sum b_i=1. E is
shared across kernels. Store A and D per retained kernel/vertex, or duplicate six
scalars into face-major records when coherence is worth the storage. Evaluate
H_s(b) exp(-q_s(b)/(2 sigma²)), followed by the selected reference's power,
soft-step, rankings, normalization and channel postprocessing. Constants and
epsilon terms must be carried through; do not silently substitute paper formulas.

This removes spectral dimension from the per-candidate runtime computation.
It does not remove exponential/sigmoid evaluation, candidate traversal, ranking,
or memory traffic. A complete compiled asset can avoid loading the spectral
cache in the renderer; the authoring process still needs its own working data.

Donor: [partial evaluation](https://studwww.itu.dk/people/sestoft/pebook/).
The transferred mechanism is specializing computations with fixed inputs, not
changing the fitted signal. [Random-access vector graphics](https://hhoppe.com/ravg.pdf)
supplies the closer rendering precedent: localized primitive streams with
adaptive evaluation. Our required adaptation is curved-domain cell addressing
plus HKTex's distance/ranking semantics. This is known-component engineering,
with no global novelty claim.

## 2. Replace global KNN with certified local candidate supersets

Preserve the reference's outer distance ranking and inner contribution ranking
first. A list of kernels whose centers lie in a face is insufficient.

For each barycentric cell T and source s, compute conservative bounds
l_s <= q_s(b) <= u_s for all b in T. The quadratic is convex; its maximum is at
a vertex, and its minimum can be obtained by testing the constrained interior
minimum and all edges. Conservative interval bounds also work. Let U_k be the
k-th smallest u_s. Exclude s only when l_s > U_k: at least k other sources are
always no farther than U_k. All possible nearest-k sources therefore remain.

At a query, evaluate distances in that superset, retain the original nearest k,
then perform the original top-contribution selection. Subdivide cells when their
lists are large. Bound rounding outward; preserve ties conservatively and specify
deterministic tie semantics. Shared quadratic terms cancel in pairwise distance
differences, so distance-order boundaries are affine inside a face. Explicit
order-k arrangements are a possible alternative, not the first implementation.

Donor: [simplicial Bernstein range bounds](https://www.reliable-computing.org/reliable-computing-25-pp-024-037.pdf).
Use interval arithmetic for exp and sigmoid around polynomial enclosures;
Bernstein bounds alone do not certify a transcendental function. Initially prune
only impossible nearest-k candidates, not small alpha values. Arbitrary alpha
cutoffs and changed top-k semantics are approximations requiring separate error
contracts and potentially refitting.

**Worst case:** every cell retains almost every source, or subdivision produces
too many records. Storage can approach the face-source product. Count compiled
records, duplicates, offsets, hierarchy nodes and shared arrays—not just kernels.
A subdivision limit returns a truthful budget failure; it never silently drops
contributors. Global heat tails also prevent assuming that kernel movement has
strictly local effects.

**Flat-region shortcut:** after the exact compiler, add a separately measured
error-bounded specialization. Bound final color over the cell, including all
possible rankings and clamps. If it is uniformly within epsilon of a constant,
store that constant and skip kernel evaluation. The reference-code denominator
floor is useful: for a candidate superset, let B_s bound
sup_T |alpha_s| ||c_s||. The sum of the largest inner-k B_s bounds the residual
numerator magnitude and hence its contribution after division by a denominator
at least 1. Clipping is nonexpansive. A small sum certifies the base-color shortcut,
even when distant kernels remain in the nearest-k set. Negative weights require
absolute-value bounds; undefined power inputs must fail validation.

This is a proposed conservative sufficient test, not a necessary condition for
uniform appearance. It can fail on cancellation or large but equal-color
contributions. Tighter bounds/subdivision or a general final-color polynomial
approximation are alternatives. Count all cached cells. Approximate mode reports
its tolerance and error evidence; exact mode does not take this shortcut. Near
ranking ties, a small distance discrepancy can cause a large color jump, so a
ranking ambiguity band never waives the output-error gate.

A particularly close donor is Keeter's
[region-specialized implicit rendering](https://www.mattkeeter.com/research/mpr/):
interval evaluation simplifies a large expression separately over spatial regions.
Transfer the specialization mechanism to HKTex color expressions and ranking
branches. Do not assume its implicit-surface rendering guarantees transfer to
HKTex. Initial implementation needs only fixed HKTex data records and evaluator
variants, not a new general-purpose bytecode VM or shader framework.

## 3. Make filtering a query of an area, not a blurred point sample

This is the hardest unresolved rendering part. The desired reference is an
integral of the **final composited and postprocessed color** over the pixel's
visible surface footprint. Averaging separately filtered weights and dividing
afterward is not generally equivalent. Likewise, widening a sigmoid or averaging
vertex samples does not reliably capture many tiny kernels inside one triangle.

Build a CPU adaptive footprint-integration oracle, split across triangle and
ranking boundaries, and compare it with converged supersampled renders. A
sample-difference stopping rule is an estimator, not a rigorous bound; certified
tests need interval enclosures, or an explicit empirical tolerance and independent
convergence control. Occlusion boundaries require visibility-aware sampling.

First fast candidate: reuse compiled cell lists for adaptive, footprint-aware
sampling of the final evaluator. Smooth low-variation cells use few samples;
unresolved detail uses more or a preintegrated coarse representation. Investigate
an adaptive hierarchy of final-color moments/polynomials for large footprints,
integrating the reconstruction over clipped footprint regions. Error must cover
the spatial reconstruction, quadrature, footprint approximation and transitions.
The hierarchy is derived storage and must be counted. If it becomes dense per-face
texels, call it a hybrid bake and compare it honestly with Htex.

There is a useful sufficient condition: if a reconstruction P approximates final
color C uniformly within epsilon, integrating P against a normalized nonnegative
footprint differs from integrating C by at most epsilon. This follows directly
from the triangle inequality. It requires **integrating** P, not point-sampling it.
Sum clipped integrals across cells/faces and bound geometric footprint error
separately. Top-k discontinuities may prevent a single smooth cell polynomial
meeting the bound; split those regions or use explicitly bounded area error.

For fully covered coarse nodes, stored integrals or low-order weighted moments
can summarize discontinuous detail without a uniform smooth approximation. For
partially covered nodes, refine or use a certified residual bound. A pixel
footprint spanning many tiny triangles needs a cross-face cluster hierarchy,
not just independent per-face trees. Coarse-cache storage, certified integration
cost and stable transitions remain open. Fine mode can retain analytic kernels;
the coarse cache need not resolve maximum-zoom detail as texels.

Donors: [EWA Surface Splatting](https://www.merl.com/publications/TR2001-20)
for anisotropic footprints; [mip-NeRF](https://jonbarron.info/mipnerf/) for
scale-aware queries; [Mip-Splatting](https://arxiv.org/abs/2311.16493) for
separating representation bandwidth from image sampling; and vector-graphics
local supersampling. Their formulas do not directly solve HKTex's nonlinear
ratio, clamping and selection boundaries. The adaptation remains research.

## 4. Fit faster and charge the actual deployment cost

For fixed supports and weights, unclipped predicted colors are linear in kernel
colors and base color. Use a constrained/regularized linear color solve inside
the nonlinear source-position/shape loop. Fix the base or handle its rank
ambiguity. This transfers [variable projection](https://www.nist.gov/publications/variable-projection-nonlinear-least-squares-problems).
Clipping, perceptual losses and inverse-rendering objectives are not automatically
linear; an inner constrained or nonlinear solve must reflect the actual objective.

Fit directly to queryable source mesh properties when photographs and lighting
inference are unnecessary. Warm-start edits; a color-only edit can reuse
geometric coefficients and candidate lists. Source/shape edits must update their
fields and certify changed support; topology edits require transfer/recompilation.
No assumption that every edit is local or cheap.

Transfer [rate-distortion optimization](https://www.microsoft.com/en-us/research/publication/rate-distortion-optimization-for-video-compression/)
from codecs: optimize measured distortion plus compiled bytes and an evaluation
cost surrogate, not kernel count alone. Keep hard quality limits and independently
measured runtime gates. Candidate count is not GPU time. A shared layout can
support many material channels, but one layout may fit different channels poorly.

## 5. Reduce preprocessing only after runtime compilation is viable

Keep the published spectral generator initially so representation changes are
not confused with compiler bugs. Cache its input/version/configuration identity.
Grouping kernels by anisotropy-grid cell may reduce active basis memory, but
alignment dependencies and I/O must be measured; four-node streaming is not an
established bound on total preprocessing memory.

If cold preparation remains unacceptable, investigate
[polynomial heat-operator actions](https://arxiv.org/abs/1911.02721), transferred
from cortical-surface analysis: approximate exp(-t L) applied to a source using
sparse matrix recurrences rather than eigenvectors. With a mass matrix, use a
consistent symmetric mass-scaled operator and spectral interval. Moving sources,
anisotropy, polynomial degree and batched-source storage remain costs. The result
is not automatically the paper's truncated, interpolated spectral field; refit
and validate it as a distinct generator.

A second option is a snapshot-based
[reduced basis](https://epubs.siam.org/doi/10.1137/100795772), with independent
error checks over anisotropy/source queries. Neither a compact global basis nor
positivity follows for free. DEIM-style hyper-reduction is an exploratory option
only if nonlinear operator evaluation still dominates.

## 6. Preserve channels, topology and editing semantics

- Continuous albedo and bounded scalar material channels can use typed fits,
  with per-channel units, ranges, losses and error thresholds. One RGB clamp is
  not an acceptable general property encoding.
- Store exact vertex/face signals directly, optionally with an explicitly named
  HKTex residual for genuinely finer content. Never approximate authoritative
  labels/IDs merely to make a default appear universal.
- For object-space normal detail, reconstruct a vector with an explicit
  degeneracy rule and deformation transform. Renormalization alone does not
  preserve minified specular appearance. Investigate directional moments using
  [LEAN Mapping](https://userpages.cs.umbc.edu/olano/papers/lean/), adapting the
  distribution to the engine BRDF. LEAN's original tangent-space/shading model
  is not a drop-in proof for object-space GGX.
- Split disconnected components explicitly and define sheet adjacency at
  nonmanifold junctions. A [tufted Laplacian](https://www.cs.cmu.edu/~kmcrane/Projects/NonmanifoldLaplace/index.html)
  helps the scalar operator, but does not establish robust anisotropic operators,
  kernel transport, or physically intended cross-sheet diffusion. Robustness
  cannot be inferred from the availability of a scalar solver.
- Existing `Geometry.HalfedgeMesh.VectorHeatMethod` offers a reuse lead for
  directional transport. [Vector heat](https://www.cs.cmu.edu/~kmcrane/Projects/VectorHeatMethod/index.html)
  does not remove cut loci or directional singularities.
- Rest-domain coefficients can follow deformation through face/barycentric
  attachment. Recomputing diffusion on the stretched surface is a different
  behavior. Remeshing needs correspondence/transfer and validation; a full refit
  may be required but is not universally necessary.
- Keep authored UV interpretation and generate an atlas at export when the
  destination requires one. Replace current atlas inspection with an actual
  kernel/coverage/error inspection path before removing a user workflow.

## Engine ownership and adoption gates

Geometry owns surface operators, coordinates and numerical records, depending
only on core. A paper-specific method owns the fit/compiler contract. Assets own
serialized CPU payloads; graphics/assets owns residency; graphics consumes
immutable compiled data without live ECS access; runtime composes source binding,
jobs, publication and invalidation. App uses runtime. Reuse the existing material
and validated configuration paths; do not add a second asset/texture manager.

The first experiment is a deterministic CPU compiler identity and candidate-list
study on one fitted asset plus adversarial tiny fixtures. Include sharp
subtriangle detail, broad overlapping kernels, sparse coverage, near distance
ties, triangle boundaries, degenerate triangles and signed residual colors.
Compare to a pinned reference, preserving outer/inner ranks and every clamp.
Proposed normalized-color CPU tolerance is 1e-6; validate its suitability before
adoption, never loosen it merely to get a passing result. Record ranking changes
separately. This is a proposed test, not a passed test.

If that passes, measure compiled storage/list tails, then filtering, then a Vulkan
evaluator against the CPU oracle. Only afterward optimize preprocessing. Compare
to the current atlas with its real compression/mips and all UV/gutter costs,
direct attributes where appropriate, and an atlas-free sampled baseline.

Promotion requires predeclared scene/device/frame and memory budgets, a held-out
asset cohort with reported eligibility and failure rates, no silent fallback,
bounded visual/numerical error, stable minification, supported material channels,
save/load, editing, cancellation, invalidation, inspection and export. Establish
budgets from the actual product target before measuring; the present discussion
does not invent a frame-rate requirement. No default changes before CPU reference,
optimized CPU and separately demonstrated Vulkan parity/runtime coverage.

## What would make us change direction?

If exact compiled lists are too expensive, test a **named HKTex-derived variant**
with compact support, changed ranking, or a mixed exact-field/residual model.
Each changes the fitted function and needs refitting and separate evidence.
If filtering storage approaches ordinary local textures, prefer the simpler
sampled representation where it wins. HKTex as authoring plus Htex at runtime is
a legitimate atlas-free fallback, but it is not pure HKTex runtime adoption.

The most credible path is therefore compiler first, certified candidates second,
final-color filtering third. The first two have concrete algebraic mechanisms;
bounded memory and effective antialiasing are still the decisive uncertainties.
