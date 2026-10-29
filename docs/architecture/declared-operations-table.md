# Named operations for ADR 0031 (projection basis)

Status: estimate, not a measurement. ADR 0031 (`docs/adr/0031-declared-operations.md`) asks for a
table of named operations with their removable code, as the basis of the phase 2 gate ("projected
net saving at least 5,000 lines"). This table is that basis. It was made on 2026-10-29 at
`f96a13e2d` by a read-only agent from function boundaries (`wc` lines, comments and blank lines
included, tests left out), and the main session spot-checked four sizes against the code (`ARCH-021`).
Phase 2 replaces the estimates of the migrated operations with measured numbers.

**Path keys.**
- O = `src/runtime/Editor/Operations/Runtime.`
- C = `src/runtime/Config/internal/Runtime.`: NB = `PointNeighborhoodConfigCodecs.cpp`,
  PF = `PointFeaturesConfigCodecs.cpp`, NR = `PointNormalsRegistrationConfigCodecs.cpp`,
  FC = `FeatureConfigCodecs.Detail.cpp`
- M = `src/runtime/Modules/`
- MPP / MTP / PSu = `src/app/Sandbox/Editor/Sandbox.{MeshProcessingPanels,MethodPanels,PanelSupport}.cpp`
- AG = `src/runtime/Agent/Runtime.AgentOperations.Operations.cpp`

**Column keys.**
- Input → output kinds: PS point samples, MS mesh surface, MT mesh topology edit, PG property graph,
  2E two entities, Cn n property columns, TR topology replacement, Cu custom publication.
- GPU: sc scalar run, fr framed queries, nm normals-like, sm smoothing-like, own its own backend or
  service, – none.
- Lines by concern:
  - Cfg: config declaration, codec and wrappers
  - Cap: capture and compute glue
  - Job: job and GPU glue
  - Pub: publication and undo
  - Res: result struct and slots
  - Ag: agent row
  - UI: panel
- Now: the measured total. Keep: the estimate under the ADR.
- A split inside a mixed function is a judgement, about ±25%; a row's total rests on measured ranges,
  about ±10%.

| Operation | Main ranges | In → out; GPU | Cfg | Cap | Job | Pub | Res | Ag | UI | Now | Keep |
|---|---|---|---|---|---|---|---|---|---|---|---|
| kernel_density | O PointFieldOperations.Processing.cpp 61-91, 129-362; NB 91-121, 345-446; MPP 2629-2739 | PS → C1 f32; sc | 184 | 88 | 69 | 10 | 29 | 4 | 119 | 503 | 100 |
| point_spacing | Processing 92-128, 363-402; NB 122-150, 451-549; MPP 3041-3159 | PS → C1 f32; sc | 179 | 94 | 69 | 10 | 31 | 4 | 127 | 514 | 100 |
| density_weight | O PointAnalysisOperations.DensityWeights.cpp 46-452; NB 155-195, 554-681; MPP 2740-2876 | PS → C1 f32; sc | 223 | 235 | 193 | 23 | 37 | 4 | 145 | 860 | 180 |
| keypoints | O PointAnalysisOperations.Keypoints.cpp 57-822; PF 48-61, 169-245; MPP 2323-2478; AG 775-800 | PS → C2 u32/f32; sc + fr | 144 | 298 | 492 | 32 | 33 | 32 | 164 | 1195 | 230 |
| descriptors | O PointAnalysisOperations.Descriptors.cpp 42-480; PF 63-77, 246-336; MPP 2479-2628 | PS + normals → C33 f32; fr | 161 | 268 | 182 | 30 | 32 | 4 | 162 | 839 | 220 |
| outlier | O PointAnalysisOperations.Outliers.cpp 57-1055; NB 36-88, 198-340; MPP 2114-2322 | PS → C2 + stamp, compaction: Cu; own | 254 | 328 | 539 | 188 | 55 | 4 | 219 | 1587 | 420 |
| normals | O NormalOperations.Processing.cpp 64-1384; NR 24-226; MPP 1798-2113, 4085-4134 | PS or MS → C1 vec3; nm | 291 | 517 | 796 | 71 | 92 | 4 | 376 | 2147 | 480 |
| bilateral | O PointSetOperations.Bilateral.cpp 42-656; PF 34-45, 98-168; MPP 3160-3275 | PS → C1 vec3 (alias); fr | 143 | 290 | 278 | 67 | 40 | 4 | 120 | 942 | 210 |
| point_construction | O PointConstructionOperations.Processing.cpp 68-702; PF 79-97, 337-469; MPP 2877-3040 | PS → new mesh: Cu; fr | 238 | 434 | 218 | 26 | 50 | 8 | 168 | 1142 | 350 |
| progressive_poisson | O PointSetOperations.Processing.cpp 66-1721; M ProgressivePoisson GPU backend; FC; MTP | PS → C4 + visualization: Cu; own | 449 | 625 | 2606 | 655 | 65 | 9 | 620 | 5029 | 2850 |
| icp | O RegistrationOperations.Processing.cpp 67-1242; NR 54-67, 231-324; MPP 3276-3546; AG 711-725, 952-974 | 2E → transform: Cu; fr | 179 | 568 | 506 | 92 | 98 | 40 | 284 | 1767 | 600 |
| cpd | O RegistrationOperations.CoherentPointDrift.cpp 81-1063; M Registration; MPP 3547-4033 | 2E → transform, positions, displacement: Cu; own | 371 | 345 | 911 | 144 | 123 | 50 | 574 | 2518 | 1200 |
| point_sampling | O PointSamplingOperations.cpp 60-431; M PointSampling; MPP 5350-5486; AG 749-773 | PS → C2 or new cloud: Cu; fr | 298 | 168 | 322 | 115 | 28 | 33 | 185 | 1149 | 360 |
| glyph_order | O PointSamplingOperations.GlyphOrder.cpp 51-201; Sandbox.DomainPanels.cpp ~785-830 | PS → C1 f32 (-1 for deleted rows); – | 6 | 128 (with Pub) | 0 | – | 18 | 0 | 45 | 197 | 70 |
| k_means | M Clustering; FC; O PointCloudServiceOperations.cpp 25-98, 177-200; MTP; AG 802-872 | PS → C1 u32; own, chunked | 314 | 250 | 1670 | 260 | 520 | 83 | 358 | 3455 | 650 |
| consolidation | M PointCloudConsolidation; FC; MTP; AG 874-950 | PS → positions, count changes: Cu; own | 711 | 960 | 3353 | 395 | 817 | 87 | 1212 | 7535 | 3000 |
| property_smoothing | O MeshFieldOperations.Smoothing.cpp 66-1268; MPP 4149-4502 | PG → C1 any kind; sm | 310 | 229 | 682 | 82 | 77 | 6 | 366 | 1752 | 600 |
| spectral_modes | O MeshFieldOperations.Eigenbasis.cpp 53-451; MPP 4503-4699 | PG/MS → k columns, k at run time: Cu; – | 185 | 217 | 0 | 83 | 17 | 6 | 202 | 710 | 220 |
| harmonic_field | O MeshFieldOperations.HarmonicField.cpp 49-477; MPP 4700-4935 | PG → C1 typed; – | 216 | 223 | 0 | 76 | 16 | 6 | 241 | 778 | 170 |
| scalar_gradient | O MeshFieldOperations.Gradient.cpp 43-277; MPP 4936-5035 | MS → C1 vec3; – | 104 | 147 | 0 | 42 | 14 | 6 | 103 | 416 | 75 |
| mesh_curvature | O MeshFieldOperations.Curvature.cpp 140-710, 1384-1821, 2054-2262; M Curvature; MPP 1020-1134 | MS → up to C6 mixed; CPU job | 200 | 234 | 458 | 500 | 207 | 8 | 131 | 1738 | 180 |
| curvature_segmentation | Curvature.cpp 711-1383, 2263-2784; M CurvatureSegmentation; FC; MPP 239-273, 1135-1493 | MS → face and edge properties: Cu; – | 686 | 863 | 0 | 490 | 57 | 8 | 412 | 2516 | 650 |
| geodesics | O MeshFieldOperations.Geodesics.cpp 54-245; M Geodesics; MPP 5036-5226 | MS → C1 f64; – | 206 | 286 | 0 | 82 | 18 | 8 | 196 | 796 | 170 |
| mesh_denoise | O MeshTopologyOperations.Topology.cpp (four ranges); MPP 928-1019 | MT → C1 vec3 (alias); – | 46 | 175 | 164 | 224 | 186 | 21 | 110 | 926 | 150 |
| mesh_remesh | Topology.cpp (five ranges); MPP 1494-1606 | MT → TR; – | 50 | 207 | 165 | 9 | 148 | 22 | 131 | 732 | 155 |
| mesh_subdivide | Topology.cpp (four ranges); MPP 1607-1709 | MT → TR; – | 40 | 320 | 159 | 9 | 91 | 18 | 121 | 758 | 210 |
| mesh_simplify | Topology.cpp (five ranges); MPP 1710-1797 | MT → TR; – | 52 | 288 | 174 | 9 | 121 | 21 | 106 | 771 | 200 |
| scalar_ridge | O ScalarRidgeOperations.cpp 51-467; MPP 5227-5349; AG 579-631 | MS → new graph entity and properties: Cu; – | 105 | 160 | 0 | 190 | 40 | 53 | 128 | 676 | 280 |
| parameterization | O ParameterizationOperations.cpp 58-1735; FC; MTP; PSu 2786-2900 | MS → corner UV and diagnostics: Cu; – | 1093 | 819 | 0 | 409 | 181 | 17 | 1474 | 3993 | 1650 |
| uv_regeneration | O ParameterizationOperations.Processing.cpp 1-1563; MTP 3333-3771; PSu 1622-1766 | MT → TR with seams; – | 150 | 806 | 264 | 427 | 117 | 0 | 584 | 2348 | 900 |

Texture bake (about 6,300 now, about 5,500 kept as an asset service) is left out of the totals.
The glyph_order row was corrected on review: the agent's split added up to 269 lines, more than its
cited ranges hold (151 + 46), and its output is a float column, not `u32`.

## Totals (30 operations)

- By concern now: config 7,588; capture 10,570; job and GPU 14,270; publication 4,750; results
  3,358; agent 570; panels 9,183.
- About 50,290 lines now, about 16,630 projected to remain, about 33,630 projected removable. About
  4,700 of the remaining lines are GPU dispatch under `src/runtime/Modules` that stays.
- Conservative cases:
  - With the non-GPU part of "Keep" at 1.5 times the estimate, about 27,800 lines are removable.
  - Without progressive Poisson, k-means and consolidation as well, about 24,200 are removable.
  - The phase 2 gate (at least 5,000 net) holds with a wide margin either way, before the
    framework's own size, which is not estimated.
- Phase 0 and 1 subset (point samples in, scalar columns out, scalar or no GPU): kernel_density,
  point_spacing, density_weight, keypoints and glyph_order have about 3,270 lines now, 680 kept and
  2,590 removable. Of keypoints, only the CPU and VulkanCompute paths fit; its VulkanLBVH backend
  uses framed queries.

## Largest uncertainties

1. **Service operations.** Consolidation, k-means and progressive Poisson mix lifecycle code
   (removable) with dispatch recording (stays). The estimate assumes most of it stays: about ±4,000.
2. **Panels.** The projection assumes about 20 operations accept the default operation panel. The
   kept custom-panel lines are estimates: about ±2,000.
3. **Shared glue and framework cost.** Family glue owned by no single operation is outside the rows,
   for example:
   - `GeometryProcessingOperations.*`, about 3,900;
   - `PointScalarTransaction`, 750;
   - the frame files, about 270;
   - AG 1-404;
   - the mesh-field resolve.

   The framework replaces part of it, and its own size is not estimated: about ±3,000.
4. **Output and input kinds.** Several operations need more than ADR 0031's first kinds:
   - a column count set at run time (spectral modes);
   - columns that alias an input (bilateral, denoise, normals);
   - two entities (ICP, CPD);
   - a changed point count (consolidation);
   - UV seams (UV regeneration).

   Each one that stays custom keeps more lines.
