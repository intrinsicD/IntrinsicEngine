# Heat kernels vs. UV atlas

Your view is mostly right. Two parts of it are too generous to heat kernels, and one part misses where they actually help.

## 1. "A complete basis can represent arbitrary discrete fields" is weaker than it sounds

The basis is complete only over the discretization it is defined on. On an n-vertex mesh, the heat-kernel or eigenbasis spans ℝⁿ, which is exactly the space of per-vertex attributes. It adds no resolution. Any per-fragment evaluation ends up barycentrically interpolating per-vertex quantities, whether those are eigenfunction values, kernel values, or coefficients. So a heat-kernel texture is at best a compressed vertex attribute.

Sub-triangle detail needs a finer sample domain: subdivision, per-face grids, or texels. At that point you have rebuilt an addressing scheme, and the heat kernel sits on top of it. The central point is that the heat kernel is a **representation/operator**, not a **sample store**. Addressing is the hard part of what a UV atlas does, and heat kernels do not replace it.

## 2. Sharp details are worse than "costly"

- **Truncated eigenbases** ring (Gibbs) at edges.
- **Landmark kernels at a fixed t** avoid ringing because they are positive, but they blur.
- **Small t** sharpens, but the kernel's support must be resolved by the mesh. In the limit you have dense landmarks with Gaussian reconstruction, which is just a splat texture with extra steps.

There is no regime where heat kernels store a crisp decal more cheaply than texels.

## 3. HKS is a red herring for addressing

The HKS diagonal is a descriptor. It is non-injective generically, because of level sets, not only under symmetries. Landmark-kernel embeddings into ℝᵏ can be injective. But then you are hashing an intrinsic k-dimensional coordinate into storage, and a 3D-position hash grid or sparse voxel texture already gives UV-free storage without eigensolves.

The only thing intrinsic coordinates buy you over those is invariance under isometric deformation.

## 4. Deformation: I'd invert your framing

UV-attached data is **more** robust than heat fields under deformation. Per-vertex UVs follow connectivity through any deformation, isometric or not. A heat-derived field depends on the metric, so it changes under:

- stretching,
- topology edits (globally at large t),
- eigenbasis sign, order, and multiplicity flips,
- loss of landmark correspondence after a remesh.

Heat fields win only when you must re-derive a field on new connectivity, where they are discretization-convergent. That is a transfer and remeshing argument, not a runtime texturing argument.

## 5. Normal maps and frames

The vector heat method gives you smooth parallel-transported frames, but Poincaré–Hopf forces singularities whose indices sum to χ. UV charts move those singularities into seams, and heat methods move them into points. Neither escapes topology. Object-space normal maps avoid tangent frames entirely but still need addressing.

## 6. Does vertex-property baking need UVs?

It does not, if the target resolution is at or below the render mesh's vertex density. Then you just keep vertex attributes, optionally heat-smoothed. Baking into textures earns its cost only in these cases:

- detail finer than the mesh (high-to-low poly),
- hardware filtering, mips, or anisotropic filtering,
- interop (glTF expects UVs).

It is worth checking which of these IntrinsicEngine's runtime albedo and normal baking actually needs. If the source is a vertex property on the same mesh, the atlas may be pure overhead for that path.

## 7. Where heat kernels genuinely help

- **Prefiltering.** Diffusion time is the natural intrinsic scale-space, a seam-free analogue of a mip chain. It is not an anisotropic-filtering substitute, since screen-footprint mapping needs a local parametrization and heat kernels are isotropic.
- **Seam-aware dilation and inpainting** across UvAtlas chart boundaries, using seam xrefs to diffuse across cuts instead of pushing color into gutters.
- **Spectral compression** of smooth vertex fields.
- **Correspondence and transfer** of authored textures across remeshes.

## Htex disambiguation

Htex (per-halfedge textures, Ptex-like) actually solves UV-free *addressing*. If the goal is "no atlas," that is the right investment, with heat diffusion as its filtering and cross-patch operator. `Geometry.HtexPatch` having metadata and simple layouts does not establish sampling, filtering across patch borders, or mip support. Those remain the open risks.

## Recommendation

- Keep UvAtlas canonical for authored UVs, interop, and sharp signals.
- Add heat diffusion as a field operator: seam-aware dilation, a mip/prefilter prototype, and vertex-attribute smoothing and compression.
- Pursue Htex, not heat kernels, if you want an atlas-free path.

## A small falsifiable experiment

**Setup**
- **Meshes:** 3 meshes: genus 0, genus ≥ 1, and a symmetric one.
- **Signals:** (a) smooth, such as mean curvature; (b) sharp, a crisp checker or decal boundary that crosses atlas seams.
- **Reference:** evaluate each signal on a dense Loop-subdivided copy.

**Methods at equal storage budget B floats**, counting basis storage honestly:
1. XAtlas texture with B texels.
2. k eigen-coefficients plus per-vertex basis storage.
3. Landmark heat kernels.

**Metrics**
- L2 and L∞ error.
- Error restricted to a band around seams and around the sharp edge.
- Re-evaluation after an isometric bend and after a remesh.
- Per-fragment op counts; op counts rather than timings, to avoid platform noise.

**Predictions**
- Heat methods win on (a), especially near seams.
- They lose on (b) unless k approaches n.
- UV wins under non-isometric deformation.

**Falsifier:** if any heat method matches the atlas's L∞ on the sharp edge band at equal B, my claim #2 is wrong.
