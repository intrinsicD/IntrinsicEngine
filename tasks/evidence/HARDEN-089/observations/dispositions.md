# HARDEN-089 audit-window dispositions

The coordinates below refer to the immutable audit window
`51e7faddad943ab7727e407d008e474ec076566d..3ee6a343d13c54dbd255e15ce254aa812d6dc194`,
not to post-cleanup line numbers. The raw source-root search also finds
`src/platform/CMakeLists.txt:52`; that build-input comment is deliberately
excluded by the task's documentation-only boundary. The remaining 98 lines are
reviewed individually here.

| # | Audit-window source line | Disposition | Rationale |
| ---: | --- | --- | --- |
| 1 | `src/app/Sandbox/README.md:136` | Keep | METHOD-037 is the current operation identity, not task chronology. |
| 2 | `src/app/Sandbox/README.md:188` | Rewrite | Replace the bare BUG-137 pointer with the current topology/duplication contract. |
| 3 | `src/ecs/Components/ECS.Component.GeometrySources.cppm:37` | Keep | METHOD-039 identifies the current canonical property catalog entry. |
| 4 | `src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cppm:244` | Keep | METHOD-039 is the current diagnostic method identity. |
| 5 | `src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cppm:248` | Keep | METHOD-037 identifies the current production default after the invariant. |
| 6 | `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp:659` | Rewrite | Remove the task-first prefix; retain the UV-seam storage-slot contract. |
| 7 | `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp:701` | Rewrite | Remove historical provenance; retain the boundary-halfedge seam guard. |
| 8 | `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp:776` | Rewrite | State the NaN boundary-only rule directly without pre-task chronology. |
| 9 | `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm:163` | Rewrite | Remove task provenance; retain the domain-dependent seam contract. |
| 10 | `src/geometry/Geometry.HalfedgeMesh.Utils.cpp:425` | Rewrite | Replace the bug-labelled divider with its current domain-resolution subject. |
| 11 | `src/geometry/Geometry.HalfedgeMesh.Utils.cppm:23` | Rewrite | Remove the task-first prefix; retain the canonical corner-property rationale. |
| 12 | `src/geometry/Geometry.HalfedgeMesh.Utils.cppm:42` | Rewrite | Remove the task-first prefix; retain corner-over-vertex resolution. |
| 13 | `src/graphics/vulkan/README.md:459` | Rewrite | State the exact accepted libX11 suppression and place BUG-118 after the rule. |
| 14 | `src/graphics/vulkan/README.md:688` | Keep | GRAPHICS-119 is durable provenance after the current serial/parallel fixture fact. |
| 15 | `src/platform/README.md:107` | Rewrite | State that no ImGui backend is initialized and put BUG-139 after the invariant. |
| 16 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:264` | Rewrite | Remove task/“now” wording; retain canonical UV-domain validation. |
| 17 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:450` | Rewrite | Describe the shared atlas-to-corner seam as current ownership. |
| 18 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:451` | Rewrite | Remove BUG-147 chronology from the same shared-ownership block. |
| 19 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:475` | Rewrite | Remove the task-first prefix; retain native OBJ corner-UV preservation. |
| 20 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:769` | Rewrite | Remove the task-first prefix; retain authored corner-UV preservation. |
| 21 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:809` | Rewrite | State source-topology publication and GPU-only seam splitting directly. |
| 22 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cppm:48` | Rewrite | Remove history; define the current corner-domain diagnostic flag. |
| 23 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cppm:58` | Rewrite | Define GPU split count without former-field or former-ECS narration. |
| 24 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cppm:81` | Rewrite | Remove the task-first prefix; retain the GPU-only duplication counter. |
| 25 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp:637` | Rewrite | State why the enrichment diagnostic exposes topology and duplication facts. |
| 26 | `src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp:1017` | Rewrite | Remove task/“now” wording; retain source topology and GPU duplication reporting. |
| 27 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:697` | Rewrite | Remove the task-first prefix; retain source-topology UV publication. |
| 28 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:881` | Rewrite | Replace BUG-138 history with the stored-array topology-signature invariant. |
| 29 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:1055` | Rewrite | Replace BUG-096 history with fail-closed point-to-plane normal preflight. |
| 30 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:1443` | Rewrite | Remove the task-first prefix; retain the exact changed-value count. |
| 31 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:1464` | Rewrite | Remove the task-first prefix; retain written-versus-changed rationale. |
| 32 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:3742` | Rewrite | Replace old/new comparison history with current vertex/topology split ownership. |
| 33 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:3827` | Rewrite | Remove task provenance; retain stored pre-apply topology capture. |
| 34 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:3942` | Rewrite | Remove task provenance; retain post-apply stored topology capture. |
| 35 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:4257` | Rewrite | Remove the task-first prefix; retain full replacement-mesh comparison. |
| 36 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:4301` | Rewrite | Remove the task-first prefix; retain UV-inclusive no-change comparison. |
| 37 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:4313` | Rewrite | Remove task/“now” wording; retain domain-aware UV comparison. |
| 38 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:4395` | Rewrite | Remove the task-first prefix; retain user-visible UV-loss diagnostics. |
| 39 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:4661` | Rewrite | State `RejectedIndices` as the current outlier-removal change signal. |
| 40 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:6477` | Rewrite | Remove the task-first prefix; retain scratch-mesh corner-UV forwarding. |
| 41 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:6489` | Rewrite | Remove BUG-147 provenance; retain the no-op comparison rationale. |
| 42 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:6562` | Rewrite | Remove the task-first prefix; retain canonical UV-presence resolution. |
| 43 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:6638` | Rewrite | Remove BUG-138 provenance; retain the submit-time fingerprint meaning. |
| 44 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:7436` | Rewrite | Remove the task-first prefix; retain why subdivision needs no no-change gate. |
| 45 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:7538` | Rewrite | Remove BUG-138 history; retain terminal unpublished-job reconciliation. |
| 46 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:7822` | Rewrite | Remove the task-first prefix; retain stored-to-stored topology fingerprinting. |
| 47 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:7887` | Rewrite | Remove the task-first prefix; retain stored-to-stored topology fingerprinting. |
| 48 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:7955` | Rewrite | Remove the task-first prefix; retain stored-to-stored topology fingerprinting. |
| 49 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8079` | Rewrite | Remove the task-first prefix; retain the pre-solver effective-variant meaning. |
| 50 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8213` | Rewrite | Remove the task-first prefix; retain normal snapshot/staleness semantics. |
| 51 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8290` | Rewrite | Remove the task-first prefix; retain point-to-plane normal staleness. |
| 52 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:9584` | Rewrite | Replace BUG-147/history narration with source-soup and corner-UV ownership. |
| 53 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:9884` | Rewrite | Remove the task-first prefix; retain before-state corner-UV capture. |
| 54 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:10867` | Rewrite | Remove the task-first prefix; retain explicit UV discard on new topology. |
| 55 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:11075` | Rewrite | Remove the task-first prefix; retain explicit UV discard on new topology. |
| 56 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:11175` | Rewrite | Remove the task-first prefix; retain subdivision's guaranteed topology change. |
| 57 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:11271` | Rewrite | Remove BUG-146 provenance; retain simplification UV preservation. |
| 58 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:12241` | Rewrite | Remove the task-first prefix; retain pre-dispatch normal validation. |
| 59 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:525` | Rewrite | Remove the task-first prefix; retain topology-operation UV outcome contract. |
| 60 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:675` | Rewrite | Replace BUG-096 history with requested/effective variant semantics. |
| 61 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:721` | Rewrite | Remove the task-first prefix; retain changed-normal counter meaning. |
| 62 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:762` | Rewrite | Replace the task pointer with a direct changed-normal contract. |
| 63 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:814` | Rewrite | Replace the task pointer with a direct changed-normal contract. |
| 64 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:902` | Rewrite | Remove the task-first prefix; retain per-operation result-slot lifetime. |
| 65 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:1153` | Rewrite | Remove history; define GPU seam duplication without mesh-topology implication. |
| 66 | `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:1192` | Rewrite | Remove the task-first prefix; retain structured rejection ownership. |
| 67 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:55` | Rewrite | Remove the task-first prefix; retain canonical UV-domain publication. |
| 68 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:766` | Rewrite | Remove the task-first prefix; retain exact dual-domain history restoration. |
| 69 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:1010` | Rewrite | Replace BUG-141 history with the actionable rejection-message contract. |
| 70 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:1251` | Rewrite | Remove the task-first prefix; retain vertex publication/corner retirement. |
| 71 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:1519` | Rewrite | Remove history; state the corner-domain UV view limitation and action. |
| 72 | `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp:1563` | Rewrite | Replace duplicate-message history with current header/result ownership. |
| 73 | `src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:451` | Rewrite | Remove the task-first prefix; retain reserved corner-domain property meaning. |
| 74 | `src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:474` | Rewrite | Remove the task-first prefix; retain reserved corner-domain property meaning. |
| 75 | `src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1929` | Rewrite | Remove history; retain canonical corner-over-vertex UV discovery. |
| 76 | `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:571` | Rewrite | Remove the task-first prefix; retain reserved corner-domain property meaning. |
| 77 | `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:594` | Rewrite | Remove the task-first prefix; retain reserved corner-domain property meaning. |
| 78 | `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:3325` | Rewrite | Replace BUG-141 history with current shared-versus-operation diagnostic ownership. |
| 79 | `src/runtime/GeometryIntegration/Runtime.MeshSurfaceTopology.cppm:152` | Rewrite | Remove task provenance; retain source-mesh corner-UV mapping contract. |
| 80 | `src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp:24` | Keep | The UI/debug literal truthfully names the selected METHOD-037 implementation. |
| 81 | `src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp:26` | Keep | The UI/debug literal truthfully names the selected METHOD-039 implementation. |
| 82 | `src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cppm:44` | Keep | Method IDs distinguish the operational default from the diagnostic candidate. |
| 83 | `src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cppm:67` | Keep | METHOD-039 identifies which current controls the declaration documents. |
| 84 | `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp:858` | Rewrite | Remove history; retain canonical corner-over-vertex texture-bake input. |
| 85 | `src/runtime/README.md:62` | Keep | Durable implementation-task provenance follows a complete current module contract. |
| 86 | `src/runtime/README.md:80` | Rewrite | Remove “since BUG-139”; state current key translation directly. |
| 87 | `src/runtime/README.md:84` | Keep | BUG-138 is durable evidence after the current terminal-job invariant. |
| 88 | `src/runtime/README.md:442` | Rewrite | Replace BUG-140/145 chronology with the current changed-count rule. |
| 89 | `src/runtime/README.md:664` | Rewrite | State METHOD-037's current Curvature-window availability directly. |
| 90 | `src/runtime/README.md:665` | Rewrite | Replace BUG-163 chronology with METHOD-039's current diagnostic status. |
| 91 | `src/runtime/README.md:687` | Keep | METHOD-039 is the current selected-method identity for the described properties. |
| 92 | `src/runtime/README.md:691` | Rewrite | Link GEOM-076 explicitly as a planned evidence-gated follow-up. |
| 93 | `src/runtime/README.md:791` | Rewrite | Remove BUG-096 from the heading and state current fail-closed prerequisites. |
| 94 | `src/runtime/README.md:917` | Rewrite | Replace BUG-141/fix history with current diagnostic scope and lifetime. |
| 95 | `src/runtime/Rendering/Runtime.RenderExtraction.Geometry.cpp:130` | Rewrite | Remove history; retain domain-aware texcoord revision tracking. |
| 96 | `src/runtime/Rendering/Runtime.RenderExtraction.cpp:705` | Rewrite | Remove task/slice history; retain corner-domain render eligibility. |
| 97 | `src/runtime/Scene/Runtime.SceneSerialization.cpp:1549` | Rewrite | Remove history; retain corner-domain UV persistence on scene round-trip. |
| 98 | `src/runtime/Visualization/Runtime.VisualizationRecipes.cpp:1139` | Rewrite | Remove history; retain domain-aware texture-bake dirty tracking. |

Summary: 12 keep, 86 rewrite, 0 drop. Every kept identifier names a current
method or durable implementation/evidence record after the current contract;
every task-first, chronological, or future-owner line is rewritten.

## Post-edit reconciliation

- The focused five-interface scan reports zero objective errors. Its one
  remaining `comment-history` review prompt is
  `Geometry.HalfedgeMesh.CurvatureSegmentation.cppm:89`, outside the 98-line
  task-ID inventory; the comment states the current units and deterministic
  normalization counter for `FitMilliseconds`, so it remains unchanged.
- The targeted `Runtime.GeometryProcessingOperations.Mesh.cpp` and runtime
  README scan reports no `comment-history` findings. The remaining README
  chronology prompts are inherited outside the fixed findings and remain
  report-only.
- The diff removes exactly 86 audit-window identifier lines, matching the 86
  Rewrite rows above. The 12 Keep rows are byte-for-byte unchanged.
