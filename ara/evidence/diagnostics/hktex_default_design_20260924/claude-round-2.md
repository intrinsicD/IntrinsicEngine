# Revised HKTex plan: frozen-kernel compilation against the reference code

I accept all six corrections. Two of them go further than you stated (2 and 6), and one of my earlier claims was simply wrong (3). I have not read the pinned commit myself. Every reference-code semantic below is quoted from your cross-examination and must be confirmed at Gate 0.

## 1. Exact algebra and the parity contract

Take a point p = (f, b) with Σbᵢ = 1. For each kernel s, the source p\*ₛ, the angle and anisotropy (θₛ, ηₛ), and the blended basis Φᵠₛ are all frozen.

**Heat term (affine per face).**
- hₛ(b) = Σᵢ bᵢ aₛ(vᵢ), where aₛ(v) = Σₖ e^{−tλᵠₛₖ} Φᵠₛ[v,k] φᵠₛₖ(p\*ₛ) m\*.
- Divide by hₛ(p\*, p\*), then apply `power_diffused_diracs` pointwise, exactly where the code applies it.
- The power stays exact, but it is no longer affine. Its behaviour on negative inputs (NaN, clamp, or otherwise) must match the code.

**Distance term (quadratic per face).**
- qₛ(b) = ‖z(b) − zₛ‖² = Σᵢ bᵢ Dₛ(vᵢ) − Σᵢ<ⱼ bᵢbⱼ Eᵢⱼ.
- Eᵢⱼ is shared by all kernels, so no 64-D embedding is needed at runtime.

**Filter and composite.**
- h̃ = h̄^a · exp(−q/2σ²), with the Gaussian unnormalized as in the config.
- α is the rescaled soft-step, with α(0) = 0.
- N₅₀(p) = the 50 smallest qₛ; T₃₀ = the top 30 by α within N₅₀.
- C = clamp(c_base + Σ_{T₃₀} αc / max(Σα, 1), 0, 1).

**Contract.**
- Heat and distance terms: algebraic parity. Differences are floating-point only.
- Rankings: parity outside an ambiguity band of width δ around bisectors. δ comes from the reference's own arithmetic. GPU FAISS in float32 likely uses the ‖x‖² + ‖y‖² − 2xy expansion, which cancels badly near ties.
- Ties: parity only if the reference tie rule is deterministic. Clone-densified kernels can remain exact duplicates, so they tie on sets of nonzero measure.
- Index type: if the configured FAISS index is approximate, "exact oracle" must be redefined as exact flat KNN, and the reference becomes a second, non-identical comparator.

I retract my tiny-denominator critique. The code uses max(Σα, 1), which is additive below 1 and continuous. I also retract the Wendland route and the claim that α reaches negative h̃: α(0) = 0, so support at level ε is {h̃ ≳ ε/α′(0)}, which is broad but finite. **The reference's effective support is set by KNN rank, not by α.** That is why culling must preserve rankings.

## 2. The culling proof: confirmed, with two additions

For a cell T:
- lₛ = min_T qₛ, a convex QP;
- uₛ = max over T's corners, since qₛ is convex;
- U₅₀ = the 50th smallest uₛ.

**Claim:** if lₛ > U₅₀, then s ∉ N₅₀(p) for every p ∈ T.

**Proof:** at any p, at least 50 kernels r satisfy qᵣ(p) ≤ uᵣ ≤ U₅₀ < lₛ ≤ qₛ(p). So 50 kernels are strictly nearer than s. ∎

Keeping everything with lₛ ≤ U₅₀ preserves ties for query-time resolution. The rounding margin must use the reference's error model, not ours.

**Addition 1: cancellation.** The pairwise difference qₛ − qᵣ = −2z(b)·(zₛ − zᵣ) + const is affine because z(b) is affine per face. Two consequences:
- Exact order-k cells are line arrangements in the barycentric plane. Order-k Voronoi complexity grows as O(k(n−k)) (Lee 1982, verify), so this is feasible but expensive.
- Cancellation holds only within one face. At face boundaries the rankings are piecewise and continuous, but the cells are not shared.

**Addition 2: rankings impose a floor.** Preserving reference semantics forces at least 50 distance evaluations and 50 α evaluations per fragment, plus a 30-of-50 selection. That is not a tail cost; it is a floor. Subdivision makes lₛ, uₛ converge to qₛ(p), so the candidate set shrinks toward 50 plus ties. It never goes below 50.

## 3. Data worst cases

- **Sparse-kernel regions are the worst case, not dense ones.** The paper shows kernels thinning out on uniform colour. There, the 50 nearest kernels are far away, and each is retained across the whole region. The cost is at least 50 × (ĥ, D) per vertex of that region, which is possibly more than vertex colours or texels. This is the memory killer, and it is created by the KNN semantics.
- **Large faces** need deeper barycentric subdivision before the lₛ–uₛ gap closes.
- **Clone duplicates** produce ties.
- **Negative ĥ** from spectral ringing makes the behaviour of pow and α depend on the code.

## 4. Filtering: preferred path and explicit unknowns

- **Oracle.** CPU adaptive supersampling of the final clamped C under the pixel filter. Visibility and cross-triangle footprints are handled by construction.
- **Geometric error term.** Because C ∈ [0,1], replacing the true footprint filter w_F with a stored filter w_G costs at most ‖w_F − w_G‖₁. This is signal-independent and computable.
- **Approximation error term.** The remaining error is |∫w_G C − P_ℓ|, where P_ℓ is the stored level-ℓ approximant, evaluated at the pixel centre. It is certified at compile time by quadrature, which is rigorous only with interval arithmetic.
- **Why this addresses your aliasing point.** A bound on L∞ error of the signal says nothing about point-sampling it. The quantity to bound is sample-vs-footprint-integral error.
- **Runtime design.** Evaluate the compiled kernels exactly when the footprint is at or below the switch scale. Above that scale, use per-face moment pyramids that are **truncated at the top**: no level finer than the switch scale is stored. This is an honest local bake, but its resolution is set by the kernel scale at the switch, not by the maximum zoom. Adding hardware-style multi-tap probes reduces the ‖w_F − w_G‖₁ term under anisotropy.
- **Unknowns.**
  - Where to put the switch scale, and whether the stored pyramid bytes stay below per-face texels.
  - Footprints spanning many small faces, which becomes a geometric-LOD problem.
  - Silhouettes, which texture prefiltering does not solve for UV textures either (MSAA/TAA territory).
  - The approximation error of the per-kernel ς_eff widening, which does not commute with the nonlinear composite.

## 5. Six blockers and adapted donors

1. **Candidate tails and sparse-region memory.** Adapt per-cell lists with specialized encodings from Nehab & Hoppe 2008, and branch-and-bound KNN bounds. Order-k arrangements are the exact limit.
2. **Oracle numerics.** FAISS index type, precision, tie rule, and clones. No donor applies: specify a double-precision exact oracle plus a reference-compatibility mode.
3. **Footprint integration.**
   - EWA from Zwicker 2001: adapt only at the α level, since it is exact only for linear blends.
   - Scale conditioning from mip-NeRF 2021.
   - Frequency capping from Mip-Splatting 2024: cap ς by the intended sampling rate. **This changes the model and needs a refit.**
   - Truncated per-face pyramids after Ptex.
4. **Channels.** Exact vertex interpolation as the baseline, plus an HK residual for finer signal. Label this explicitly as a hybrid. Labels are never fitted. Normal-moment filtering (LEAN, Toksvig) against GGX or object-space normals is research.
5. **Compile pipeline.** The alignment chain is sequential ("last set aligned"). Streaming 4 nodes reduces RAM only if the aligned bases are persisted, and persisting them *is* the cache. Huang et al. 2020 is your citation; I cannot vouch for it, and it does not give parity.
6. **Edits, fitting latency, and mesh robustness.**
   - Refit gradients need the basis. Variable projection is a candidate with no guarantee.
   - Eigen failures and non-manifold input: the tufted Laplacian (Sharp & Crane 2020) changes the basis, so it needs a refit.
   - Rest-pose compiled data survives skinning. Any geometric edit invalidates the basis and the fit.

## 6. Edit locality under global heat tails

"Regenerable" was overstated. The runtime data regenerates from **parameters plus the cached aligned basis**, not from parameters alone.

**Kernel edits** (moving p\*, changing θ/η/τ/ς/c, adding or removing a kernel) are local, but their reach is the **ranking footprint**:

old ∪ new cells where lₛ ≤ U₅₀

This is at least as large as the visual footprint. It is large in sparse regions, where a new kernel can evict others from N₅₀ far from where it is visible. The fields ĥₛ and Dₛ are globally nonzero, but they are stored and recomputed only over retained cells. Recompute cost scales with affected vertices × 4 grid nodes × 256 modes; I have not measured it.

**Mesh edits** are global: new basis, new fields, and a refit.

## 7. What must pass before HKTex becomes the default

- **G0.** Confirm from the pinned code:
  - where the power is applied,
  - m\*,
  - the FAISS index type,
  - the tie rule,
  - handling of negative α and ĥ,
  - the clamps.
- **G1.** On reference-fitted assets, the compiled CPU evaluator matches the reference within tolerance. All ranking disagreements fall inside the computed δ-band.
- **G2.**
  - Zero culling misses against exhaustive ranking on dense samples. This supplements the proof; it doesn't replace it.
  - Report the candidate tail distribution.
  - Compare compiled bytes against vertex attributes and per-face texels at equal error.
- **G3.** Filtering error vs. the oracle across a zoom sweep, reporting both bound terms, plus temporal stability.
- **G4.** Pass rate on the engine's assets through the reference filters. Exceptions for exact, categorical, and authored channels are routed away from fitting.
- **G5.** Fitting and edit latency within a budget **you** set, with the ranking footprint measured.
- **G6.** Parity under skinning.

If any gate fails, HKTex stays opt-in, and the truncated-pyramid Htex route becomes the generated-texture default.
