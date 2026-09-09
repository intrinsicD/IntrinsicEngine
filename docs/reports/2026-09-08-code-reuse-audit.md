# Code reuse audit — 2026-09-08

The audit identifies **38 actionable reuse opportunities**, grouped by the shared responsibility rather than counting every matching block separately. The strongest opportunities concern shared editor decision rules, geometry IO helpers, GPU identity/layout contracts, and repeated resource-management code.

This is a source review of the **current dirty working tree**, including untracked source files, based on HEAD `314db9ea5`. Existing implementation changes were left untouched. Priorities express the value of addressing duplication; they are **not assertions of reproduced bugs**.

## Scope and method

- Scanned **1,556 project-owned source/build/workflow files**, containing **581,467 physical lines**: `src/`, `methods/`, `benchmarks/`, `tests/`, `tools/`, `cmake/`, `research/`, `assets/shaders/`, `.github/workflows/`, and the root `CMakeLists.txt`.
- The exact-match probe ignores whitespace, comments, import/include lines and punctuation-only lines; it finds repeated runs of at least nine substantive lines. It produced **2,357 candidate block pairs**. Those are overlapping lexical candidates, not 2,357 findings or an estimate of removable lines.
- Followed with identifier-normalized probes and targeted searches for existing helpers, duplicated function names, hashes, numerical routines, shader layouts and shared-index adoption.
- Manually reviewed the source for each numbered finding below and checked the proposed ownership against the repository layering rules. The detector is a discovery aid, not a compiler or semantic proof.
- Excluded external/vendor code, build output, generated skill mirrors, historical task/evidence trees and Framework24 reference code. No source file was treated as needing consolidation merely because it implemented a distinct backend, format, algorithm or test case.
- **Completeness limit:** this is a broad, systematic inventory of confirmed opportunities, not proof that every semantic duplication has been found. Large test files and low-value lexical matches were sampled. Additional candidates are distinguished below.
- Session-local probe artifacts: `/tmp/intrinsic_reuse_scan.py`, `/tmp/intrinsic-reuse-scan.json`, and the normalized scan files. These are scratch artifacts, not maintained repository tools.
- Verification: source comparisons; all 155 local source/document links and cited source-line bounds checked; `python3 tools/docs/check_doc_links.py --root . --strict` passed (3,917 relative links); report whitespace checked. No C++ build or test suite was run because the audit changes no implementation.

## Finding index

High priority means a shared eligibility, identity, parsing, layout or lifetime rule currently has multiple implementations. Medium priority means a clear shared helper with a useful maintenance payoff. Low priority covers small projections and test convenience code.

| ID | Priority | Area | Shared responsibility |
| --- | --- | --- | --- |
| R01 | high | editor | [Import eligibility and disabled reasons](#r01) |
| R02 | high | editor | [Property catalog and visualization eligibility](#r02) |
| R03 | high | editor | [Vertex-channel source preflight](#r03) |
| R04 | high | editor | [Geometry metadata signatures](#r04) |
| R05 | high | editor | [Transform undo adapter](#r05) |
| R06 | high | editor | [Render-hint undo adapter](#r06) |
| R07 | medium | editor | [History-status conversion](#r07) |
| R08 | medium | editor | [Scene-file and import result messages](#r08) |
| R09 | low | editor | [Job-to-editor model conversion](#r09) |
| R10 | medium | app | [Repeated panel widgets](#r10) |
| R11 | medium | app | [Appearance command construction and bake UI helpers](#r11) |
| R12 | high | runtime | [Queued asset import lifecycle](#r12) |
| R13 | medium | runtime | [Queued scene IO bookkeeping](#r13) |
| R14 | high | geometry | [Point-cloud IO bypasses existing text helpers](#r14) |
| R15 | high | geometry | [PLY scalar/header machinery](#r15) |
| R16 | medium | geometry | [ASCII and binary PCD export preamble](#r16) |
| R17 | medium | geometry | [Ray/AABB slab intersection](#r17) |
| R18 | medium | geometry | [Curvature median and robust scale](#r18) |
| R19 | high | runtime/graphics | [Surface-index fingerprint contract](#r19) |
| R20 | medium | runtime | [Geometry upload channel setup](#r20) |
| R21 | high | graphics | [Packed transient/overlay upload allocation](#r21) |
| R22 | medium | graphics | [GPU surface bucket draw recording](#r22) |
| R23 | high | graphics | [Transient memory placement algorithm](#r23) |
| R24 | high | vulkan | [Initial and recreated swapchain construction](#r24) |
| R25 | medium | vulkan | [Transfer and readback submission](#r25) |
| R26 | medium | graphics | [Debug/overlay pipeline descriptor defaults](#r26) |
| R27 | medium | runtime | [No-op command contexts](#r27) |
| R28 | high | shaders | [Two copies of the culling shader](#r28) |
| R29 | high | shaders | [Promoted surface material sampling](#r29) |
| R30 | high | shaders | [Progressive-Poisson GPU layout and cell encoding](#r30) |
| R31 | high | shaders | [K-means GPU layout declarations](#r31) |
| R32 | medium | shaders | [Point/surfel EWA math](#r32) |
| R33 | medium | benchmarks | [Curvature profile measurement helpers](#r33) |
| R34 | medium | benchmarks | [Smoke result JSON envelope](#r34) |
| R35 | medium | tooling | [CI toolchain and vcpkg setup](#r35) |
| R36 | low | tests | [Graphics inspection and setup helpers](#r36) |
| R37 | low | tests | [Editor geometry fixtures](#r37) |
| R38 | medium | geometry/runtime | [Curvature parameter validity rules](#r38) |

## Evidence and smallest useful consolidation

<a id="r01"></a>

### R01 — Import eligibility and disabled reasons

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:129](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L129); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:152](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L152).

Both files contain the payload list, importer availability checks, reason builders and the complete EvaluateFileImportPrerequisites implementation. The largest exact match spans 263 physical lines.

**Reuse:** Put the pure evaluator and its result record in runtime editor internals; both the snapshot builder and import command should call it. This prevents the displayed availability rules and actual command validation from diverging.

<a id="r02"></a>

### R02 — Property catalog and visualization eligibility

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:435](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L435); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:555](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L555); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:632](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L632); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:817](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L817).

Copies include internal/connectivity-property filtering, scalar eligibility, domain conversion, property-set lookup, catalog supported kinds and AppendVisualizationPropertiesForDomain.

**Reuse:** Share these runtime editor rules as free functions over GeometryEntityAvailability and canonical property references. Keep UI formatting and mutation separate while using one eligibility policy.

<a id="r03"></a>

### R03 — Vertex-channel source preflight

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:733](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L733); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:812](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L812); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1210](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1210); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1306](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1306).

Property-domain/source-type selection, SourceTypeAllowedForVertexChannel, resolver scratch accounting and EvaluateVertexChannelBinding are copied between action and snapshot code.

**Reuse:** Expose one internal preflight used by both paths, retaining the existing VertexAttributeBinding resolver. The command and UI must agree about types, domain, fallback and normalization.

<a id="r04"></a>

### R04 — Geometry metadata signatures

**Locations:** [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:818](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L818); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:978](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L978); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:2620](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L2620); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:2783](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L2783).

Byte/string mixing, property descriptor traversal and GeometryMetadataSignatureForEntity are implemented twice.

**Reuse:** Share the exact metadata-signature implementation inside runtime. Preserve the existing seed, ordering, domain tags and deleted-count handling; changing the hash during consolidation would change cache and stale-result behavior.

<a id="r05"></a>

### R05 — Transform undo adapter

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:809](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L809); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8056](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L8056).

SameTransformComponent, EditorTransformMutationIdentity and ExecuteEditorTransformMutation duplicate the same stable-entity validation, write and dirty-tag logic.

**Reuse:** Reuse one transform mutation adapter around the already existing Internal::ExecuteUndoableEntityMutation. No new history framework is needed; registration and scene edits should share the same transform transaction.

<a id="r06"></a>

### R06 — Render-hint undo adapter

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:539](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L539); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:628](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L628); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1121](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1121); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1336](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1336).

EditorRenderHintState, component comparison, ApplyRenderHintState and ExecuteEditorRenderHintMutation are copied.

**Reuse:** Move the shared render-hint state/transaction functions into runtime editor internals. Keep scene-specific commands and visualization-specific command construction at their current owners.

<a id="r07"></a>

### R07 — History-status conversion

**Locations:** [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:236](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L236); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8776](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L8776); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:741](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L741); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1695](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1695).

The same history-status to EditorCommandStatus switch appears in three translation units, with a further copy in the mesh operations file.

**Reuse:** Use one conversion in the common editor/history support surface. A new history status should require one mapping decision.

<a id="r08"></a>

### R08 — Scene-file and import result messages

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:393](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L393); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:439](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L439); [src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp:476](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp#L476); [src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp:556](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp#L556).

Import error wording and scene-file success/failure message builders are duplicated between direct actions and context adapters.

**Reuse:** Share the message builders in runtime editor internals so synchronous and queued completion messages stay consistent.

<a id="r09"></a>

### R09 — Job-to-editor model conversion

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1754](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1754); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1792](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1792).

ToEditorJobDependencyModel and ToEditorJobModel repeat the same job identity, dependency, progress and diagnostic projection.

**Reuse:** Use one runtime editor projection helper. UV regeneration can reuse it when locating a job; the workspace snapshot can reuse it for the full job list.

<a id="r10"></a>

### R10 — Repeated panel widgets

**Locations:** [src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp:130](../../src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp#L130); [src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp:90](../../src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp#L90); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:172](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L172).

DrawDiagnostics is repeated; the mesh and method panels also copy DrawDismissLastResultButton and DrawDomainWindowHeader.

**Reuse:** Keep a small shared Sandbox panel helper surface. In particular, one dismissal helper should clear both the local result and its session result slot.

<a id="r11"></a>

### R11 — Appearance command construction and bake UI helpers

**Locations:** [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:60](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L60); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:143](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L143); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:189](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L189); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:63](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L63); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:132](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L132); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:220](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L220).

The two files copy bake option tables/state views and visualization model-to-command construction. Uniform and scalar command constructors also repeat the field projection.

**Reuse:** Share the app-local tables and state views; create the base command from a model once, then override the fields an individual action changes. This reduces the chance that a new appearance field is reset or dropped by one panel.

<a id="r12"></a>

### R12 — Queued asset import lifecycle

**Locations:** [src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp:1983](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp#L1983); [src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp:2502](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp#L2502).

QueueGeometryImportWithIngest and QueueModelTextureImportWithIngest repeat submission identity, route/decode transitions, event recording, stale-target handling and unpublished-result finalization. Several long blocks match exactly.

**Reuse:** Extract the shared submission/transition/finalization steps inside this executor. Keep payload decoding and materialization explicit, including the different required services. Preserve cancellation and world/binding generations.

<a id="r13"></a>

### R13 — Queued scene IO bookkeeping

**Locations:** [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:808](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L808); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:935](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L935); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:1033](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L1033); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:1165](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L1165).

Queued save and load repeat binding capture, job bookkeeping, completion-event publication and FinalizeUnpublishedOnMainThread handling.

**Reuse:** Share the scene-file job identity and event/finalization helpers locally. Keep save snapshotting and load replacement transactions separate: their mutation semantics differ.

<a id="r14"></a>

### R14 — Point-cloud IO bypasses existing text helpers

**Locations:** [src/geometry/Geometry.PointCloud.IO.cpp:34](../../src/geometry/Geometry.PointCloud.IO.cpp#L34); [src/geometry/Geometry.IOText.hpp:22](../../src/geometry/Geometry.IOText.hpp#L22); [src/geometry/Geometry.Graph.IO.cpp:28](../../src/geometry/Geometry.Graph.IO.cpp#L28); [src/geometry/Geometry.HalfedgeMesh.IO.cpp:36](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L36).

PointCloud.IO independently implements PathInfo, MakePathInfo, ReadTextFile, Trim, NextLine, SplitWhitespace and ParseNumber. Graph and mesh IO already use Geometry::IOText.

**Reuse:** Use Geometry.IOText.hpp in point-cloud IO too, with a small adapter for its existing Core::ErrorCode return contract. This is an existing helper adoption, not a new parser framework.

<a id="r15"></a>

### R15 — PLY scalar/header machinery

**Locations:** [src/geometry/Geometry.HalfedgeMesh.IO.cpp:338](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L338); [src/geometry/Geometry.HalfedgeMesh.IO.cpp:442](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L442); [src/geometry/Geometry.PointCloud.IO.cpp:593](../../src/geometry/Geometry.PointCloud.IO.cpp#L593); [src/geometry/Geometry.PointCloud.IO.cpp:650](../../src/geometry/Geometry.PointCloud.IO.cpp#L650).

Both readers define PLY format/scalar enums, scalar-name parsing, byte-size lookup, endian swapping, property descriptions and binary scalar decoding.

**Reuse:** Share a geometry-owned internal PLY lexical/scalar layer. Preserve separate mesh-face construction, cloud publication, list-count validation and public diagnostics. A parsing correction should reach both importers.

<a id="r16"></a>

### R16 — ASCII and binary PCD export preamble

**Locations:** [src/geometry/Geometry.PointCloud.IO.cpp:2477](../../src/geometry/Geometry.PointCloud.IO.cpp#L2477); [src/geometry/Geometry.PointCloud.IO.cpp:2642](../../src/geometry/Geometry.PointCloud.IO.cpp#L2642).

WritePCD and WritePCDBinary repeat input/property validation and the PCD field, size, type, count, width and point-count header construction.

**Reuse:** Share the validated export view and PCD header writer with an explicit data encoding. Keep ASCII and binary row encoders distinct.

<a id="r17"></a>

### R17 — Ray/AABB slab intersection

**Locations:** [src/geometry/Geometry.ContactManifold.cpp:16](../../src/geometry/Geometry.ContactManifold.cpp#L16); [src/geometry/Geometry.Overlap.cpp:21](../../src/geometry/Geometry.Overlap.cpp#L21).

The two modules carry the same RayAabbSlabInterval implementation, including parallel-axis handling and interval accumulation.

**Reuse:** Place the interval primitive in an appropriate geometry-owned shared helper; keep overlap/contact result construction separate. Preserve zero-direction, tangent and negative-parameter behavior.

<a id="r18"></a>

### R18 — Curvature median and robust scale

**Locations:** [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp:114](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp#L114); [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp:193](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp#L193); [src/geometry/Geometry.Statistics.cppm:75](../../src/geometry/Geometry.Statistics.cppm#L75).

Segmentation and patches each implement Median and the same MAD-to-RMS-to-one RobustScale fallback. Geometry::Statistics already exposes an exact median.

**Reuse:** Share the robust-scale policy. Reuse the statistics median for validated finite inputs, explicitly adapting empty-input behavior; its non-finite filtering contract must not silently replace a different method policy.

<a id="r19"></a>

### R19 — Surface-index fingerprint contract

**Locations:** [src/graphics/renderer/Graphics.GpuWorld.cpp:37](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L37); [src/graphics/renderer/Graphics.GpuWorld.cpp:72](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L72); [src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp:75](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp#L75); [src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp:1764](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp#L1764).

GpuWorld::FingerprintUint32Stream and texture baking's FingerprintIndices independently implement the same little-endian FNV-1a word stream. The bake path directly compares their results to accept GPU atlas residency.

**Reuse:** Give this fingerprint one implementation in a layer both consumers can legally use, retaining the seed, byte order and zero-to-one mapping. This is a shared identity contract, not merely similar hashing code.

<a id="r20"></a>

### R20 — Geometry upload channel setup

**Locations:** [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp:174](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp#L174); [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.PointCloud.cpp:126](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.PointCloud.cpp#L126); [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp:215](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp#L215).

Graph and point-cloud builders duplicate normal/color binding and channel publication; mesh packing repeats the explicit color binding path.

**Reuse:** Share the canonical-domain normal/color channel preparation around existing ResolveVec3Channel and ResolveColorChannelPackedUnorm8. Leave primitive topology, mesh corner expansion and mesh default-color policy at their owners.

<a id="r21"></a>

### R21 — Packed transient/overlay upload allocation

**Locations:** [src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp:114](../../src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp#L114); [src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp:153](../../src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp#L153).

Both upload helpers contain the same lease/capacity growth, cap checking, host-visible BDA buffer allocation and upload operation under different packed vertex types. The overlay implementation explicitly describes itself as a copy of the transient helper.

**Reuse:** Share the packed-buffer upload routine inside graphics with an explicit stride or small typed helper. Keep packet expansion, lane limits and frame-slot ownership separate; retain in-flight resource lifetimes.

<a id="r22"></a>

### R22 — GPU surface bucket draw recording

**Locations:** [src/graphics/renderer/Passes/Pass.Selection.EntityId.cpp:21](../../src/graphics/renderer/Passes/Pass.Selection.EntityId.cpp#L21); [src/graphics/renderer/Passes/Pass.Selection.FaceId.cpp:21](../../src/graphics/renderer/Passes/Pass.Selection.FaceId.cpp#L21); [src/graphics/renderer/Passes/Pass.Deferred.GBuffers.cpp:23](../../src/graphics/renderer/Passes/Pass.Deferred.GBuffers.cpp#L23); [src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:23](../../src/graphics/renderer/Passes/Pass.Forward.Surface.cpp#L23); [src/graphics/renderer/Passes/Pass.DepthPrepass.cpp:24](../../src/graphics/renderer/Passes/Pass.DepthPrepass.cpp#L24).

The passes repeat bucket validity checks, managed index-buffer binding, GpuScenePushConstants construction and DrawIndexedIndirectCount.

**Reuse:** Use a graphics-local draw-recording helper with pipeline, bucket and frame inputs. Keep each pass's eligibility, attachments, shader ABI and recipe identity explicit.

<a id="r23"></a>

### R23 — Transient memory placement algorithm

**Locations:** [src/graphics/framegraph/Graphics.RenderGraph.cpp:133](../../src/graphics/framegraph/Graphics.RenderGraph.cpp#L133); [src/graphics/renderer/Graphics.Renderer.cpp:1083](../../src/graphics/renderer/Graphics.Renderer.cpp#L1083).

BuildTransientPlacementPlan and BuildRendererTransientPlacementPlan independently implement active-range expiry, sorted free ranges, aligned first-fit placement, splitting and alias-reuse tracking.

**Reuse:** Extract the common pure placement kernel. The framegraph supplies estimated requirements; the renderer supplies device requirements and retains memory-type/dedicated-allocation checks. Do not reuse estimated sizes as device allocation sizes.

<a id="r24"></a>

### R24 — Initial and recreated swapchain construction

**Locations:** [src/graphics/vulkan/Backends.Vulkan.Device.cpp:1112](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp#L1112); [src/graphics/vulkan/Backends.Vulkan.Device.cpp:1962](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp#L1962).

Initialize repeats swapchain create-info construction, image-count/usage policy and image/view setup already implemented by CreateSwapchainResources.

**Reuse:** Have initialization and recreation use one backend-local swapchain builder, with explicit initial/old-swapchain inputs. Preserve initialization diagnostics, transactional adoption and cleanup on failure.

<a id="r25"></a>

### R25 — Transfer and readback submission

**Locations:** [src/graphics/vulkan/Backends.Vulkan.Transfer.cpp:168](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp#L168); [src/graphics/vulkan/Backends.Vulkan.Transfer.cpp:230](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp#L230).

Submit and SubmitReadback duplicate command finalization, timeline-ticket allocation, semaphore submit records, queue submission and failure cleanup.

**Reuse:** Share the private submission primitive returning the ticket/result. Keep upload/readback-specific token and sink bookkeeping separate, including lock scope and readback slot ownership.

<a id="r26"></a>

### R26 — Debug/overlay pipeline descriptor defaults

**Locations:** [src/graphics/renderer/Graphics.Renderer.cpp:6026](../../src/graphics/renderer/Graphics.Renderer.cpp#L6026); [src/graphics/renderer/Graphics.Renderer.cpp:6067](../../src/graphics/renderer/Graphics.Renderer.cpp#L6067); [src/graphics/renderer/Graphics.Renderer.cpp:6094](../../src/graphics/renderer/Graphics.Renderer.cpp#L6094); [src/graphics/renderer/Graphics.Renderer.cpp:6144](../../src/graphics/renderer/Graphics.Renderer.cpp#L6144); [src/graphics/renderer/Graphics.Renderer.cpp:6183](../../src/graphics/renderer/Graphics.Renderer.cpp#L6183).

The transient triangle/line/point and visualization vector/isoline factories repeat the same rasterizer, depth, blend and target-format settings.

**Reuse:** Use a small common descriptor initializer, then set each shader pair, topology, push-constant size and name explicitly. Existing initialization/rebuild factory reuse should remain intact.

<a id="r27"></a>

### R27 — No-op command contexts

**Locations:** [src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp:45](../../src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp#L45); [src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp:217](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp#L217); [src/graphics/renderer/Backends/Null/Backends.Null.cpp:113](../../src/graphics/renderer/Backends/Null/Backends.Null.cpp#L113).

Two runtime compute paths copy a complete NoopCommandContext implementation; the Null renderer backend has the same underlying no-op command surface.

**Reuse:** Share the two identical runtime helpers or expose an appropriate existing Null command-context seam. Do not replace recording test doubles or real Vulkan contexts; their observable behavior is different.

<a id="r28"></a>

### R28 — Two copies of the culling shader

**Locations:** [assets/shaders/instance_cull.comp:1](../../assets/shaders/instance_cull.comp#L1); [assets/shaders/culling/instance_cull.comp:1](../../assets/shaders/culling/instance_cull.comp#L1); [src/graphics/renderer/Graphics.Renderer.cpp:6344](../../src/graphics/renderer/Graphics.Renderer.cpp#L6344); [tests/integration/graphics/Test.GpuWorldAndCulling.cpp:378](../../tests/integration/graphics/Test.GpuWorldAndCulling.cpp#L378).

The shader sources are byte-identical (confirmed with cmp). Production loads the root shader path while multiple tests use the culling/ path; a renderer test even reads both sources.

**Reuse:** Use one canonical shader source and update consumers, or retain thin include-based entry points if both asset paths are required. Both paths must compile the same implementation.

<a id="r29"></a>

### R29 — Promoted surface material sampling

**Locations:** [assets/shaders/forward/default_debug_surface.frag:48](../../assets/shaders/forward/default_debug_surface.frag#L48); [assets/shaders/deferred/default_debug_gbuffer.frag:56](../../assets/shaders/deferred/default_debug_gbuffer.frag#L56); [assets/shaders/deferred/gbuffer.frag:40](../../assets/shaders/deferred/gbuffer.frag#L40).

Texture-ID validation, normal-texture decoding/space handling and albedo/visualization resolution are independently maintained in forward and deferred fragment shaders; deferred variants also repeat metallic/roughness sampling.

**Reuse:** Move matching material sampling functions into common GLSL includes. Preserve each entry point's outputs and push-constant layout; the existing property_texture_normal.glsl include demonstrates the available sharing mechanism.

<a id="r30"></a>

### R30 — Progressive-Poisson GPU layout and cell encoding

**Locations:** [assets/shaders/progressive_poisson_accept_phase.comp:37](../../assets/shaders/progressive_poisson_accept_phase.comp#L37); [assets/shaders/progressive_poisson_build_cells.comp:39](../../assets/shaders/progressive_poisson_build_cells.comp#L39).

Both shaders repeat the state BDA record, push-constant layout and packCell2D/packCell3D bit encodings.

**Reuse:** Use a method-local shared GLSL include for the exact ABI and cell encoding. Keep acceptance and cell-building algorithms distinct, and preserve CPU/GPU layout checks.

<a id="r31"></a>

### R31 — K-means GPU layout declarations

**Locations:** [assets/shaders/kmeans_assign.comp:9](../../assets/shaders/kmeans_assign.comp#L9); [assets/shaders/kmeans_reset.comp:24](../../assets/shaders/kmeans_reset.comp#L24); [assets/shaders/kmeans_update.comp:26](../../assets/shaders/kmeans_update.comp#L26).

KMeansStateRef, KMeansPush and matching reduction declarations are copied across the compute stages.

**Reuse:** Share the method-local GLSL ABI declarations, including the new NodesBDA field. Keep stage-specific buffer access qualifiers and execution bodies explicit.

<a id="r32"></a>

### R32 — Point/surfel EWA math

**Locations:** [assets/shaders/point.vert:91](../../assets/shaders/point.vert#L91); [assets/shaders/point_retained.vert:106](../../assets/shaders/point_retained.vert#L106); [assets/shaders/point_surfel.vert:118](../../assets/shaders/point_surfel.vert#L118); [assets/shaders/point.frag:29](../../assets/shaders/point.frag#L29); [assets/shaders/point_retained.frag:39](../../assets/shaders/point_retained.frag#L39).

The point shader family repeats tangent-frame projection, covariance construction, EWA evaluation and guarded lighting.

**Reuse:** Share the matching mathematical pieces as GLSL functions taking values. Retain distinct attribute sources, per-point radius/color policy and push-constant ABIs; consolidate only after comparing covariance floors and other numerical details.

<a id="r33"></a>

### R33 — Curvature profile measurement helpers

**Locations:** [benchmarks/runners/CurvaturePatchProfileRunner.cpp:191](../../benchmarks/runners/CurvaturePatchProfileRunner.cpp#L191); [benchmarks/runners/CurvatureSegmentationProfileRunner.cpp:352](../../benchmarks/runners/CurvatureSegmentationProfileRunner.cpp#L352).

The runners copy point-to-segment distance, tube intersection, boundary geometry collection and boundary-quality evaluation. Multiple long exact matches extend beyond the initial helper.

**Reuse:** Use a benchmark-owned common boundary evaluator and reporting helpers. Keep this oracle independent from the segmentation/patch implementation under evaluation.

<a id="r34"></a>

### R34 — Smoke result JSON envelope

**Locations:** [benchmarks/runners/BenchmarkSmokeRunner.cpp:564](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L564); [benchmarks/runners/BenchmarkSmokeRunner.cpp:605](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L605); [benchmarks/runners/BenchmarkSmokeRunner.cpp:1079](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L1079).

Each emitter reconstructs benchmark/method/backend/dataset/commit metadata, metrics/diagnostic object wrappers and pass/fail output. Existing emitters already differ in formatting and serialization approach.

**Reuse:** Create one envelope/serialization function around explicit per-benchmark metadata, metrics and diagnostics. Preserve each run's precision and measured settings; do not turn smoke output into a stronger evidence claim.

<a id="r35"></a>

### R35 — CI toolchain and vcpkg setup

**Locations:** [.github/workflows/ci-linux-clang.yml:264](../../.github/workflows/ci-linux-clang.yml#L264); [.github/workflows/ci-sanitizers.yml:24](../../.github/workflows/ci-sanitizers.yml#L24); [.github/workflows/ci-source-coverage.yml:34](../../.github/workflows/ci-source-coverage.yml#L34); [.github/workflows/ci-vulkan.yml:56](../../.github/workflows/ci-vulkan.yml#L56); [.github/workflows/ci-release.yml:104](../../.github/workflows/ci-release.yml#L104); [.github/workflows/nightly-deep.yml:18](../../.github/workflows/nightly-deep.yml#L18).

Workflows repeatedly specify the base apt packages, vcpkg binary-cache key/location, bootstrap and VCPKG_BINARY_SOURCES setup. Bootstrap itself already uses the shared script.

**Reuse:** Share just the common setup in a local composite action or setup helper with explicit capability additions. Keep separate CPU/sanitizer/Vulkan jobs, selectors, privileges and concurrency budgets.

<a id="r36"></a>

### R36 — Graphics inspection and setup helpers

**Locations:** [tests/contract/graphics/Test.TransientDebugSurfacePass.cpp:55](../../tests/contract/graphics/Test.TransientDebugSurfacePass.cpp#L55); [tests/contract/graphics/Test.VisualizationOverlayPass.cpp:58](../../tests/contract/graphics/Test.VisualizationOverlayPass.cpp#L58); [tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp:114](../../tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp#L114); [tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp:87](../../tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp#L87).

Tests repeat command-pass lookup, barrier inspection, ReorderToRgba and SrgbToLinearPixel, plus portions of smoke readback scaffolding. MockRHI.hpp and MinimalTriangleReadback.hpp are already shared.

**Reuse:** Move stable inspection/setup helpers into tests/support. Keep the assertions, expected pixels and pass-specific packet creation visible in each test so the oracle remains readable and independent.

<a id="r37"></a>

### R37 — Editor geometry fixtures

**Locations:** [tests/contract/runtime/Test.SandboxEditorModels.cpp:371](../../tests/contract/runtime/Test.SandboxEditorModels.cpp#L371); [tests/contract/runtime/Test.SandboxEditorVisualization.cpp:221](../../tests/contract/runtime/Test.SandboxEditorVisualization.cpp#L221); [tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp:279](../../tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp#L279); [tests/support/EditorFeatureTestContext.hpp:1](../../tests/support/EditorFeatureTestContext.hpp#L1).

Several editor suites copy low-level property/topology fixture construction such as SetFloatProperty, SetEdges and SetHalfedges even though a shared editor test-support location exists.

**Reuse:** Share the repeated fixture constructors in tests/support, preserving explicit test-specific property values and expected outcomes. Avoid deriving expected results through production code.

<a id="r38"></a>

### R38 — Curvature parameter validity rules

**Locations:** [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp:89](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp#L89); [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp:142](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp#L142); [src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp:49](../../src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp#L49).

Geometry and runtime config validation repeat the mixture-count, tolerance, covariance-floor and related numerical bounds. Runtime adds method/UI-specific bounds.

**Reuse:** Expose or reuse a pure geometry parameter validator after config-to-parameter conversion, then apply runtime-specific rules separately. Preserve the selected method's own bounds; superficially similar patch and feature controls are not automatically interchangeable.

## Further candidates requiring a scope decision

These source similarities are real, but the audit does not classify them as unconditional consolidation work.

| Candidate | Evidence | What must be established first |
| --- | --- | --- |
| BVH/KD-tree median-split construction | [BVH:95](../../src/geometry/Geometry.BVH.cpp#L95), [KDTree:87](../../src/geometry/Geometry.KDTree.cpp#L87) | A small shared split/partition primitive may help. Their point-versus-AABB bounds and query contracts do not justify merging the indices into a generic tree framework. |
| General 64-bit hash helpers | [IOBackend:18](../../src/core/Core.IOBackend.cpp#L18), [TaskGraph:56](../../src/core/Core.Dag.TaskGraph.cppm#L56), [FrameGraph:54](../../src/core/Core.FrameGraph.cppm#L54), [GpuWorld:34](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L34), [procedural geometry:20](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Procedural.cpp#L20), [parameterization:92](../../src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp#L92), [recipe policies:68](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp#L68) | Share byte/word mixing only after documenting seeds, framing, floating-point normalization and token stability. Existing Core.Hash is 32-bit, so it is not a drop-in replacement. R19 is the narrower confirmed identity contract. |
| Saturating arithmetic and alignment | [SupportRadius:105](../../src/geometry/Geometry.SupportRadius.cpp#L105), [consolidation:379](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp#L379), [LinearArena:26](../../src/core/Core.Memory.LinearArena.cpp#L26), [GpuWorld:83](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L83) | Tiny helpers may merit a common core primitive if enough callers need the exact same overflow, zero-alignment and failure contract. Do not hide different policies behind a generic utility. |
| Retained history texture pairs | [HZB:207](../../src/graphics/renderer/Graphics.HZB.cpp#L207), [reconstruction:347](../../src/graphics/renderer/Graphics.Reconstruction.cpp#L347) | Common allocation/retirement details exist, but ownership and initialization semantics must be compared before choosing the smallest helper. |
| Mesh export traversal | [HalfedgeMesh.IO:2427](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L2427), [HalfedgeMesh.IO:2644](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L2644) | As with R16, share validated geometry/header pieces only after preserving per-format normal/color/topology rules. |
| Repeated geometry operation job scaffolding | [mesh operations:5889](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L5889), [mesh operations:7347](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L7347) | Extract a stable repeated snapshot/publication operation only where the same stale-source and output policy applies. A generic method service would be an unjustified expansion. |
| Offline thickness CLI wrapper | [refine_thickness_parts.py:91](../../tools/diagnostics/curvature/refine_thickness_parts.py#L91), [select_thickness_parts.py:112](../../tools/diagnostics/curvature/select_thickness_parts.py#L112) | Common input/provenance/output code exists, but these are bounded experiment entry points. Consolidate if they become maintained workflows, retaining per-script source provenance. |
| Shared spatial-index adoption | [consumer inventory](../architecture/spatial-index-consumers.md), [SpatialIndexCache](../../src/runtime/GeometryIntegration/Runtime.SpatialIndexCache.cppm) | Remaining point-analysis callers may benefit from shared index ownership. This requires workload evidence and exact neighborhood semantics; it is not a mandate to replace every KD-tree, octree, grid or brute-force oracle. |

## Similar code that should remain distinct

- **Backend implementations and recording doubles:** Null/Vulkan and Null/GLFW are supported implementation seams. They are not duplicate features to delete. R27 concerns identical no-op helpers only.
- **Independent numerical oracles:** CPU references and exhaustive benchmark truth must remain independent of the optimized/GPU kernel being checked. R33 shares the measurement code across runners, not with the algorithm under test.
- **Graph and point-cloud Gaussian noise:** [graph](../../src/geometry/Geometry.Graph.Utils.cpp#L1456) scales by bounding-box diagonal; [point cloud](../../src/geometry/Geometry.PointCloud.Utils.cpp#L969) scales by average spacing. Both already call `Geometry::Sampling::GaussianDisplacement`. Merging their complete operations would change semantics.
- **DEC and sparse solver declarations:** [DEC](../../src/geometry/Geometry.HalfedgeMesh.DEC.cppm#L269) exposes aliases/forwarding over the sparse solver contract. Similar declarations alone do not establish duplicated solver implementations.
- **LOP grid shader stages:** [count](../../assets/shaders/lop_grid_count.comp) and [scatter](../../assets/shaders/lop_grid_scatter.comp) already include `lop_gpu_common.glslinc`. Their counting versus indexed-scatter writes are different operations.
- **Distinct geometry domains and result records:** repeated fields in DTOs, const/mutable views, graph/mesh containers and exported pass APIs are not sufficient evidence that a new inheritance hierarchy or template framework would improve them.
- **Different geometric queries:** point-nearest, nearest triangle, distance to a ray, topology-geodesic distance and descriptor-space matching cannot share an index merely because each searches for something close.
- **Test assertions and scenario setup:** similarity alone does not justify parameterizing every test. Share stable fixtures and inspection routines while keeping expected behavior legible.

## Suggested order

1. Start with **R14**: point-cloud IO can adopt an existing helper, with a small and clear ownership boundary.
2. Address **R01–R03**, then **R04–R06**: these unify editor rules and mutations used by current product workflows.
3. Address **R19** and **R28–R31**: shared identity and shader ABI definitions reduce the chance of paired implementations drifting.
4. Take resource-lifecycle work (**R12, R21, R23–R25**) in separate reviewed slices with focused lifetime/failure tests.
5. Apply lower-risk panel, output and fixture consolidation when those areas are next touched.

The numbered entries are a reviewable follow-up inventory, not authorization for a bulk refactor. Each implementation should preserve current behavior, remain within its owning layer, and run the relevant focused CPU tests; rendering/ABI changes additionally need their applicable Vulkan coverage.
