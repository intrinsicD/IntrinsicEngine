**1. First integration (decided).** The pipeline is: existing `SegmentFaceFeatures` → explicit cleanup (absorb regions below a declared area fraction into the neighbour sharing the longest boundary; ties broken by ID) → freeze → `UvAtlasInput::FaceRegions` → xatlas `faceMaterialData` → independent audit. The editor requests XAtlas explicitly; import is unchanged. No new growth algorithm is needed first.
- **Satisfies** (audited, not trusted): coverage, correspondence, validity, and separation of frozen labels.
- **Cannot:**
  - meet distortion targets by construction (the audit only gates them);
  - place soft, property-guided seams;
  - provide area or importance modes;
  - enforce non-separating hard seams exactly.
- Path B and new solvers use the same contract. They are promoted only by passing corpus gates against constrained xatlas.

**2. Contract.** Strict mode turns every degraded status into failure.
1. **Admissibility:** finite positions, valid triangles, label array of size F. Otherwise, typed failure.
2. **Hard invariants, after bounded repair** (re-solve, split, single-triangle charts). Violation means typed failure with nothing published.
   - Every source corner has finite UVs and source xrefs.
   - Each face is in exactly one chart, and charts ⊆ frozen regions.
   - Every non-degenerate face is positively oriented in the published floats.
   - No overlap within a chart, and no texel shared between charts at the declared resolution and padding.
   - UVs lie in [0,1] without clamping.
3. **Exclusions:** degenerate or float-unrepresentable faces are listed and excluded from orientation checks and metrics. Status is `Partial`, never `Success`.
4. **Requested targets:** a missed distortion target gives `TargetMissed`; a missed pixel budget gives `UnderResolved`. `Success` requires all targets met and no exclusions.
5. **Preferences** (few regions or charts, seam length, guidance): reported only.

Cut reasons (Semantic, Hard, Topology, Distortion, Guided) stay distinct. Requested/actual/`UsedFallback` is reported separately from status.

**3. Modes.** Pixel metric `J_px = diag(W,H)·J`; `σ̂ᵢ = σᵢ/ρ` with one atlas-wide ρ (declared or achieved, and stated). Define `d = σ̂₁σ̂₂`, `κ = σ̂₁/σ̂₂`, target density `d*`. Gates use a declared statistic (default: max over non-excluded faces).
- **none:** Tutte with positive weights on a circle boundary. It is injective in exact arithmetic (Tutte; Floater 2003); floats are audited. Validity gates only.
- **angle:** LSCM or BFF; gate κ.
- **both:** symmetric Dirichlet, which is isometric, not "area"; gate κ and d.
- **area** (area-prioritized, AMIPS form, Fu–Liu–Guo 2015):
  `E = Σ A_f·exp(s·[α·½(κ+1/κ) + (1−α)·½(d/d* + d*/d)])`, with small α.
  - The area term is a barrier as det→0⁺. It needs its own gradient but reuses the flip-limited line search.
  - Global overlap is caught only by audit plus split.
  - Safety bound: gate κ ≤ κ_max alongside `exp|log(d/d*)| ≤ τ_A`.

Pure determinant matching is ill-posed: continuum area-preserving maps exist (Moser) but are non-unique, with unbounded shear. Single triangles meet any τ. So "infeasible" means *within the declared chart/seam/pixel budget*, reported per chart as `TargetMissed`.

**4. Density.** Default is uniform (d* ≡ 1) with one shared atlas.
- **Importance:** `d* = w/w̄`, w ≥ w_min > 0, ∫d* dA = A. Area mode realizes it pointwise. Angle mode can realize it only per chart, because the interior conformal factor is fixed by curvature up to a harmonic term.
- **Bakes conflict when:**
  - importance fields differ spatially;
  - resolutions differ (padding and pixel budget scale with W,H);
  - label and filtered bakes need different footprints.
- **Resolution:** one combined field under an explicit rule (e.g. max of normalized demands), validation at the lowest declared resolution, and typed rejection below atlas support.

**5. Fidelity** uses a CPU oracle with the same raster rule. Coverage is measured from the raster's face-ID mask, never predicted.
- **Affine vertex fields:** exact to encoding tolerance. Non-affine fields are compared to the P1 interpolant, separately from mesh error.
- **Face constants and labels:** bitwise on covered texels. Labels are never interpolated, including in gutters and mips.
- **Invalid samples:** masked, counted, and excluded from gutter fill and mip generation.
- **Seams:** sample both sides with the declared runtime filter at every generated level. Tolerance = encoding + |∇f|·(footprint mapped to the surface). Labels must match exactly at interior-offset points.
- **Gutters:** under the declared filter and mip policy, the level-L texels read by each chart's covered samples must be disjoint across charts and fully defined. Unsafe levels are not generated (`MaxSafeMip`).
- **Pixel budget:** a separate, resolution-dependent check (`UnderResolved`), never geometric invalidity. Skipped degenerate triangles make the bake `Partial`.

**6. UI and order.** One `SceneViewportRect` is the sole authority for viewport/scissor (or a rect-sized scene target), projection aspect, picking rays, gizmo rect and input routing. The existing `Graphics.UvView` target fills the complementary rect. No second camera, second ECS mesh, docking or topology mutation. A full-window rect must reproduce today's behaviour.
- **Hover:** shows the exact source value and the texel value separately. The texel value appears only once readback exists.
- **Auto-open:** only when a user-initiated Generate/Bake job ends without failure, its input fingerprint still matches, and the entity is still selected. Never on import, load, undo/redo or background rebakes.
- **Tab binding:** each tab binds (atlas content hash, property content hash, UV binding, encoding, filter policy).
  - Undo restores validity automatically.
  - Property edits mark that property's bakes stale.
  - Position edits mark only metrics stale.
  - Topology edits invalidate the atlas and all bakes.
  - Recompute creates a new revision and marks bakes stale, never deleting them.
  - On mismatch: Stale badge, overlay off, and `RenderExtraction` refuses the bake.
  - The baker uses the selected binding, not a literal `h:texcoord`.

**Slice 0 (≤1 day):**
- `ValidateAtlas` v0: the O(F) checks of contract items 2–3 (xrefs, membership, finiteness, bounds, orientation, exclusions), plus label-crossing detection and status mapping.
- Truthful requested/actual/fallback reporting, with the test that fails today (confirming or refuting my source reading).

**Then, in order:**
1. Constrained xatlas.
2. Audit v1: overlap, texel occupancy, pixel metrics, gates.
3. Fingerprints and baker binding.
4. Scene rect.
5. Corner UVs, explicit texture, raw-float colormap.
6. Bake fidelity.
7. Tabs and auto-open.
8. Path B (angle).
9. Both and area modes.
10. Scaling and the default decision.

Area mode, raw-float display and mip-safe gutters stay in the final scope. They are hidden until implemented (UI-036), not dropped.

**7. Revisions to round 1.**
- **Withdrawn:**
  - the pinned-window camera shift;
  - source hover presented as a bake readout;
  - the oversized first slice;
  - per-chart *metric* normalization (per-chart rescale remains a packing step);
  - "reported, not gated";
  - splitting-only "area";
  - soft regions (frozen labels are hard; softness lives in ψ and pre-freeze cleanup);
  - predicted coverage;
  - the 2^L padding rule;
  - per-piece material IDs.
- **Complexity:** only the growth queue is O(F log F). Factorization is ≈O(n^1.5) per planar-like chart with nested dissection. Unbalanced bisection reaches depth O(F) unless balance is enforced. The grid audit is O(B²) in the worst case. Measure these; don't claim them.
- **Determinism** (tested over 3 runs):
  - Bitwise identical per binary, platform and thread count.
  - Thread-count invariance holds only if per-chart solves and packing are internally sequential; unverified for xatlas.
  - Across platforms, validity only.

**8. No winner yet:** constrained xatlas versus path B on chart count at fixed τ; BFF/LSCM order; defaults for s, α, κ_max and w_min; packer; cleanup threshold.