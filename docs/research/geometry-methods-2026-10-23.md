1. "please use the scholar inbox skill and do a literature research backwards in time (30 days) and the trending and my digest for new, cool, better approaches and methods in geometry processing. check for parts and ideas we can reuse in our engine and should adopt. also look a implementation and feature gaps we have without overloading and overengineering the engine."
2. "add those papers you find to zotero and prepare a plan of what you propose we do."

# Geometry literature review and proposed adoption plan — 2026-10-23

Status: literature assessment and proposal, not implementation authorization or an engine performance result. Search ID: `geometry-methods-2026-10-23`. Retrieval date/timezone: 2026-10-23, Europe/Berlin.

The standing goal is [Framework24 product convergence](../product/framework24-convergence.md), particularly W3 (geometry analysis), W4 (editing without losing work), and W5 (inspectable properties). The operator explicitly requested this research despite the standing pause on research expansion. This does not change the implementation priority automatically. `ara/logic/problem.md` has no populated additional goal. The shortest useful path is to improve existing operations and their checks before adding another method family.

## Coverage and provenance

- Scholar Inbox's configured authenticated MCP was used directly. No separate installed Scholar Inbox SKILL.md was found. Server tool discovery exposed **only `get_digest`**. Codex's existing URL-only registration was repaired to reference `SCHOLAR_INBOX_MCP_TOKEN`; the secret is outside the repository. MCP initialization and tool calls succeeded. The unauthenticated `/health` request returned HTTP 403. The default digest for October 23 succeeded with zero papers. Current chat tool exposure was not reloaded by restarting the desktop app.
- **Requested interval: September 24–October 23 inclusive (30 calendar dates).** First queried the whole interval with `top_k=200`; the server capped it at 100. Then queried every date separately with `top_k=100`, newest first: `get_digest(start_date=day, end_date=day, top_k=100)`. This returned **1,082 distinct recommendations**, deduplicated by Scholar Inbox paper ID. September 29 and October 14 reached the 100-paper cap. Empty days are empty digest results, not evidence of no publications. October 13 returned no recommendations despite indexed papers. The interval response reported 1,038 failed downloads across its indexed corpus, not necessarily shortlisted papers.
- **Trending is not covered.** The authenticated endpoint offered no Trending tool, and the in-app website was signed out. A personalization score was not treated as community popularity. Public discovery pages were used only as leads, never as a replacement for the user's Trending feed.
- Digest date and publication date are distinct. Some recommendations predate September 24; they were excluded from the new-publication shortlist. All 12 selected works have primary arXiv submission dates inside the interval. EGGroW was read at **v2, October 2**; the other shortlisted arXiv versions are v1. These dates are not journal publication dates.
- Existing knowledge reused first: Zotero semantic query `geometry processing mesh repair remeshing surface reconstruction point cloud simplification geodesics curvature` (7 results), lexical `remeshing` (5 results), the note attached to `0:NTQ4VYE9` (Hu et al., error-bounded remeshing), and the October 9 BCPD review at `docs/research/reports/BCPD Erweiterungen und Varianten.md`. The shared research pointer index had no prior entry for this question. Zotero's index reported an October 23 refresh before searching.
- Discovery web queries: `"Scholar Inbox" MCP tools digest trending skill`; `geometry processing remeshing reconstruction October 2026 September 2026 arxiv`; `site:scholar-inbox.com MCP skill`; `site:arxiv.org/abs/2610 geometry mesh remeshing processing`; `site:arxiv.org/abs/2609 "geometry" "Sep" remeshing`. Broad web searches were noisy. Direct arXiv abstract/version pages verified the shortlist. Titles were filtered for mesh, point cloud, surface, reconstruction, geodesic, registration, curvature, topology, PDE and related terms; shortlisted abstracts were then read manually. This is a targeted assessment, not exhaustive coverage of 49,960 indexed interval records or all 1,082 abstracts.
- Deeper selective reading: PNF sections 4.1–4.3 and appendices D/F; localized refinement sections 5–6 and appendices D–F; clique/consensus definitions and counterexample discussion. Other works are abstract-level unless their source note says otherwise. Downloading a PDF does not mean its full text was read.
- Code discovery followed official paper links. Localized refinement's GitHub repository metadata reports MIT, but its default-branch root contained only `LICENSE`; no runnable implementation or weights were present there. Superquadric decomposition's `gair_ransac` README was inspected: Python research workflow, graph cuts, and mixed-integer model selection; no license file appeared in its root listing and implementation correctness is unverified. PNF's project page and clique/consensus demos were inaccessible. No external code was executed.

Zotero collection: **IntrinsicEngine — geometry methods**, key `HTQHSQA2`, personal library scope `0`. Identifier/title searches preceded import; **12 items, 12 PDFs and 12 agent notes** were verified afterward. Earlier library content was retained. Source notes own detailed bibliographic information and reading depth.

## Shortlist, newest first

These are author-described contributions; “use” below is our proposed application, not reproduced evidence of an improvement.

| First submitted | Paper / read version | Zotero key | Engine decision |
| --- | --- | --- | --- |
| Oct 20 | [DEC-based Whittle–Matérn Fields](https://arxiv.org/abs/2610.28488v1) | `0:N3I726XG` | Watch: sparse probabilistic fields could reuse DEC; no present uncertainty-aware interpolation consumer. |
| Oct 19 | [Maximum Clique vs Maximum Consensus](https://arxiv.org/abs/2610.27275v1) | `0:PZG47VWP` | Adopt adversarial test ideas. Pairwise consistency does not certify one common rigid transform. Keep the current residual-scored RANSAC. |
| Oct 19 | [Surface Matching with Rigid-Mode Separation](https://arxiv.org/abs/2610.26238v1) | `0:NYJIQFJQ` | Watch: separate pose change from deformation for analysis. LDDMM derivation is not a drop-in CPD/BCPD change. |
| Oct 16 | [Surface Biharmonic FEM without Stabilization](https://arxiv.org/abs/2610.23492v1) | `0:Q534D3A3` | Defer: interesting numerical formulation, but new element assembly/DOFs without a demonstrated current need. Closed-surface assumptions matter. |
| Oct 15 | [Tessellation-Free Displaced-Surface Queries](https://arxiv.org/abs/2610.21584v1) | `0:EQ9BK97D` | Watch: conservative affine height bounds are promising for an actual displaced-surface feature, not a replacement for mesh/point spatial indices. |
| Oct 15 | [VoroDuce](https://arxiv.org/abs/2610.21811v1) | `0:6KYQJHWU` | Defer: volumetric Voronoi simplification is a different representation from triangle QEM. |
| Oct 13 | [Range-Scan Reconstruction under Visibility Constraints](https://arxiv.org/abs/2610.18005v1) | `0:6EQ7K74W` | Preserve the idea for a scanner workflow: first-hit ray evidence needs acquisition origins/depths. Do not add its neural field pipeline now. |
| Oct 8 | [RGBD Mesh Refinement with Depth and Symmetry](https://arxiv.org/abs/2610.11187v1) | `0:DFF59AA8` | Defer: sensor/calibration and valid symmetry assumptions need a concrete use case. |
| Sep 28 | [Projective Normal Fields](https://arxiv.org/abs/2609.34784v1) | `0:GHNFSWTX` | Best bounded method prototype: isolate orientation-free normal estimation. The tensor optimum is not a surface-recovery guarantee; downstream UDF extraction can remain double-layered. |
| Sep 28 | [Superquadric Decomposition with Geometric-Aware Inlier Refinement](https://arxiv.org/abs/2609.35725v1) | `0:JX9SRHYJ` | Reuse the question “do residuals and normals agree?” in existing parts work. Defer a new superquadric stack. [Official research code](https://github.com/Tededo02/3D_superquadric_decomposition/tree/gair_ransac) links publication DOI `10.1016/j.gmod.2026.101343`. |
| Sep 26 | [Geodesic Gromov–Wasserstein / EGGroW](https://arxiv.org/abs/2609.32824v2) | `0:HMAHHYB4` | Watch for partial correspondence. Random-feature/geodesic transport adds a substantial objective and solver; first finish the existing Nyström comparison. |
| Sep 25 | [Learned Localized Mesh Refinement](https://arxiv.org/abs/2609.31946v1) | `0:JXRGBKBN` | Adopt the region-local editing requirement. Defer the learned generator; classical local remeshing cannot claim to recover missing semantic detail. |

High-ranking generative/splatting papers such as 3DRAG, WorldRecon, MEGA and OptimusMesh were screened out of the adoption list: pretrained representations, training/runtime dependencies and synthesized geometry do not address the immediate engine gaps with a small change. Older RADmesh and HKTex leads were not counted as new papers in this interval.

## Source-backed engine fit and gaps

Inspection started at `a9df44fe3` on `claude/ui-083-menus-and-per-primitive-appearance`. Other work modified renderer/runtime files during research; none was edited here. These are source observations, not fresh runtime acceptance claims. Some inventory rows are explicitly evidence gaps, not missing implementations.

| Existing owner | What can be reused / what is missing |
| --- | --- |
| [`Geometry.PointCloud.Features`](../../src/geometry/Geometry.PointCloud.Features.cpp), `EstimateCoarseAlignment` | Deterministic feature matching, edge checks, Kabsch and actual transformed-residual consensus already exist. A maximum-clique registration implementation is unnecessary. Existing tests cover known transforms/outliers; the inspected tests do not include the proposed mirrored-tetrahedron case. |
| [`Geometry.PointCloud.Normals`](../../src/geometry/Geometry.PointCloud.Normals.cppm), [`Geometry.PCA`](../../src/geometry/Geometry.PCA.cppm), [`NormalEstimationConfig`](../../src/runtime/Modules/NormalEstimation/Runtime.NormalEstimationConfig.cppm) | Neighbor reuse, double 3×3 eigensolver, PCA normals, MST orientation, validated config and publication are present. The inspected normals result has no per-point axial-separation output. PNF requires a coupled tensor solve, not simply another PCA call. |
| [`AdaptiveRemeshing`](../../src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cppm), [`Remeshing`](../../src/geometry/Geometry.HalfedgeMesh.Remeshing.cppm) | Existing uniform/adaptive sizing, split/collapse/flip/smooth, boundary handling, work/growth limits and frozen-reference projection. No region mask in the inspected APIs. |
| [`MeshTopologyOperations`](../../src/runtime/Editor/Operations/Runtime.MeshTopologyOperations.Topology.cpp), `ComputeMeshRemesh` / `ApplyEditorMeshRemeshCommand` | Existing undoable/job-based remesh path. The command has no region selector and explicitly reports UV discard on topology replacement. Locality cannot be promised by adding only a UI toggle. |
| [`SurfaceReconstruction`](../../src/geometry/Geometry.SurfaceReconstruction.cppm), [`MarchingCubes`](../../src/geometry/Geometry.MarchingCubes.cppm) | Oriented-point field on a regular grid and isosurface extraction exist. The inspected reconstruction API has no acquisition-ray input. A Hoppe-style field is not automatically a certified 1-Lipschitz field. |
| [`curvature_segmentation`](../../methods/geometry/curvature_segmentation/README.md), existing parts work | Already the home for boundaries/labels. Any normal-consistency idea must be compared there before creating a second decomposition system. |
| [Feature inventory](../product/framework24-feature-inventory.md) and [METHOD-083](../../tasks/backlog/methods/METHOD-083-nystrom-upstream-alignment.md) | Workflow/evidence closure and the author-program registration comparison remain relevant. This review does not retire their acceptance criteria or replace current owners. |

Serena symbol/overview queries were used alongside `rg`; its C++ reference query for coarse alignment returned no references, so caller conclusions rely on direct text/source inspection, not on that empty result.

## Proposed order — one slice at a time

Every step serves Q1's reusable methods/gaps and Q2's concrete plan. Effort is an engineering estimate, not a commitment. No research result is asserted and no experiment is authorized by this document alone.

### 1. First step, at most one day: strengthen registration checks

Use a generic noncoplanar tetrahedron and its reflection with fixed correspondences. Assert that all pairwise lengths match, while the returned proper rigid transform has positive determinant and **cannot classify all four as inliers** at a specified small world-unit tolerance. Require successful recovery of a three-point consensus, and independently recompute the inlier residuals from the returned transform; an identity transform with zero inliers must not pass. Do not incorrectly require the entire operation to fail. Add an ordinary rotated/translated positive control and a scale/tolerance variant. Reuse `PointCloudFeatures`; change production code only if these tests expose a defect, then stop at that newly exposed scope as the drift rule requires.

Done: the regression fixtures pass against the existing implementation and fail under a deliberate reflection-accepting or pairwise-counting mutation. Tests for existing correct behavior need not fail on the unmodified implementation. No new solver, config, dependencies or UI.

Verification after implementation:

```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^PointCloudFeatures\.'
```

### 2. First method direction: normal ambiguity, then bounded PNF intake

Start with a small diagnostic over the current PCA normals (roughly 1–2 days including the existing property publication path): expose a documented dimensionless separation between the smallest covariance eigenvalue and its neighbor. With descending covariance eigenvalues, a candidate is `(lambda2-lambda3)/lambda1` when the largest eigenvalue is positive, with explicit invalid/degenerate handling. It is **PCA axial separation**, not PNF's optimized-tensor confidence or a calibrated probability.

Check analytic plane, line, isotropic and invalid fixtures, rotation/scale invariance and count-matched publication with undo/save behavior. Reuse the current eigensolver, neighborhood providers, normal operation, scalar visualization and validated configuration. Do not add a generic uncertainty framework.

Then, only if the user wants the method itself, prepare a bounded CPU PNF normal-estimation prototype (roughly 3–5 days for intake/reference; integration estimate awaits that work). Source mapping: sections 4.1–4.3 define soft tensors, prior and fixed-graph objective; appendix F separates normal solving from reconstruction. Reuse existing neighbor queries and 3×3 algebra. Pin positive anchoring, units/normalization, graph construction, PSD trace-one projection, termination and memory limits. Test objective/constraint invariants and sign reversal before any comparison. Keep the full heat/Poisson/UDF/extraction pipeline out of this slice.

Any outcome-bearing comparison needs a reviewed experiment-preflight packet: existing PCA baseline, constant/random-axis nulls, clean and corrupted known-normal inputs, junction versus ordinary regions, sign-invariant angular error `acos(abs(dot(n_est, n_ref)))` for unit bidirectional normals, retained-point coverage, time and memory. Treat ambiguous junction references separately from unique normal references. A low angular error achieved by discarding difficult points is not success. Freeze thresholds, seeds and budget before running; this review supplies no result or unfrozen run permission. If PCA is sufficient, stop and keep the diagnostic.

### 3. First editing direction: local refinement with a truthful attribute contract

Adopt the localized paper's user requirement through existing classical remeshing. Estimate 3–5 days **after** an at-most-one-day design/test slice establishes whether current topology publication can preserve untouched attributes. No learned model, tokenizer or new deformation framework.

The smallest candidate is a selected interior patch with a frozen boundary ring. Every split, collapse, flip and smoothing phase must respect the region. Outside it, positions, connectivity and existing properties must remain unchanged; storage indices may be remapped if ownership/provenance is preserved. Reuse primitive selection and the existing command/history/job lane. Explicitly resolve new-element scalar/vector/label and corner-UV handling; do not interpolate labels as numbers or silently erase untouched UVs. Existing UV-discard behavior is a known obstacle, not something to hide.

Done: empty/all/partial selections behave predictably; a seam-crossing textured fixture keeps outside data and reports any permitted inside loss; boundaries do not crack; undo/redo and save/reload preserve the result. If this needs a general attribute-transfer framework or a much larger publication rewrite, stop at the scoped design result and return the concrete tradeoff instead of expanding the engine silently. Classical refinement redistributes existing geometry; it does not reproduce learned missing-detail synthesis.

## Deferred directions and next search

Do not add LDDMM, EGGroW, a neural reconstruction runtime, volumetric Voronoi storage, probabilistic DEC fields, or a new superquadric service from this scan. Keep acquisition-ray preservation and displaced-surface bounds as requirements for future consumers, not new infrastructure now. No performance win, state-of-the-art status, or engine-wide superiority is established.

Refresh when Trending becomes accessible, a missing implementation is released, or one proposed slice is selected. Use overlap to catch late indexing; keep publication/update dates separate. Resolve PNF's actual algorithm/code/license details before choosing an implementation. Existing recent BCPD research need not be repeated.

## Delivery checks

No engine code, benchmark or experiment was run or changed for this review. No new task was activated and no existing task acceptance was altered. This file and Zotero are the durable records; temporary sources and review receipts are removed after delivery checks.

- Documentation checks passed: strict task policy (648 files, zero findings), strict relative links (4,681 links), strict ARA claims (125 claims), and YAML parsing of the four bookkeeping records. These are structural checks, not scientific validation.
- Zotero index refresh succeeded: 12 new records, zero errors; index total 10,264 documents. All 12 selected items have PDF attachments and source notes in collection `HTQHSQA2`.
- Independent read-only reviewer `/root/literature_plan_review` (GPT-6 Astra, fresh context, no delegation) returned: **“Verdict: approve”** and **“No blocking findings.”** The reviewed document SHA256 was `a94e38443b31a2cb64511428194fa8744faf05196db00b25cc37404143b411be`. Its optional refinements—requiring a non-vacuous three-point registration result and specifying the bidirectional normal metric—are incorporated above. The earlier Claude invocation produced no verdict and is not counted as a review.
- Review limits: live Scholar Inbox/Zotero receipts were accepted as supplied packet evidence; external implementation correctness, complete paper proofs, builds and experiments were not independently checked. Trending remains uncovered, and effort estimates remain provisional.

Final delta review by the same independent reviewer: **“Verdict: approve. No regression in the delivery delta; both test refinements are correctly incorporated, and the delivery record preserves the review’s limits.”** Reviewed SHA256: `5ecfdffeaaac15f32bc53323549e2af26c9a8fc91c28c369747c50328271a3d7` (before this receipt was appended).
