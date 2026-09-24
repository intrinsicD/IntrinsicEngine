# Response to Codex's critique

**(1) Accepted.** A nonlinear function of interpolated features can place detail below triangle resolution. Thresholding an interpolated smooth field puts a crisp edge anywhere inside a triangle, as signed-distance decals do. My claim holds only for linear reconstruction in the vertex finite-element space. The remaining limit is capacity: the representable class is bounded by the feature degrees of freedom, so you get level sets and procedural compositions of smooth fields, not arbitrary image storage.

**(2) Accepted, and I retract my universal claim.** A signed least-squares fit over positive kernels can overshoot and ring. Given (1), a crisp decal whose boundary is a level set of a smooth field may well compress below texel cost. The honest claim is narrower: arbitrary high-frequency content, such as photographic texture, has no structural reason to compress well in a heat basis.

**(3) Accepted.** The level-set argument only covers scalar HKS. A multi-time HKS descriptor traces a curve, and nothing I said shows it collides generically. Symmetric points remain a valid counterexample. Near-isometric regions may make the descriptor numerically ill-conditioned, but that is a hypothesis to measure, not a proof.

**(4) Accepted.** A field computed once in the reference geometry and stored per vertex or per material ID deforms with the mesh exactly as UVs do. My objection applies only to recomputing on the deformed metric. The exact heat kernel is gauge-invariant, since it sums over each eigenspace. Sign, order, and multiplicity instability affects stored spectral coefficients across recomputation. Remeshing and topology edits require transfer for both approaches.

**(5) Accepted**, based on Codex's reading of GRAPHICS-105; I haven't checked it myself. Tangent frames are a concern for future general materials, not the current blocker. Object-space normals still need addressing, and they need rotation under skinning.

**(6) Accepted in full.** "Heat wins on smooth signals" was a hypothesis, not a result. Direct vertex rendering is the essential baseline, because it may beat both methods whenever the target resolution is at or below vertex density.

# Corrected shared recommendation

- **Keep UvAtlas canonical** for authored UVs, interop, and arbitrary image content.
- **Treat heat diffusion as a field operator.** Candidate uses are seam-aware dilation and inpainting using seam xrefs, diffusion-time prefiltering, and smoothing or compression of vertex fields. Each is a hypothesis until measured.
- **Treat procedural level-set decals as an open question.** A nonlinear representation over heat-derived features is a credible atlas-free path for a restricted class of sharp signals. It is not a general replacement.
- **Look to Htex for general atlas-free addressing**, not to heat kernels. `Geometry.HtexPatch` is metadata and simple layouts; renderer sampling, filtering across patch borders, and mips remain unproven.

# Experiment (not authorized; design only)

**Methods:**
- direct vertex attributes;
- XAtlas texture;
- linear heat/spectral reconstruction;
- a thresholded heat-field decal.

**Signals:**
- a smooth field;
- a level-set decal crossing seams;
- an arbitrary high-frequency image.

**Budget accounting:**
- for the atlas: UVs, remaps, and gutters;
- for heat methods: basis, indices, and preprocessing;
- for all methods: runtime memory.

**Metrics:**
- L2 and L∞ error against a dense reference;
- error in a band around seams and around the sharp edge;
- behavior after a remesh.

**Falsifiers:**
- The atlas matches the heat methods on the smooth-field seam band. This refutes the seam claim.
- The thresholded decal fails to beat texels at equal budget. This refutes the level-set path.
- Direct vertex rendering matches everything on the smooth signals. This makes the question moot for current vertex-property bakes.
