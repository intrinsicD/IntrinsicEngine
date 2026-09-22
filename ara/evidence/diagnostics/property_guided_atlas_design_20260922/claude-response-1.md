# Design review: property-guided segmentation → low-distortion UV atlas → property baking

I built and ran nothing for this review. Each fact is tagged by where it comes from:
- **[S]**: my own reading of the source.
- **[T]**: a test in the tree asserts it. I did not run that test.
- **[R]**: recorded evidence in a task file or ARA claim. I did not re-run it.

## 1. What exists today, and the gaps

**Implemented**
- **Atlas generation:** `Geometry.UvAtlas::ResolveUvAtlas` offers three methods: Authored, `FastStaged` (the default) and `XAtlas` [S].
  - It returns cross-references from output back to source faces and vertices, plus chart and seam records.
  - It reports the requested method, the method actually used, and any fallback [S; T in `Test.UvAtlas.cpp`].
- **Editor atlas job:** `ApplyEditorUvRegenerationCommand` in `Runtime.GeometryProcessingOperations.Uv.cpp` [S].
  - It runs as a `JobService` job with undo.
  - It rejects stale results using a snapshot of positions, halfedge topology and all known property values.
  - It keeps the mesh topology and writes `h:texcoord` (one UV per face corner) when seams exist.
- **Parameterization solvers:** LSCM, harmonic/Tutte and BFF, all requiring disk topology [S].
  - `Geometry.Parameterization.Optimize` has the building blocks for SLIM: symmetric-Dirichlet energy, ARAP/SLIM proxy systems, and a flip-preventing line search.
  - There is no outer solver loop yet; that is METHOD-021/022 in the backlog.
- **Segmentation:** `SegmentFaceFeatures` (a Gaussian mixture plus spatial smoothing over 1–3 face channels, normalized by median/MAD) [S].
  - `CurvatureSegmentationConfig::Features` can bind any numeric vertex or face property; vertex values are averaged onto faces.
  - The patch and multicut methods are diagnostic only. METHOD-040's adoption gate was refuted (C59) [R].
- **Baking runs on the GPU only, not the CPU** (`PropertyRasterGpu`) [S]. It supports vertex, face and nearest-edge sources, resolves corner UVs, fills gutters only for encoded RGBA, and writes a single mip level (`MipLevels=1`). `ServiceFailsClosedWithoutGpuComposition` [T] confirms there is no headless bake.
- **UV views:** a CPU `ImDrawList` controls|UV split (UI-036) and the GPU `Graphics.UvView` with Grid/Checker/TexelDensity/Texture backgrounds [S, R].

**Gaps found by reading the source.** Each needs a confirming test.
1. **The editor misreports its UV backend.**
   - `RunUvRegenerationCpuWorker` sets only `options.BackendName="xatlas"`. `UvAtlasOptions::Method` defaults to `FastStaged`, so the editor actually runs FastStaged and uses xatlas only if FastStaged fails.
   - Meanwhile `ValidateUvRegenerationRequest` tells the user "Only the promoted xatlas UV backend is available".
   - `EditorUvRegenerationCommandResult` has no requested/actual/fallback fields. This breaks the reporting rule in AGENTS.md §1.
2. **FastStaged over-fragments curved meshes.** `FaceFitsPlanarChart` compares each candidate face against the *seed face's* normal (15° cone) and plane (distance 1e-4·diagonal). BUG-160 records about 90,594 charts for 100k faces [R].
3. **FastStaged gives charts inconsistent texel density.**
   - Each chart's UVs come out in different units: LSCM pins two vertices at (0,0)/(1,0), so every chart is scaled by its pin distance; the Tutte fallback maps to the unit square; projection keeps metres.
   - `PackFastCharts` then applies one common scale, so relative texel density between charts is arbitrary.
   - Output UVs are silently `glm::clamp`ed to [0,1].
   - The Tutte fallback uses a square boundary (`HarmonicBoundaryPolicy::Square`). Its straight sides can collapse "ear" triangles to zero area.
4. **No check for overlaps inside a chart.** `AcceptSolverUvs` rejects only flipped triangles. The projection fallback is never rejected. The tests check chart-vs-chart overlap only with bounding boxes.
5. **The distortion metrics are not scale-normalized.** `EvaluateParameterizationDiagnostics` uses raw `uvArea/area3d` and raw `σmax`, averaged per face rather than weighted by area. On a packed atlas, the "area distortion" and "stretch" numbers mostly measure the packing scale.
6. **An atlas with seams cannot be displayed.**
   - `BuildEditorParameterizationViewModel` reads per-vertex UVs only. It explicitly refuses corner UVs (`h:texcoord`), which is what any seamed atlas publishes [T: `ViewModelNamesCornerDomainUvsItCannotDraw`].
   - The `Texture` background can only show the material's Albedo texture (`Runtime.EditorFeatureContextAdapters.cpp` ~1677).
7. **Baking is fragile and can go stale.**
   - One non-finite value, or one UV triangle with area ≤1e-10, fails the whole bake.
   - `ZeroCoverageBake` is declared, but I found no code that produces it.
   - Raw-float bakes cannot be padded.
   - `PropertyTextureBakeRecord` stores no UV fingerprint, and `RenderExtraction` (~1150) binds any Ready record by name, domain and encoding. So after a new atlas is computed, a texture baked against the old UVs may stay bound on the 3D mesh.
8. **Persistence is partial.** The scene codec saves `v:texcoord`, `h:texcoord` and `h:normal`. I found no generic face-property persistence. For bakes I found only the `useBakedTexture` flag, not the bake catalog.
9. **Mesh saliency does not exist.** The only saliency is point-cloud keypoint saliency (λ3) in `Geometry.PointCloud.Features`.
10. **An unused lever: xatlas can respect regions.** The pinned xatlas header has `MeshDecl::faceMaterialData` ("Only faces with the same material will be assigned to the same chart"). `benchmarks/runners/UvChartPackDiagnosticRunner.cpp` already uses `AddUvMesh`/`PackCharts` for packing only.

## 2. Segmentation and charting

**Options considered**
- **A. xatlas constrained by regions.** Each material ID is one connected piece of a region after cutting along hard seams.
  - Pros: a few hours of work, a strong baseline, and it makes "fallback obeys regions" true.
  - Cons: no distortion presets; its internal heuristics are opaque; the face-order cross-reference and determinism need tests.
- **B. Guided seeded growth → validate or split → validated merges.** This is the classic LSCM-atlas / D-Charts family.
  - METHOD-044 counts only as an offline sanity check (C69/C70). This is not adoption because the prototype exists.
  - Recommended as the product path. It replaces FastStaged's growth (closing BUG-160) only after the gates in section 6 pass.
- **C. Global partitions** (multicut, spectral, variational cuts). Heavier, not aware of disk topology or distortion, and METHOD-040's gate was refuted. Not now.

**Objective for B.** It is a heuristic; there is no claim of a global optimum.

  minimize Σ_{e∈cuts∖H} ℓ̂_e · exp(−γ·ψ_e) + μ·|P|

  subject to: every chart is a disk (after topology cuts), maps injectively, and has distortion D_preset ≤ τ; no chart crosses a hard boundary H.

- ℓ̂_e = ℓ_e/√A_total is the edge length made scale-free. μ is a dimensionless penalty per chart. γ ≥ 0 is the guidance strength. ψ_e ∈ [−1,1] is a signed preference for cutting at edge e.
- **Normalizing the property:** ŝ = (s − median)/(1.4826·MAD), using the existing MAD→RMS→unit fallback, clipped to ±6. Vertex values are area-weighted onto faces.
- **Three guidance modes:**
  - *discontinuity*, ψ = 1 − exp(−Δŝ²/2): cut where the property changes.
  - *prefer-high*, ψ = 2·percentile(ŝ) − 1: e.g. hide seams in creases.
  - *avoid-high*, ψ = 1 − 2·percentile(ŝ): keep seams out of salient areas.
- **Missing values:** non-finite or unsupported samples (use the curvature support mask, because unsupported vertices currently read as zero curvature) get ψ = 0 and are counted. Today `CaptureSegmentationFaceFeatures` fails the whole operation instead.
- **Cheap growth check:** each chart accumulates its total |angle defect| (dimensionless) and keeps a cone around its *average* normal.
  - Rationale: under a conformal map, the log scale factor u satisfies Δu = K, so ∫|K| predicts area distortion. It is a predictor, not a bound.
  - The preset sets these budgets; the real gate is always the solved map.

**Boundary types.** `UvAtlasSeamCutRecord` should gain a `Reason` field:
- **Semantic:** a region boundary; hard.
- **Hard:** a user or authored seam, a sharp edge above a threshold angle, or a non-manifold edge.
- **Guided:** a soft seam chosen by the property.
- **Topology:** a cut inside a region to make it a disk (the METHOD-045 approach).
- **Distortion:** a split made to meet τ.

Source region IDs, chart IDs and corner duplication stay three separate things.

**Robustness and determinism**
- **Disconnected components:** processed independently, in lowest-face-index order. All ties are broken by IDs.
- **Parallel chart solves:** results are collected in chart-ID order, so output is bit-identical at any thread count.
- **Non-manifold input:** non-manifold edges become forced seams, and bowtie vertices are split per fan. This only arises on the triangle-soup path, since `Geometry.MeshSoup` reports `NonManifoldEdge` when building halfedge meshes.
- **High genus or closed meshes:** validation (Euler characteristic 1, one boundary loop, disk vertex links) triggers shortest-path topology cuts or bisection.
- **Degenerate faces:** attached to a neighbouring chart, excluded from metrics and flagged. They never cause the whole atlas to be rejected, which FastStaged does today.
- **Very large meshes:** a compact (CSR) dual graph, O(F log F) growth, a cap on chart size for solves, and cancellation checkpoints.

**Limits of "any mesh"**
- Surface triangle meshes only; point clouds and curves are out of scope.
- Non-finite positions are rejected.
- Zero-area faces cannot get positive UV area.
- Non-orientable surfaces work only on the soup path.
- τ is a target under chart-count and time budgets, not a guarantee.
- Anatomical "parts" (limbs, necks) are not reliable with the current signals. METHOD-042/043 refuted several thickness and neck hypotheses (C64, C67, C68).

"Reliable" should therefore mean: **always a valid atlas, or a typed failure.**

## 3. Parameterization

**What "none/angle/area/both" can truthfully mean**
- Zero angle *and* zero area distortion means an isometry, which requires the chart to be developable (Gauss's Theorema Egregium). A sphere cannot be flattened this way in one piece.
- Single-triangle charts are always exact isometries. So zero distortion is always *possible*, at the cost of seams. The real problem is minimizing seams and chart count subject to τ.
- Each preset therefore defines what is measured and which solver runs by default. Only implemented solvers are selectable, since UI-036 forbade placeholder options.

| Preset | Default solver chain | Threshold, per chart after scale normalization |
|---|---|---|
| none | Tutte, uniform weights, **circle** boundary | validity only |
| angle | BFF automatic → LSCM → Tutte (order to be settled by corpus measurement) | angle ratio σ1/σ2 ≤ τ_a; area kept in check by the curvature budget |
| both | SLIM started from Tutte, once METHOD-022 exists; until then conformal solvers with a tighter τ | max(σ̂1, 1/σ̂2) ≤ τ_s |
| area | no dedicated map energy at first | exp(\|log σ̂1σ̂2\|) ≤ τ_A, met by splitting charts |

Pure area-preserving maps exist but are highly non-unique (arbitrary shear). No solver should be labelled "area" until an energy with an angle term (SLIM family) is implemented and tested.

**Metrics**
- Compute the Jacobian J per face in a local frame (double precision).
- Chart scale s_c = √(ΣA_uv/ΣA_3d); normalized singular values σ̂ = σ/s_c.
- Report area-weighted mean, p95 and max; L2 stretch; and the per-chart density ratio s_c/s_atlas.

**Flips versus global overlaps**
- A flip is local: a triangle with det J ≤ ε.
- A global overlap happens when all triangles are positively oriented but non-adjacent ones still overlap, e.g. a spiral-shaped chart.
- If every triangle is positively oriented and the chart boundary is a simple polygon with matching orientation, the chart map is injective (winding-number argument).
  - So check boundary simplicity with `Geometry.RobustPredicates` plus a grid broad phase, O(B log B).
- Between charts: rely on the packer's disjointness, then audit texel occupancy including padding.
- Re-validate in the float UVs actually published, after packing, because small triangles can degenerate. Never clamp.

**Scale and packing**
- Rescale each chart so ΣA_uv = ΣA_3d, then apply one global scale. Conformal maps have no natural scale, so this step is required for them.
- Pack with xatlas `PackCharts` (one material per chart; existing dependency). Keep the shelf packer as a deterministic fallback.
- Exhaustive xatlas placement was not uniformly better in earlier tests (C72) [R].

**Bounded retries**
- At most 3 solvers per chart.
- Then bisection (re-grow from the two farthest faces), depth ≤ ⌈log₂ F_c⌉ for a chart of F_c faces.
- Then single triangles. The total solve size stays O(F log F).
- Merge attempts are capped, and rejected pairs are cached.

**Failure and fallback semantics**
- Each chart reports its requested and actual solver and any split reason.
- The atlas reports `Success`, `SuccessWithFallbacks`, or a typed failure.
- `DistortionTargetMissed` is reported when budgets stop splitting; a strict mode fails instead.
- Constrained xatlas runs only per region piece and is reported as `UsedFallback`. Unconstrained xatlas runs only if the user explicitly chooses "ignore regions".

## 4. Baking

- **Correspondence is exact:** it is the same mesh, so each texel maps to a corner-UV triangle and barycentric coordinates in the source face. Keep the GPU raster as the product path. Add a small CPU reference rasterizer for tests only.
- **Vertex fields:** barycentric interpolation. Values stay continuous across seams because both sides share the source vertex.
- **Face fields:** constant per face via `gl_PrimitiveID`.
- **Labels:** nearest corner (or face constant), nearest-neighbour sampling, no mips.
- **Corner (halfedge) source fields:** later.
- **Invalid values** become "no data" texels (alpha 0, or a coverage mask) and are counted. Degenerate UV triangles are skipped and counted, rather than failing the whole bake.
- **Gutters:** padding needs coverage information, so raw floats need RGBA with alpha or a separate coverage mask. Bilinear filtering needs ≥2 texels of padding; mip level L needs ≥2^L. Defer mips.
- **Coverage:** normal rasterization drops triangles smaller than a texel. Report the fraction of 3D area that received at least one texel, predicted from texel density.
- **Texel density:** derive the resolution from a target texels-per-unit and the packing utilization; report what was achieved.
- **Stale bakes:** store an atlas fingerprint (topology plus corner UVs) and the property generation in each bake record.
  - Both the tabs and `RenderExtraction` must detect and show staleness.
  - Re-atlasing marks bakes stale; it never deletes them silently.

## 5. UI plan

- **Where it lives:** extend the existing `mesh.processing.parameterize_uv` window in `Sandbox.MethodPanels` into an atlas workspace with collapsible controls. This follows UI-036's decision to keep new state in the existing owner.
- **Split screen:** docking and a second 3D viewport are deferred (ADR-0025), so pin the UV window to half of the main viewport with `SetNextWindowPos/Size`.
  - **Auto side:** place it opposite the selected mesh's on-screen bounding-box centre, with hysteresis. Offer Left/Right/Auto as an override.
  - **"Frame in visible half":** move the camera sideways along its right vector so the mesh centres in the uncovered half. This needs no projection or renderer change, and picking is unaffected.
- **Tabs:**
  - An `Atlas` tab: wireframe, fill by chart or region, seams coloured by reason, distortion heatmap.
  - One tab per bake record from `TextureBakeSnapshot`: the baked texture as background, optional wireframe overlay, legend, a Pending/Ready/Failed/Stale badge, and a Rebake button.
  - **Hover readout:** computed on the CPU from the source property by locating the UV triangle, so it is exact and needs no GPU readback.
  - Pan and zoom are shared across tabs per entity.
- **Runtime changes:**
  - `EditorParameterizationUvViewRequest` needs an explicit background texture AssetId, resolved via `GpuAssetCache::GetView`.
  - GPU line indices for corner UVs must use `BuildMeshCornerAttributeSplit`, the same split the mesh upload and bake use.
  - A display colormap for raw-float bakes in `uv_view/background.frag` can come later.
- **Linked chart selection:**
  - Clicking in the UV pane selects a chart and shows a transient highlight in 3D (presentation only).
  - Picking a face in 3D highlights its chart in the UV pane, if face picking exists.
- **Job lifecycle:** reuse the existing job pattern with staged progress and cancel. Auto-open the workspace on success only if that entity is still selected.
- **Persistence:**
  - Atlas options go in a versioned config section with validated preview/apply (AGENTS.md §5).
  - Chart IDs can be recomputed from how faces connect in `h:texcoord`, so they need no storage.
  - Semantic regions need a codec field if they must survive save/load.
  - Save bake recipes and re-bake on load, rather than saving textures.

## 6. Slices and acceptance

**First slice, ≤1 day**
1. **Truthful backend:** set `options.Method` explicitly in the editor path. Add requested/actual/fallback fields to `EditorUvRegenerationCommandResult` and show them in `DrawUvRegenerationStatus`.
2. **Region input:** add optional per-face `FaceGroups` to `UvAtlasInput` and pass them to xatlas as `faceMaterialData`.
   - FastStaged returns a typed rejection when groups are supplied, rather than investing in a path BUG-160 may retire.
   - A post-check that no chart spans two groups turns violations into a typed failure; a size mismatch is a typed failure.
   - The runtime binds an optional face UInt32 property (e.g. `f:curvature_region`). The existing stale guard already covers face properties.
3. **Atlas drawing:** the view model draws corner-domain UVs on the CPU. GPU mode reports a CPU fallback for corner UVs.

Tests for this slice:
- A runtime test for backend truthfulness that fails today.
- The group constraint on a two-region cube and on a sphere.
- A corner-UV view test that deliberately replaces the "cannot draw" test.

**Later slices**
- **S1:** bake tabs, explicit background texture, fingerprint-based staleness.
- **S2:** the split-screen workspace.
- **S3:** normalized metrics, per-chart rescale, boundary-simplicity check, a `ValidateAtlas` report, bake coverage accounting and skipping of degenerate triangles.
- **S4:** path B as a CPU reference, following the method protocol, with presets none and angle.
- **S5:** SLIM, enabling the "both" preset.
- **S6:** parallel solves, `PackCharts`, 1M-face scaling, and the BUG-160 default decision.

**Test corpus**
- Generated CI fixtures: plane, open and closed cylinders, icosphere levels, torus, a genus-g perforated slab, cube and sharp prism, a seeded noisy sphere, slivers and degenerate faces, disconnected pieces, soup fins and bowtie vertices, and a 1M-face grid labelled `slow`.
- Local meshes pinned to exact source revisions: frog, sculpt, bunny10k, fandisk, dolphin, elephant, bumpy_torus, genus3, office_chair.

**Hard gates:** must pass 100% for every preset, over 3 repeated runs, at 1 and N threads.
- Every face covered exactly once, all UVs finite.
- Zero flips in the published floats, zero chart-boundary self-intersections.
- Zero texel overlaps including padding, zero charts crossing a region boundary.
- UVs inside [0,1] without clamping.
- Typed failures for unsupported input, and bit-identical outputs.

**Reported, not gated:** chart count, seam length/√A, normalized distortion distributions, packing utilization, density ratio, bake coverage, and time and memory per stage — per mesh, against both plain and constrained xatlas.

**Before making claims**
- Say "reliable" only after the gates pass and a fixed-revision review accepts it.
- Say "fast" only with clean Release, schema-v2 benchmark manifests against matched xatlas runs (AGENTS.md §8/8b).
- Predictions you can check today:
  - FastStaged on a 1×2×3 box gives per-chart density ratios ≠ 1.
  - The editor job actually reports FastStaged, not xatlas.

## 7. Objections to your hypothesis, open decisions, and what not to build

**Objections**
1. **The bake is not CPU.** It is GPU-only, so headless CI has no bake at all.
2. **Keeping regions separate from charts:** agreed. But regions should be optional and soft by default.
   - Segmentation labels can be fragmented (`MinimumRegionFaces = 2` is the default), and hard constraints copy every labelling defect into seams.
   - Absorb tiny regions before treating them as hard.
3. **Region growth as the starting point:** agreed only with average-normal cones, curvature budgets, validate-or-split and merges. Seed-plane growth is exactly BUG-160. The property mainly belongs in *where seams go*, not in chart membership.
4. **The xatlas fallback can obey regions** via `faceMaterialData`. It cannot honour the distortion presets, and it must be reported as a fallback.
5. **Presets:** "area" and "both" have no implemented solver yet. Do not advertise them until they do.
6. **Fix metrics and texel density before setting any thresholds or running comparisons.** FastStaged outputs are confounded by the density problem.
7. **The UI work is more than "add tabs".** Corner-UV display and explicit texture binding must come first, and stale bakes also affect the 3D mesh, not only the tabs.

GEOM-076 waits on METHOD-040, which was refuted. Re-scope it, or open a new task, for region-constrained atlas input instead of waiting.

**Open decisions**
- The editor default: real xatlas, or the new path. Keep import (Framework24 workflow W2) on the current path until benchmarked.
- Regions hard or soft by default.
- Which packer.
- What "area" means.
- Pinned window or a real second viewport.
- How bakes persist, and whether semantic regions persist.

**Do not build yet**
- Multicut, spectral or variational cuts; cone-based, seamless or OptCuts-style global maps.
- GPU solvers or new dependencies.
- Segmentation or strategy registries/frameworks.
- ImGui docking or multiple viewports.
- Mesh saliency.
- UDIM, GPU value readback, or mips for label bakes.
- Wholesale ports of the Python prototypes, or changes to the import default.

Record this direction in a micro task (AGENTS.md §1/§11). S4 and S5 follow the method protocol and the ARA evidence rules.