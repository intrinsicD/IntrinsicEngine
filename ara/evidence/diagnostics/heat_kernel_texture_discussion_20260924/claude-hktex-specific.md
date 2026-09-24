# HKTex reassessment

**Correction first.** Our two earlier rounds critiqued a generic "heat-kernel field" method, not HKTex. Its core mechanism escapes most of that critique. Kernels have learned positions anywhere on faces (§4.2–4.3) and adaptive density (§4.3), and they are passed through a sigmoid soft-step (Eq. 6) inside a local partition of unity (Eq. 7). So the "linear in vertex DOFs" ceiling doesn't apply. The paper even fits an image on two triangles with K=3, although that needed kernel amplification with a=30 (§5.4, Fig. 4; S.1 eigenmodes note).

However, the verdict for IntrinsicEngine mostly survives, for different reasons than we gave.

## 1. What is actually demonstrated
- The target is **albedo only** (§1, §6 Limitations; S.1 rendering notes).
- The "PBR" claim means HKTex was plugged in as the albedo slot of Mitsuba's Principled BSDF.
- Roughness, metallic, and normal maps are called "highly feasible" but are not shown.
- Multi-view fitting uses synthetic renders only (§5.5).

**Challenge to you:** "arbitrary material channels" is further from the paper than you imply.
- Eq. 7 blends values with a partition of unity. That is meaningless for categorical data such as labels or IDs.
- Blended normals need renormalization, which the paper never discusses.

## 2. Baked mesh properties
Our vertex-derived bakes, such as curvature and scalar fields, are already exactly representable at the vertices. HKTex would be a lossy, optimized refit of data we already hold exactly. Tab. 1's GT VTex row (vertex colours) is the relevant baseline here, and the paper itself treats it as a baseline. HKTex has no case for these bakes unless the source signal is finer than the mesh.

## 3. Disk size vs. caches vs. VRAM
- Tab. 1 reports HKTex at ~97 KB. That figure is zipped, unquantized learned parameters (§5.3).
- It **excludes** the 49-node ALBO cache with 256 modes: about 370 MB on average per mesh, and 97.8 ± 286.9 s of precompute that is also excluded from timings (S.1 precomputation note).
- The claim that the cache "need not be stored" assumes the eigensolve is deterministic. But the learned parameters are tied to a *specific aligned gauge*: the Hungarian, Procrustes, and sign-fix steps of §4.1, chained through a reference. A different eigensolver, precision, or platform could shift that gauge. This is the practical form of the gauge point we raised in round 2.
- 370 MB is not resident VRAM. The per-fragment access pattern is 4 bilinear grid corners times K_s=50 neighbours (§4.1–4.2, Tab. 3). That is random access across the cache, so the resident set is unknown, not small.

## 4. Optimization vs. inference, and the real-time Vulkan path
- **Fitting** means 5k steps, a 10M-point sample pool, and Optuna-tuned hyperparameters (S.1). The excerpt does not report fitting time.
- **Inference** needs a FAISS KNN in a 64-dimensional biharmonic embedding (§4.2), then 256-mode eigenvector dot products per neighbour. On static meshes this could become precomputed per-face candidate lists, but that is our conjecture.
- The paper's render times are Mitsuba/PyTorch numbers in their harness (Tab. 1: 784.7 ms vs 33.7 ms for LR UV). They say nothing about a Vulkan fragment shader.
- The storage comparison ignores GPU block compression (BCn/ASTC), which is what a Vulkan atlas actually stores in memory.

## 5. Sub-triangle detail
This is my derivation, not the paper's. Within a face, h (Eq. 3) is linear in barycentrics, and d_BH² (Eq. 5) is quadratic. So each kernel's footprint inside a face is a sigmoid of a linear term times a Gaussian of a quadratic. Its boundaries therefore come from a restricted family of curves, and that likely explains why a=30 was needed on the two-triangle rectangle. Detail beyond the mesh comes mostly from kernel density, not from diffusion.

## 6. "Filtering"
The "kernel filtering" in §4 is sigmoid shaping, not sampling filtering. The paper defines no minification prefilter, no mips, and no derivative-driven LOD. Sharp sigmoids (ς up to 200) will alias when minified in rasterization. An analytic fix is plausible, such as widening ς by the ddx/ddy footprint, but it is unproven.

## 7. Static vs. edited meshes
- Kernels stored as (face, barycentric) with rest-pose bases would deform with skinning.
- Editing means painting a baked dense point cloud and then re-optimizing (S.1 editability note).
- Remeshing requires a full refit.
- The anisotropy frames use principal curvature directions (§3), which are unstable near umbilics.
- The dataset filter (§5.1) excludes multi-component meshes, non-manifold meshes, meshes with complex internal structure, and meshes whose eigendecomposition fails. Many engine assets fail that filter.

## Verdict
HKTex should not replace UvAtlas for either bakes or real-time materials. It is a credible **offline compression and acquisition format for albedo on clean static manifolds**, and it is worth tracking. Our earlier rejection was too strong on representational power. It was right on deployment, but for the wrong reasons.

## Smallest evaluation before any adoption (design only)
1. **Filter gate.** Run §5.1's criteria over the engine's test assets and report the pass rate. If few pass, stop.
2. **Gauge reproducibility.** Recompute the ALBO cache twice, on different machines or solver settings, and check whether a fitted HKTex renders identically. If it doesn't, the cache must ship with the texture, and the storage claim collapses.
3. **Offline albedo comparison** using the authors' code on a handful of passing assets, against three baselines at equal bytes:
   - a BC-compressed XAtlas texture;
   - vertex colours;
   - subdivided vertex colours.
   
   Evaluate at magnified and minified scales.
4. **Only if gates 1–3 pass:** count the per-fragment operations and resident memory for a static per-face-candidate design before writing any Vulkan code.
