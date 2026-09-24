# Toward HKTex-as-default: a compiled-evaluator design

I'm working from the paper and README text you supplied earlier in this session. The central move is to separate a **canonical runtime evaluator** from the **generators** that produce kernels: the paper's spectral pipeline, a local PDE, or a fitter. That makes runtime cost, exactness, and filtering properties of one small, fixed function. The generators become offline tooling that can be swapped out.

## 1. Compilation algebra (exact identities)

Fix a kernel s with source p\*, its (θ,η), t, and the bilinearly blended basis Φᵠ. Take a point p = (f, b) with Σbᵢ = 1.

**Heat term.** Because §4.2 evaluates φₖ(p) by barycentric interpolation, the kernel is affine in b:

h_t(p) = Σᵢ bᵢ · aₛ(vᵢ), where aₛ(v) = Σₖ e^{−tλᵠₖ} Φᵠ[v,k] φᵠₖ(p\*) m\*

- aₛ(v) depends only on the kernel and the vertex, not the face. After Eq. 4 normalization, ĥₛ(v) = aₛ(v) / h_t(p\*,p\*) is a **per-vertex scalar field per kernel**.
- The a=30 power is pointwise, so it survives compilation.

**Biharmonic term.** With zᵢ = z(vᵢ) and the simplex identity:

‖Σbᵢzᵢ − z\*‖² = Σᵢ bᵢ ‖zᵢ − z\*‖² − Σᵢ<ⱼ bᵢbⱼ ‖zᵢ − zⱼ‖²

This gives d²ₛ(p) = Σᵢ bᵢ Dₛ(vᵢ) − Σᵢ<ⱼ bᵢbⱼ Eᵢⱼ.

- Dₛ(v) is per kernel and per vertex.
- Eᵢⱼ is a **per-edge, kernel-independent** squared biharmonic length.
- This is your "6 coefficients" point, but the factorization is stronger than that: only 3 of the 6 are kernel-specific, and those 3 are shared across faces through vertices.

**Consequence.** For each kernel, the complete runtime data is a sparse vertex field pair (ĥₛ, Dₛ) over its support, plus its parameters (τ, ς, c). One global per-edge array E is shared by all kernels. The per-fragment work per candidate kernel is:

- an affine term (3 values),
- a quadratic term (3 values plus 3 shared edge values),
- one exp, one sigmoid, and blend arithmetic.

Compare this to the paper's evaluator: 50 neighbours × 4 grid nodes × 256 modes, plus a 64-D KNN. I'm stating the operation structure only, not a speedup; it has not been measured.

**Two observations the paper doesn't make:**
- Inside a face, x(b) = ĥ(b)·exp(−d²(b)/2σ²). Its level sets satisfy ln ĥ(b) − d²(b)/2σ² = const.
- Because ĥ is affine on the source face, it cannot peak at p\* there. **Sub-triangle localization therefore comes from the biharmonic window, not the heat term.** Any generator that drops the window loses sub-face detail. This matters for Route B.

## 2. Canonical model (CHK): deliberate deviations from the paper

The paper's evaluator is not exactly compilable into static per-face lists. I propose canonicalizing the model and then fine-tuning published fits under the new semantics. Each deviation below comes with a diagnostic that quantifies how far CHK departs from the paper.

| Paper behaviour | Problem | CHK definition | Exactness diagnostic |
|---|---|---|---|
| K_s=50 biharmonic KNN per point (§4.2) | Membership varies *within* a face | All kernels with α>0 participate | Flag faces where >50 kernels could lie nearer than the farthest active kernel. These are computable from certified bounds (below) and are the only places CHK and the paper differ. |
| Top-K_c selection (Eq. 7) | Discontinuous ranking | Drop it | Tab. 4 "No Colour Formation KNN" is tied on the authors' 92-mesh subset. That is not our data; re-verify. |
| Gaussian window (Eq. 5) | Never exactly zero, so support is global. With ς=10 and τ=0.3, α>10⁻³ reaches h̃<0, i.e. everywhere. | Compact Wendland window ψ(d²/R²), which is polynomial in b | Error vs. the Gaussian is bounded by the window difference. |
| α can go negative | Spectral ringing gives h̃<0, and then ξ(h̃)<ξ(0) | Clamp α ≥ 0 | Count clamped samples. |
| Normalized blend Σαc/Σα | A lone kernel with α=10⁻⁶ shows its **full colour**. Fringes step from c_s to base instead of fading. | C = c_base + Σαc / (Σα + β), with β>0 | Dropping a kernel then costs at most ε‖c‖/β **absolutely**, so the cutoff certificate becomes non-relative. |

With a compact window, face-list membership is exact rather than heuristic:

- s ∈ L(f) iff min_f d²ₛ < R² and max_f ĥₛ > 0.
- d² is convex in b, so its minimum over a triangle is a closed-form 2-D QP (check interior, then edges, then corners). The maximum of an affine ĥ is at a corner.
- Lower bounds, needed for β-free variants, follow symmetrically: a convex function attains its max at a corner, and an affine one its min at a corner.

**Many-kernel faces.** The paper reports kernel-centre counts per face of 4.8 ± 7.1 on average and a maximum of 70 ± 123. Supports make candidate counts larger than centre counts. Two mechanisms:

- **Barycentric subdivision.** Split the face into 4^d sub-triangles with per-cell lists. The same convex bounds apply exactly on sub-triangles. The donor is Nehab & Hoppe's per-cell primitive lists for random-access vector graphics.
- **Budget as a model constraint.** Treat the per-cell budget like skinning's "max influences per vertex". Enforce it in fitting with an overlap penalty. Compilation fails loudly rather than silently truncating.

**Compiled evaluator vs. "secretly baking an atlas".** The compiled data is regenerable from canonical parameters. Its size scales with Σₛ |supp(s)|, not with texel resolution. It keeps nonlinear sub-face edges that stay sharp at any magnification. The honest test: if compiled bytes ≥ Htex/mesh-colors bytes at equal error at the maximum zoom we care about, then it *is* a worse bake. Support sizes are not reported in the paper, so this is unknown.

## 3. Route A (strongest): spectral generator offline, CHK at runtime

- **Runtime.** Per-face lists, per-(kernel, vertex) (ĥ, D) data, and per-edge E. There is no basis and no KNN on the GPU. Face-major duplication (6 floats per list entry) trades memory for coherence; the alternative is a vertex-major layout with indices. Measure both.
- **49-basis memory and precompute.** This becomes compile-time only. Group kernels by (θ,η) grid cell and stream the 4 node bases per cell, so peak memory is a few node bases instead of all 49. Gauge drift across machines no longer affects rendering, because the compiled fields are the render truth. Store a basis content hash with the asset. On mismatch, recompile, check against a tolerance, and refit if it fails.
- **Minification: per-kernel analytic prefilter.** Use the probit approximation σ(ςy) ≈ Φ(ςy/1.702). Convolving Φ(y/a) with N(0, s²) gives Φ(y/√(a²+s²)) exactly, so:
  - ς_eff = 1.702 / √((1.702/ς)² + s²ₓ), with s²ₓ ≈ k·(‖∂ₓx‖² + ‖∂ᵧx‖²).
  - Take the screen gradient **analytically** from the per-face coefficients and screen-space barycentric derivatives. This avoids quad-derivative errors at face edges and is anisotropic along each kernel's own gradient.
  - Donors: Loop & Blinn 2005; Green 2007.
- **Sub-pixel aggregation.** A per-kernel prefilter doesn't aggregate many sub-pixel kernels, and the ratio blend doesn't commute with averaging. Add a per-vertex **heat-diffused colour pyramid** u_ℓ = (M + t_ℓW)⁻¹ M u, with vertex values integrated from CHK at compile time. Blend to it by projected kernel size, as in Mip-Splatting's scale-aware filtering. The transition error is an open measurement.
- **Channels and normals.** Each kernel carries continuous channels (roughness, metallic) behind the same material-channel interface the shader already consumes.
  - **Normals:** each kernel stores a perturbation in rest-pose object space, rotated by skinning. Blend with the ratio, then renormalize. The pre-normalization length gives a Toksvig/LEAN-style variance for minification.
  - Principal-curvature frames matter only at fit and compile time. Prefer a smooth direction field (Knöppel et al. 2013) to avoid instability at umbilics.
- **Explicit exceptions.** These stay exact vertex/face attributes (or authored UV textures) and must never be fitted:
  - exact scientific scalars,
  - categorical labels,
  - authored UV materials.
  
  Enforce this with a channel flag. Sub-face categorical decals could use an argmax-of-α variant, flagged as lossy.
- **Fitting latency.**
  - Given α, CHK is linear in the colours. Use variable projection (Golub & Pereyra 1973): a sparse least-squares colour solve inside the nonlinear loop over geometry parameters.
  - Bake targets such as vertex fields or a high-res source mesh can be queried directly, with no rendering.
  - **Edits:** add a local edit layer of new kernels, with a local refit restricted by certified supports and periodic consolidation.
  - Fit time is unknown; measure it.
- **Deformation.** Rest-pose compiled fields ride along with skinning unchanged, by construction. Remeshing requires a recompile plus a refit against the old surface.

## 4. Route B: local PDE / rational generator (research track)

Replace the 49-node spectral grid with one anisotropic operator per kernel, W_{θ,η}, built exactly (with no interpolation or alignment) on a local patch Ω_s.

- **Heat term.** ĥₛ = r(tW)δ_{p\*}, where δ_{p\*} is the barycentric weights on 3 vertices.
  - Backward Euler (M + tW)⁻¹M decays exponentially, not like a Gaussian.
  - Better options are k steps of backward Euler, or best rational approximations to e^{−x} (Cody–Meinardus–Varga; Trefethen–Weideman–Schmelzer), which cost one sparse complex solve per pole on the patch.
- **Window.** Must be kept (see §1). Use local squared anisotropic geodesic distance, from the heat method (Crane et al. 2013) or an unfolded window propagation (Surazhsky et al. 2005). The same identity d² = Σbᵢ Dᵢ − Σbᵢbⱼℓ²ᵢⱼ holds **exactly** for a single planar unfolding. Anisotropic edge lengths ℓ²(H_s) make the edge term kernel-specific, which adds 3 values per (kernel, face).
- **What it fixes.**
  - No global eigensolve or 370 MB cache.
  - Nonmanifold meshes via the tufted Laplacian (Sharp & Crane 2020).
  - Multi-component meshes trivially.
  - Local recompile on edit.
- **Open issues.**
  - Truncating to the patch needs the discrete maximum principle to certify α=0 at ∂Ω. That holds for intrinsic-Delaunay cotan Laplacians (Bobenko & Springborn 2007), but **not generally for anisotropic operators** (the M-matrix property can fail).
  - ∂/∂θ and ∂/∂η need adjoint solves.
  - Published fits don't transfer, so a full refit and revalidation is required.

## 5. Route C: HKTex as authoring format, per-halfedge texels at runtime

Fit in CHK, then rasterize into Htex/Ptex/mesh-colors storage with heat-diffused mips.

- **Pros:** hardware-friendly filtering, trivially exact channel plumbing, and UV-free.
- **Cons:** this *is* baking. Resolution independence is lost and storage is texel-bound.

It is the fallback and the decisive comparator for Route A.

## 6. Decisive tests (design only)

1. **Compile identity.** Paper evaluator vs. compiled fields at random (f, b), on a CPU reference. Must match to float tolerance, because it is pure algebra.
2. **Canonicalization cost.** Fine-tune published-style fits under CHK and report the change on the paper's metrics, plus the KNN-divergence face count.
3. **List and memory tails.** Report the |L(cell)| distribution and compiled bytes vs. Route C at equal max-zoom error. **A fails if its bytes are ≥ C's.**
4. **Minification.** Zoom-out sequences vs. a supersampled reference, measuring error and temporal flicker, with Route C (mipped) as the baseline.
5. **Route B shape agreement.** Local-PDE kernels vs. spectral kernels; count maximum-principle violations on anisotropic patches.
6. **Exception guardrail.** Exact-flagged channels must never reach the fitter.

## 7. Unresolved mathematical issues

- Kernel-hierarchy merging: moment-matching reduction for anisotropic geodesic kernels (cf. Runnalls 2007) is unsolved on curved surfaces.
- An error bound for replacing the ratio blend with an averaged ratio under minification.
- Discrete maximum principle for anisotropic cotan operators (Route B).
- Whether overlap budgets hurt fit quality at the paper's operating points.

## 8. Citations to verify

**Confident:**
- Yuksel, Keyser & House, Mesh Colors, 2010
- Burley & Lacewell, Ptex, 2008
- Nehab & Hoppe, Random-Access Rendering of General Vector Graphics, 2008
- Loop & Blinn, 2005
- Green, 2007 (Valve, distance-field alpha testing)
- Yu et al., Mip-Splatting, 2024
- Olano & Baker, LEAN Mapping, 2010
- Toksvig, 2005
- Golub & Pereyra, 1973
- Crane, Weischedel & Wardetzky, Geodesics in Heat, 2013
- Sharp & Crane, A Laplacian for Nonmanifold Triangle Meshes, 2020
- Bobenko & Springborn, 2007
- Surazhsky et al., 2005
- Knöppel, Crane, Pinkall & Schröder, Globally Optimal Direction Fields, 2013
- Wendland, 1995

**Verify:**
- Barbier & Dupuy, Htex, 2022 (venue)
- Trefethen, Weideman & Schmelzer, BIT 2006
- Cody, Meinardus & Varga, 1969
- Runnalls, 2007
- Patané, CGF 2017, on efficient spectral kernels
- Sun et al., Diffusion Curve Textures, 2012
