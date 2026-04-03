// Runtime.EditorUI.InspectorController — Component property inspector panel.
//
// Shows ALL ECS components on the selected entity under CollapsingHeaders.
// Sections: Name, Transform, Surface, Mesh Data (with PropertySets, spectral
// modes, analysis), Graph, Point Cloud, Wireframe (Line), Vertex Display
// (Point), Geometry Processing (algorithms as CollapsingHeaders), MeshCollider,
// Hierarchy, BVH, KDTree, DEC, AxisRotator, AssetSourceRef, Selection Info,
// Dirty Tags.

module;

#include <cstring>
#include <string>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <entt/entity/registry.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Core.Commands;
import Graphics.Components;
import Graphics.Geometry;
import Geometry.MeshAnalysis;
import Geometry.MeshUtils;
import ECS;
import Interface;

namespace Runtime::EditorUI
{

// =========================================================================
// Helpers
// =========================================================================

namespace
{
    // Read-only entity ID display.
    void EntityIdText(const char* label, entt::entity e)
    {
        if (e == entt::null)
            ImGui::Text("%s: (null)", label);
        else
            ImGui::Text("%s: %u", label, static_cast<uint32_t>(static_cast<entt::id_type>(e)));
    }

    // A read-only badge for presence of a zero-size tag component.
    void TagBadge(const char* name, bool present)
    {
        if (present)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[%s]", name);
        }
    }

    template <typename T, typename Fn>
    void ApplyComponentCommand(Runtime::Engine& engine,
                               entt::registry& registry,
                               entt::entity entity,
                               const char* commandName,
                               Fn&& editFn)
    {
        auto* component = registry.try_get<T>(entity);
        if (!component)
            return;

        T before = *component;
        if (!editFn(*component))
            return;

        T after = *component;
        auto cmd = Core::MakeComponentChangeCommand<T>(
            commandName,
            &registry,
            entity,
            std::move(before),
            std::move(after));
        (void)engine.GetCommandHistory().Execute(std::move(cmd));
    }
}

// =========================================================================
// Init
// =========================================================================

void InspectorController::Init(Runtime::Engine& engine,
                               entt::entity& cachedSelected,
                               GeometryWorkflowController* geometryWorkflow)
{
    m_Engine = &engine;
    m_CachedSelected = &cachedSelected;
    m_GeometryWorkflow = geometryWorkflow;
}

// =========================================================================
// Draw — the main inspector loop
// =========================================================================

void InspectorController::Draw()
{
    ImGui::Begin("Inspector");

    const entt::entity selected = *m_CachedSelected;

    if (selected == entt::null || !m_Engine->GetSceneManager().GetScene().GetRegistry().valid(selected))
    {
        ImGui::TextDisabled("Select an entity to inspect.");
        ImGui::End();
        return;
    }

    auto& reg = m_Engine->GetSceneManager().GetScene().GetRegistry();

    // === Reset per-entity widget state on selection change ===
    if (selected != m_PreviousSelected)
    {
        m_PreviousSelected = selected;
        m_MeshVertexPropertiesUi = {};
        m_MeshEdgePropertiesUi = {};
        m_MeshHalfedgePropertiesUi = {};
        m_MeshFacePropertiesUi = {};
        m_GraphVertexPropertiesUi = {};
        m_GraphEdgePropertiesUi = {};
        m_GraphHalfedgePropertiesUi = {};
        m_PointCloudPropertiesUi = {};
        m_MeshSpectralUi = {};
        m_GraphSpectralUi = {};
        m_KMeansUi = {};
        m_RemeshingUi = {};
        m_SimplificationUi = {};
        m_SmoothingUi = {};
        m_SubdivisionUi = {};
        m_MeshAnalysisUi = {};
        m_NormalEstimationUi = {};
        m_ShortestPathUi = {};
        m_ConvexHullUi = {};
        m_SurfaceReconstructionUi = {};
    }

    // === Entity ID (always shown at top) ===
    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(static_cast<entt::id_type>(selected)));

    // === Name Tag ===
    if (auto* tag = reg.try_get<ECS::Components::NameTag::Component>(selected))
    {
        char buffer[256]{};
        std::strncpy(buffer, tag->Name.c_str(), sizeof(buffer) - 1);
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            tag->Name = std::string(buffer);
    }

    ImGui::Separator();

    // =================================================================
    // 1. Transform
    // =================================================================
    if (auto* transform = reg.try_get<ECS::Components::Transform::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const bool posChanged = Interface::GUI::DrawVec3Control("Position", transform->Position);

            glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(transform->Rotation));
            const bool rotChanged = Interface::GUI::DrawVec3Control("Rotation", rotationDegrees);
            if (rotChanged)
                transform->Rotation = glm::quat(glm::radians(rotationDegrees));

            const bool scaleChanged = Interface::GUI::DrawVec3Control("Scale", transform->Scale, 1.0f);

            if (posChanged || rotChanged || scaleChanged)
                reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(selected);
        }
    }

    // =================================================================
    // 2. Surface
    // =================================================================
    if (auto* sc = reg.try_get<ECS::Surface::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto* geo = m_Engine->GetRenderOrchestrator().GetGeometryStorage().GetIfValid(sc->Geometry);

            if (geo)
            {
                ImGui::Text("Vertices: %zu", geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                ImGui::Text("Indices: %u", geo->GetIndexCount());

                const char* topoName = "Unknown";
                switch (geo->GetTopology())
                {
                case Graphics::PrimitiveTopology::Triangles: topoName = "Triangles"; break;
                case Graphics::PrimitiveTopology::Lines: topoName = "Lines"; break;
                case Graphics::PrimitiveTopology::Points: topoName = "Points"; break;
                }
                ImGui::Text("Topology: %s", topoName);
            }
            else
            {
                ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Invalid or unloaded geometry handle");
            }

            ImGui::Checkbox("Visible##Surface", &sc->Visible);

            // Wireframe / vertex toggles — only when geometry is valid.
            if (geo)
            {
                const auto topology = geo->GetTopology();

                if (topology == Graphics::PrimitiveTopology::Triangles)
                {
                    bool showWireframe = reg.all_of<ECS::Line::Component>(selected);
                    if (ImGui::Checkbox("Show Wireframe", &showWireframe))
                    {
                        if (showWireframe)
                            reg.emplace<ECS::Line::Component>(selected);
                        else
                            reg.remove<ECS::Line::Component>(selected);
                    }

                    if (!sc->CachedVertexColors.empty())
                        ImGui::Checkbox("Per-Vertex Colors", &sc->ShowPerVertexColors);
                    if (!sc->CachedFaceColors.empty())
                        ImGui::Checkbox("Per-Face Colors", &sc->ShowPerFaceColors);
                }

                bool showVertices = reg.all_of<ECS::Point::Component>(selected);
                if (ImGui::Checkbox("Show Vertices", &showVertices))
                {
                    if (showVertices)
                        reg.emplace<ECS::Point::Component>(selected);
                    else
                        reg.remove<ECS::Point::Component>(selected);
                }

                // Inline wireframe settings when Line::Component is present.
                if (auto* line = reg.try_get<ECS::Line::Component>(selected))
                {
                    ImGui::SeparatorText("Wireframe Settings");
                    ApplyComponentCommand<ECS::Line::Component>(*m_Engine, reg, selected, "Inspector: Wire Color",
                        [](auto& component)
                        {
                            return ColorEdit4("Wire Color", component.Color);
                        });
                    ApplyComponentCommand<ECS::Line::Component>(*m_Engine, reg, selected, "Inspector: Wire Width",
                        [](auto& component)
                        {
                            return ImGui::SliderFloat("Wire Width", &component.Width, 0.5f, 32.0f, "%.1f");
                        });
                    ImGui::Checkbox("Overlay##Wire", &line->Overlay);
                    if (line->HasPerEdgeColors)
                        ImGui::Checkbox("Per-Edge Colors", &line->ShowPerEdgeColors);
                }

                // Inline vertex settings when Point::Component is present.
                if (auto* pt = reg.try_get<ECS::Point::Component>(selected))
                {
                    ImGui::SeparatorText("Vertex Settings");
                    PointRenderModeCombo("Render Mode##Point", pt->Mode);
                    ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Vertex Size",
                        [](auto& component)
                        {
                            return ImGui::SliderFloat("Vertex Size", &component.Size, 0.0001f, 1.0f, "%.5f",
                                                      ImGuiSliderFlags_Logarithmic);
                        });
                    ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Vertex Size Multiplier",
                        [](auto& component)
                        {
                            return ImGui::SliderFloat("Size Multiplier##Point", &component.SizeMultiplier, 0.1f, 10.0f, "%.2f",
                                                      ImGuiSliderFlags_Logarithmic);
                        });
                    ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Vertex Color",
                        [](auto& component)
                        {
                            return ColorEdit4("Vertex Color", component.Color);
                        });
                }
            }
        }
    }

    // =================================================================
    // 3. Mesh Data — halfedge mesh info, property browsers, color
    //    sources, spectral modes, mesh analysis
    // =================================================================
    if (auto* md = reg.try_get<ECS::Mesh::Data>(selected))
    {
        auto meshRef = md->MeshRef;
        if (!meshRef)
        {
            if (auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected))
            {
                if (collider->CollisionRef)
                    meshRef = collider->CollisionRef->SourceMesh;
            }
        }

        if (meshRef && ImGui::CollapsingHeader("Mesh Data", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Vertices: %zu (live: %zu)", meshRef->VerticesSize(), meshRef->VertexCount());
            ImGui::Text("Edges: %zu", meshRef->EdgeCount());
            ImGui::Text("Halfedges: %zu", meshRef->HalfedgeCount());
            ImGui::Text("Faces: %zu (live: %zu)", meshRef->FacesSize(), meshRef->FaceCount());

            // --- Color Sources ---
            const Geometry::ConstPropertySet vtxPs{meshRef->VertexProperties()};
            const Geometry::ConstPropertySet edgePs{meshRef->EdgeProperties()};
            const Geometry::ConstPropertySet halfedgePs{meshRef->HalfedgeProperties()};
            const Geometry::ConstPropertySet facePs{meshRef->FaceProperties()};

            if (ColorSourceWidget("Vertex Color Source", md->Visualization.VertexColors, &vtxPs, "MeshVtx"))
            {
                if (md->Visualization.VertexColors.PropertyName != "v:kmeans_label_f")
                    md->Visualization.UseNearestVertexColors = false;
                md->AttributesDirty = true;
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
            }
            if (ColorSourceWidget("Edge Color Source", md->Visualization.EdgeColors, &edgePs, "MeshEdge"))
            {
                md->AttributesDirty = true;
                reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);
            }
            if (ColorSourceWidget("Face Color Source", md->Visualization.FaceColors, &facePs, "MeshFace"))
            {
                md->AttributesDirty = true;
                reg.emplace_or_replace<ECS::DirtyTag::FaceAttributes>(selected);
            }

            // Publish normals as PropertySet properties so they appear in
            // vector field and color source dropdowns.
            if (ImGui::Button("Publish Normals##Mesh"))
            {
                Geometry::MeshUtils::PublishVertexNormals(*meshRef);
                Geometry::MeshUtils::PublishFaceNormals(*meshRef);
                md->AttributesDirty = true;
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                reg.emplace_or_replace<ECS::DirtyTag::FaceAttributes>(selected);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Compute area-weighted vertex and face normals\n"
                                  "and store as v:normal / f:normal properties.\n"
                                  "These then appear in vector field and color source selectors.");

            if (VectorFieldWidget(md->Visualization, &vtxPs, &edgePs, &facePs, "MeshVF", &reg))
                md->Visualization.VectorFieldsDirty = true;

            // --- Property Browsers ---
            if (ImGui::CollapsingHeader("Property Browser##Mesh"))
            {
                DrawPropertySetBrowserWidget("Vertex Properties", &vtxPs, m_MeshVertexPropertiesUi, "MeshVertexProps");
                DrawPropertySetBrowserWidget("Edge Properties", &edgePs, m_MeshEdgePropertiesUi, "MeshEdgeProps");
                DrawPropertySetBrowserWidget("Halfedge Properties", &halfedgePs, m_MeshHalfedgePropertiesUi, "MeshHalfedgeProps");
                DrawPropertySetBrowserWidget("Face Properties", &facePs, m_MeshFacePropertiesUi, "MeshFaceProps");
            }

            // --- Spectral Modes ---
            if (ImGui::CollapsingHeader("Spectral Modes##Mesh"))
            {
                static_cast<void>(DrawMeshSpectralWidget(*m_Engine, selected, m_MeshSpectralUi));
                if (m_GeometryWorkflow)
                {
                    if (ImGui::SmallButton("Open Dedicated Mesh Spectral Panel"))
                        m_GeometryWorkflow->OpenMeshSpectralPanel();
                }
            }

            // --- Mesh Analysis ---
            if (ImGui::CollapsingHeader("Mesh Analysis##Mesh"))
            {
                ImGui::TextDisabled("Run analysis to publish defect markers and scalar mirrors.");

                if (ImGui::Button("Run Mesh Analysis"))
                {
                    if (auto analysis = Geometry::MeshAnalysis::Analyze(*meshRef))
                    {
                        m_MeshAnalysisUi.HasResults = true;
                        m_MeshAnalysisUi.ProblemVertices = analysis->ProblemVertices.size();
                        m_MeshAnalysisUi.ProblemEdges = analysis->ProblemEdges.size();
                        m_MeshAnalysisUi.ProblemHalfedges = analysis->ProblemHalfedges.size();
                        m_MeshAnalysisUi.ProblemFaces = analysis->ProblemFaces.size();
                        m_MeshAnalysisUi.BoundaryVertices = analysis->BoundaryVertexCount;
                        m_MeshAnalysisUi.BoundaryEdges = analysis->BoundaryEdgeCount;
                        m_MeshAnalysisUi.BoundaryFaces = analysis->BoundaryFaceCount;
                        m_MeshAnalysisUi.NonManifoldVertices = analysis->NonManifoldVertexCount;
                        m_MeshAnalysisUi.DegenerateFaces = analysis->DegenerateFaceCount;
                        m_MeshAnalysisUi.NonTriangleFaces = analysis->NonTriangleFaceCount;
                        m_MeshAnalysisUi.NonFiniteElements = analysis->NonFiniteVertexCount
                                                           + analysis->NonFiniteEdgeCount
                                                           + analysis->NonFiniteFaceCount;

                        auto vMirror = meshRef->VertexProperties().GetOrAdd<float>("v:analysis_problem_f", 0.0f);
                        auto eMirror = meshRef->EdgeProperties().GetOrAdd<float>("e:analysis_problem_f", 0.0f);
                        auto hMirror = meshRef->HalfedgeProperties().GetOrAdd<float>("h:analysis_problem_f", 0.0f);
                        auto fMirror = meshRef->FaceProperties().GetOrAdd<float>("f:analysis_problem_f", 0.0f);

                        for (std::size_t i = 0; i < meshRef->VerticesSize(); ++i)
                            vMirror[i] = analysis->ProblemVertex[Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}] ? 1.0f : 0.0f;
                        for (std::size_t i = 0; i < meshRef->EdgesSize(); ++i)
                            eMirror[i] = analysis->ProblemEdge[Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)}] ? 1.0f : 0.0f;
                        for (std::size_t i = 0; i < meshRef->HalfedgesSize(); ++i)
                            hMirror[i] = analysis->ProblemHalfedge[Geometry::HalfedgeHandle{static_cast<Geometry::PropertyIndex>(i)}] ? 1.0f : 0.0f;
                        for (std::size_t i = 0; i < meshRef->FacesSize(); ++i)
                            fMirror[i] = analysis->ProblemFace[Geometry::FaceHandle{static_cast<Geometry::PropertyIndex>(i)}] ? 1.0f : 0.0f;

                        md->Visualization.VertexColors.PropertyName = "v:analysis_problem_f";
                        md->Visualization.VertexColors.AutoRange = true;
                        md->Visualization.EdgeColors.PropertyName = "e:analysis_problem_f";
                        md->Visualization.EdgeColors.AutoRange = true;
                        md->Visualization.FaceColors.PropertyName = "f:analysis_problem_f";
                        md->Visualization.FaceColors.AutoRange = true;
                        md->AttributesDirty = true;
                        reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                        reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);
                        reg.emplace_or_replace<ECS::DirtyTag::FaceAttributes>(selected);
                    }
                    else
                    {
                        m_MeshAnalysisUi.HasResults = false;
                    }
                }

                if (m_MeshAnalysisUi.HasResults)
                {
                    ImGui::SeparatorText("Last Analysis");
                    ImGui::Text("Problem Vertices: %zu", m_MeshAnalysisUi.ProblemVertices);
                    ImGui::Text("Problem Edges: %zu", m_MeshAnalysisUi.ProblemEdges);
                    ImGui::Text("Problem Halfedges: %zu", m_MeshAnalysisUi.ProblemHalfedges);
                    ImGui::Text("Problem Faces: %zu", m_MeshAnalysisUi.ProblemFaces);
                    ImGui::Text("Boundary V/E/F: %zu / %zu / %zu",
                                m_MeshAnalysisUi.BoundaryVertices,
                                m_MeshAnalysisUi.BoundaryEdges,
                                m_MeshAnalysisUi.BoundaryFaces);
                    ImGui::Text("Non-Manifold Vertices: %zu", m_MeshAnalysisUi.NonManifoldVertices);
                    ImGui::Text("Degenerate Faces: %zu", m_MeshAnalysisUi.DegenerateFaces);
                    ImGui::Text("Non-Triangle Faces: %zu", m_MeshAnalysisUi.NonTriangleFaces);
                    ImGui::Text("Non-Finite Elements: %zu", m_MeshAnalysisUi.NonFiniteElements);
                }
            }
        }
    }

    // =================================================================
    // 4. Graph
    // =================================================================
    if (auto* gd = reg.try_get<ECS::Graph::Data>(selected))
    {
        if (ImGui::CollapsingHeader("Graph", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Nodes: %zu", gd->NodeCount());
            ImGui::Text("Edges: %zu", gd->EdgeCount());
            ImGui::Text("Has Node Colors: %s", gd->HasNodeColors() ? "Yes" : "No");
            ImGui::Text("Has Node Radii: %s", gd->HasNodeRadii() ? "Yes" : "No");
            ImGui::Text("Has Edge Colors: %s", gd->HasEdgeColors() ? "Yes" : "No");
            ImGui::Checkbox("Static Geometry", &gd->StaticGeometry);

            ImGui::Checkbox("Visible##Graph", &gd->Visible);

            ImGui::SeparatorText("Node Settings");
            if (PointRenderModeCombo("Node Render Mode", gd->NodeRenderMode))
                gd->GpuDirty = true;
            if (ImGui::SliderFloat("Node Size", &gd->DefaultNodeRadius, 0.0001f, 1.0f, "%.5f",
                               ImGuiSliderFlags_Logarithmic))
                gd->GpuDirty = true;
            if (ImGui::SliderFloat("Node Size Multiplier", &gd->NodeSizeMultiplier, 0.1f, 10.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic))
                gd->GpuDirty = true;
            if (ColorEdit4("Node Color", gd->DefaultNodeColor))
                gd->GpuDirty = true;

            ImGui::SeparatorText("Edge Settings");
            if (ColorEdit4("Edge Color", gd->DefaultEdgeColor))
                gd->GpuDirty = true;
            if (ImGui::SliderFloat("Edge Width", &gd->EdgeWidth, 0.5f, 32.0f, "%.1f"))
                gd->GpuDirty = true;
            if (ImGui::Checkbox("Edge Overlay", &gd->EdgesOverlay))
                gd->GpuDirty = true;

            if (gd->HasEdgeColors())
            {
                if (auto* line = reg.try_get<ECS::Line::Component>(selected))
                    ImGui::Checkbox("Per-Edge Colors##Graph", &line->ShowPerEdgeColors);
            }

            if (gd->GraphRef)
            {
                const Geometry::ConstPropertySet vtxPs{gd->GraphRef->VertexProperties()};
                const Geometry::ConstPropertySet edgePs{gd->GraphRef->EdgeProperties()};
                const Geometry::ConstPropertySet halfedgePs{gd->GraphRef->HalfedgeProperties()};

                if (ColorSourceWidget("Node Color Source", gd->Visualization.VertexColors, &vtxPs, "GraphVtx"))
                    reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                if (ColorSourceWidget("Edge Color Source", gd->Visualization.EdgeColors, &edgePs, "GraphEdge"))
                    reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);

                if (VectorFieldWidget(gd->Visualization, &vtxPs, &edgePs, nullptr, "GraphVF", &reg))
                    gd->Visualization.VectorFieldsDirty = true;

                if (ImGui::CollapsingHeader("Property Browser##Graph"))
                {
                    DrawPropertySetBrowserWidget("Vertex Properties", &vtxPs, m_GraphVertexPropertiesUi, "GraphVertexProps");
                    DrawPropertySetBrowserWidget("Edge Properties", &edgePs, m_GraphEdgePropertiesUi, "GraphEdgeProps");
                    DrawPropertySetBrowserWidget("Halfedge Properties", &halfedgePs, m_GraphHalfedgePropertiesUi, "GraphHalfedgeProps");
                }

                if (ImGui::CollapsingHeader("Spectral Layout##Graph"))
                {
                    static_cast<void>(DrawGraphSpectralWidget(*m_Engine, selected, m_GraphSpectralUi));
                    if (m_GeometryWorkflow)
                    {
                        if (ImGui::SmallButton("Open Dedicated Graph Spectral Panel"))
                            m_GeometryWorkflow->OpenGraphSpectralPanel();
                    }
                }
            }
        }
    }

    // =================================================================
    // 5. Point Cloud
    // =================================================================
    if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(selected))
    {
        if (ImGui::CollapsingHeader("Point Cloud", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Points: %zu", pcd->PointCount());
            ImGui::Text("Has Normals: %s", pcd->HasRenderableNormals() ? "Yes" : "No");
            ImGui::Text("Has Colors: %s", pcd->HasColors() ? "Yes" : "No");
            ImGui::Text("Has Radii: %s", pcd->HasRadii() ? "Yes" : "No");
            ImGui::Text("GPU Points: %u", pcd->GpuPointCount);

            ImGui::SeparatorText("Rendering");
            ImGui::Checkbox("Visible##PCD", &pcd->Visible);

            if (PointRenderModeCombo("Render Mode##PCD", pcd->RenderMode))
                pcd->GpuDirty = true;

            if (ImGui::SliderFloat("Default Radius##PCD", &pcd->DefaultRadius, 0.0001f, 1.0f, "%.5f",
                               ImGuiSliderFlags_Logarithmic))
                pcd->GpuDirty = true;
            if (ImGui::SliderFloat("Size Multiplier##PCD", &pcd->SizeMultiplier, 0.1f, 10.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic))
                pcd->GpuDirty = true;

            if (ColorEdit4("Default Color##PCD", pcd->DefaultColor))
                pcd->GpuDirty = true;

            if (pcd->CloudRef)
            {
                const Geometry::ConstPropertySet ptPs{pcd->CloudRef->PointProperties()};

                if (ColorSourceWidget("Point Color Source", pcd->Visualization.VertexColors, &ptPs, "PCDVtx"))
                    reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);

                if (VectorFieldWidget(pcd->Visualization, &ptPs, nullptr, nullptr, "PCDVF", &reg))
                    pcd->Visualization.VectorFieldsDirty = true;

                if (ImGui::CollapsingHeader("Property Browser##PointCloud"))
                {
                    DrawPropertySetBrowserWidget("Point Properties", &ptPs, m_PointCloudPropertiesUi, "PointCloudProps");
                }
            }
        }
    }

    // =================================================================
    // 6. Standalone Line/Point component editors — only shown when no
    //    Surface component is present (for Graph/PointCloud entities).
    //    Mesh entities show these inline in the Surface section above.
    // =================================================================
    if (!reg.all_of<ECS::Surface::Component>(selected))
    {
        if (auto* line = reg.try_get<ECS::Line::Component>(selected))
        {
            if (ImGui::CollapsingHeader("Line Rendering"))
            {
                ApplyComponentCommand<ECS::Line::Component>(*m_Engine, reg, selected, "Inspector: Line Color",
                    [](auto& component)
                    {
                        return ColorEdit4("Line Color", component.Color);
                    });
                ApplyComponentCommand<ECS::Line::Component>(*m_Engine, reg, selected, "Inspector: Line Width",
                    [](auto& component)
                    {
                        return ImGui::SliderFloat("Line Width", &component.Width, 0.5f, 32.0f, "%.1f");
                    });
                ImGui::Checkbox("Overlay##Line", &line->Overlay);
                ImGui::Text("Edge Count: %u", line->EdgeCount);

                if (line->HasPerEdgeColors)
                    ImGui::Checkbox("Per-Edge Colors##Standalone", &line->ShowPerEdgeColors);
            }
        }

        if (auto* pt = reg.try_get<ECS::Point::Component>(selected))
        {
            if (ImGui::CollapsingHeader("Point Rendering"))
            {
                PointRenderModeCombo("Render Mode##StandalonePoint", pt->Mode);
                ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Point Size",
                    [](auto& component)
                    {
                        return ImGui::SliderFloat("Point Size", &component.Size, 0.0001f, 1.0f, "%.5f",
                                                  ImGuiSliderFlags_Logarithmic);
                    });
                ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Point Size Multiplier",
                    [](auto& component)
                    {
                        return ImGui::SliderFloat("Size Multiplier##StandalonePoint", &component.SizeMultiplier, 0.1f, 10.0f, "%.2f",
                                                  ImGuiSliderFlags_Logarithmic);
                    });
                ApplyComponentCommand<ECS::Point::Component>(*m_Engine, reg, selected, "Inspector: Point Color",
                    [](auto& component)
                    {
                        return ColorEdit4("Point Color", component.Color);
                    });
            }
        }
    }

    // =================================================================
    // 8. Geometry Processing — algorithms as CollapsingHeaders
    // =================================================================
    const auto processing = GetGeometryProcessingCapabilities(reg, selected);
    const auto algorithms = ResolveGeometryProcessingEntries(reg, selected);
    if (processing.HasAny() && !algorithms.empty())
    {
        if (ImGui::CollapsingHeader("Geometry Processing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawDomainBadges(processing.Domains);
            ImGui::Spacing();

            for (const auto& entry : algorithms)
            {
                const char* algoLabel = GeometryProcessingAlgorithmLabel(entry.Algorithm);
                const std::string headerId = std::string(algoLabel) + "##GeoProc";

                if (ImGui::CollapsingHeader(headerId.c_str()))
                {
                    DrawDomainBadges(entry.Domains);

                    switch (entry.Algorithm)
                    {
                    case GeometryProcessingAlgorithm::KMeans:
                        static_cast<void>(DrawKMeansWidget(*m_Engine, selected, m_KMeansUi));
                        break;
                    case GeometryProcessingAlgorithm::Remeshing:
                        static_cast<void>(DrawRemeshingWidget(*m_Engine, selected, m_RemeshingUi));
                        break;
                    case GeometryProcessingAlgorithm::Simplification:
                        static_cast<void>(DrawSimplificationWidget(*m_Engine, selected, m_SimplificationUi));
                        break;
                    case GeometryProcessingAlgorithm::Smoothing:
                        static_cast<void>(DrawSmoothingWidget(*m_Engine, selected, m_SmoothingUi));
                        break;
                    case GeometryProcessingAlgorithm::Subdivision:
                        static_cast<void>(DrawSubdivisionWidget(*m_Engine, selected, m_SubdivisionUi));
                        break;
                    case GeometryProcessingAlgorithm::Repair:
                        static_cast<void>(DrawRepairWidget(*m_Engine, selected));
                        break;
                    case GeometryProcessingAlgorithm::NormalEstimation:
                        static_cast<void>(DrawNormalEstimationWidget(*m_Engine, selected, m_NormalEstimationUi));
                        break;
                    case GeometryProcessingAlgorithm::ShortestPath:
                        static_cast<void>(DrawShortestPathWidget(*m_Engine, selected, m_ShortestPathUi));
                        break;
                    case GeometryProcessingAlgorithm::ConvexHull:
                        static_cast<void>(DrawConvexHullWidget(*m_Engine, selected, m_ConvexHullUi));
                        break;
                    case GeometryProcessingAlgorithm::SurfaceReconstruction:
                        static_cast<void>(DrawSurfaceReconstructionWidget(*m_Engine, selected, m_SurfaceReconstructionUi));
                        break;
                    }

                    if (m_GeometryWorkflow && entry.Algorithm != GeometryProcessingAlgorithm::KMeans
                        && entry.Algorithm != GeometryProcessingAlgorithm::NormalEstimation
                        && entry.Algorithm != GeometryProcessingAlgorithm::ShortestPath
                        && entry.Algorithm != GeometryProcessingAlgorithm::ConvexHull
                        && entry.Algorithm != GeometryProcessingAlgorithm::SurfaceReconstruction)
                    {
                        if (ImGui::SmallButton((std::string("Open Dedicated Panel##")
                                             + algoLabel).c_str()))
                        {
                            m_GeometryWorkflow->OpenAlgorithmPanel(entry.Algorithm);
                        }
                    }
                }
            }
        }
    }

    // =================================================================
    // 9. Mesh Collider
    // =================================================================
    if (auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Mesh Collider"))
        {
            if (collider->CollisionRef)
            {
                ImGui::Text("Positions: %zu", collider->CollisionRef->Positions.size());
                ImGui::Text("Indices: %zu", collider->CollisionRef->Indices.size());
                ImGui::Text("Triangles: %zu", collider->CollisionRef->Indices.size() / 3);
                ImGui::Text("Has Source Mesh: %s", collider->CollisionRef->SourceMesh ? "Yes" : "No");

                const auto& aabb = collider->CollisionRef->LocalAABB;
                ImGui::Text("Local AABB Min: (%.3f, %.3f, %.3f)", aabb.Min.x, aabb.Min.y, aabb.Min.z);
                ImGui::Text("Local AABB Max: (%.3f, %.3f, %.3f)", aabb.Max.x, aabb.Max.y, aabb.Max.z);

                ImGui::SeparatorText("World OBB");
                const auto& obb = collider->WorldOBB;
                ImGui::Text("Center: (%.3f, %.3f, %.3f)", obb.Center.x, obb.Center.y, obb.Center.z);
                ImGui::Text("Extents: (%.3f, %.3f, %.3f)", obb.Extents.x, obb.Extents.y, obb.Extents.z);
            }
            else
            {
                ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "No collision data");
            }
        }
    }

    // =================================================================
    // 10. Hierarchy
    // =================================================================
    if (auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Hierarchy"))
        {
            EntityIdText("Parent", hierarchy->Parent);
            EntityIdText("First Child", hierarchy->FirstChild);
            EntityIdText("Next Sibling", hierarchy->NextSibling);
            EntityIdText("Prev Sibling", hierarchy->PrevSibling);
            ImGui::Text("Child Count: %u", hierarchy->ChildCount);

            // Show data authority type if present.
            if (reg.all_of<ECS::DataAuthority::MeshTag>(selected))
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Authority: Mesh");
            else if (reg.all_of<ECS::DataAuthority::GraphTag>(selected))
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Authority: Graph");
            else if (reg.all_of<ECS::DataAuthority::PointCloudTag>(selected))
                ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "Authority: PointCloud");
        }

        // --- Overlay Children --- List child entities that carry their
        // own data authority (point cloud overlays, graph overlays, etc.).
        if (hierarchy->ChildCount > 0 && ImGui::CollapsingHeader("Overlay Children"))
        {
            entt::entity child = hierarchy->FirstChild;
            while (child != entt::null && reg.valid(child))
            {
                const bool isOverlay = reg.any_of<ECS::DataAuthority::MeshTag,
                                                   ECS::DataAuthority::PointCloudTag,
                                                   ECS::DataAuthority::GraphTag>(child);
                if (isOverlay)
                {
                    const char* overlayType = "Unknown";
                    if (reg.all_of<ECS::DataAuthority::MeshTag>(child))
                        overlayType = "Mesh";
                    else if (reg.all_of<ECS::DataAuthority::PointCloudTag>(child))
                        overlayType = "PointCloud";
                    else if (reg.all_of<ECS::DataAuthority::GraphTag>(child))
                        overlayType = "Graph";
                    std::string childName = "?";
                    if (auto* nameTag = reg.try_get<ECS::Components::NameTag::Component>(child))
                        childName = nameTag->Name;

                    ImGui::PushID(static_cast<int>(static_cast<entt::id_type>(child)));

                    const std::string label = childName + " [" + overlayType + "]";
                    if (ImGui::TreeNode(label.c_str()))
                    {
                        // Inline visibility toggle for overlay children.
                        if (auto* sc = reg.try_get<ECS::Surface::Component>(child))
                            ImGui::Checkbox("Visible##Overlay", &sc->Visible);
                        else if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(child))
                            ImGui::Checkbox("Visible##Overlay", &pcd->Visible);
                        else if (auto* gd = reg.try_get<ECS::Graph::Data>(child))
                            ImGui::Checkbox("Visible##Overlay", &gd->Visible);

                        if (ImGui::SmallButton("Select"))
                            *m_CachedSelected = child;

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                auto* childHier = reg.try_get<ECS::Components::Hierarchy::Component>(child);
                child = childHier ? childHier->NextSibling : entt::null;
            }
        }
    }

    // =================================================================
    // 11. Mesh Edge View
    // =================================================================
    if (auto* ev = reg.try_get<ECS::MeshEdgeView::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Mesh Edge View"))
        {
            ImGui::Text("Edge Count: %u", ev->EdgeCount);
            if (ev->GpuSlot != ECS::kInvalidGpuSlot)
                ImGui::Text("GPU Slot: %u", ev->GpuSlot);
            else
                ImGui::Text("GPU Slot: (none)");
            ImGui::Text("Dirty: %s", ev->Dirty ? "Yes" : "No");
        }
    }

    // =================================================================
    // 12. Mesh Vertex View
    // =================================================================
    if (auto* vv = reg.try_get<ECS::MeshVertexView::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Mesh Vertex View"))
        {
            ImGui::Text("Vertex Count: %u", vv->VertexCount);
            if (vv->GpuSlot != ECS::kInvalidGpuSlot)
                ImGui::Text("GPU Slot: %u", vv->GpuSlot);
            else
                ImGui::Text("GPU Slot: (none)");
            ImGui::Text("Dirty: %s", vv->Dirty ? "Yes" : "No");
        }
    }

    // =================================================================
    // 13. Primitive BVH
    // =================================================================
    if (auto* bvh = reg.try_get<ECS::PrimitiveBVH::Data>(selected))
    {
        if (ImGui::CollapsingHeader("Primitive BVH"))
        {
            const char* sourceNames[] = {"None", "Triangles", "Segments", "Points"};
            int srcIdx = static_cast<int>(bvh->Source);
            if (srcIdx < 0 || srcIdx > 3) srcIdx = 0;
            ImGui::Text("Source: %s", sourceNames[srcIdx]);
            ImGui::Text("Primitive Count: %u", bvh->PrimitiveCount);
            ImGui::Text("Triangles: %zu", bvh->Triangles.size());
            ImGui::Text("Segments: %zu", bvh->Segments.size());
            ImGui::Text("Points: %zu", bvh->Points.size());
            ImGui::Text("Dirty: %s", bvh->Dirty ? "Yes" : "No");

            const auto& bounds = bvh->LocalBounds;
            ImGui::Text("Local Bounds Min: (%.3f, %.3f, %.3f)", bounds.Min.x, bounds.Min.y, bounds.Min.z);
            ImGui::Text("Local Bounds Max: (%.3f, %.3f, %.3f)", bounds.Max.x, bounds.Max.y, bounds.Max.z);
        }
    }

    // =================================================================
    // 14. Point KDTree
    // =================================================================
    if (auto* kdTree = reg.try_get<ECS::PointKDTree::Data>(selected))
    {
        if (ImGui::CollapsingHeader("Point KD-Tree"))
        {
            ImGui::Text("Point Count: %u", kdTree->PointCount);
            ImGui::Text("Dirty: %s", kdTree->Dirty ? "Yes" : "No");
        }
    }

    // =================================================================
    // 15. DEC (Discrete Exterior Calculus)
    // =================================================================
    if (auto* dec = reg.try_get<ECS::Components::DEC::Component>(selected))
    {
        if (ImGui::CollapsingHeader("DEC Operators"))
        {
            if (dec->Operators)
            {
                ImGui::Text("Hodge0 Size: %zu", dec->Operators->Hodge0.Size);
                ImGui::Text("Laplacian: %zu x %zu", dec->Operators->Laplacian.Rows,
                            dec->Operators->Laplacian.Cols);
                ImGui::Text("Laplacian NNZ: %zu", dec->Operators->Laplacian.Values.size());
            }
            else
            {
                ImGui::TextDisabled("No operators computed");
            }
        }
    }

    // =================================================================
    // 16. Axis Rotator
    // =================================================================
    if (auto* rotator = reg.try_get<ECS::Components::AxisRotator::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Axis Rotator"))
        {
            ImGui::DragFloat("Speed (deg/s)", &rotator->Speed, 1.0f, -360.0f, 360.0f, "%.1f");
            ImGui::DragFloat3("Axis", &rotator->axis.x, 0.01f, -1.0f, 1.0f, "%.3f");
        }
    }

    // =================================================================
    // 17. Asset Source Reference
    // =================================================================
    if (auto* assetRef = reg.try_get<ECS::Components::AssetSourceRef::Component>(selected))
    {
        if (ImGui::CollapsingHeader("Asset Source"))
        {
            ImGui::TextWrapped("Path: %s", assetRef->SourcePath.c_str());
        }
    }

    // =================================================================
    // 18. Selection Info (PickID + tags)
    // =================================================================
    {
        const bool hasPickId = reg.all_of<ECS::Components::Selection::PickID>(selected);
        const bool hasSelectable = reg.all_of<ECS::Components::Selection::SelectableTag>(selected);
        const bool hasSelected = reg.all_of<ECS::Components::Selection::SelectedTag>(selected);
        const bool hasHovered = reg.all_of<ECS::Components::Selection::HoveredTag>(selected);

        if (hasPickId || hasSelectable || hasSelected || hasHovered)
        {
            if (ImGui::CollapsingHeader("Selection Info"))
            {
                if (hasPickId)
                {
                    const auto& pid = reg.get<ECS::Components::Selection::PickID>(selected);
                    ImGui::Text("Pick ID: %u", pid.Value);
                }

                ImGui::Text("Tags:");
                TagBadge("Selectable", hasSelectable);
                TagBadge("Selected", hasSelected);
                TagBadge("Hovered", hasHovered);
            }
        }
    }

    // =================================================================
    // 19. Dirty Tags (read-only status badges)
    // =================================================================
    {
        const bool dVP = reg.all_of<ECS::DirtyTag::VertexPositions>(selected);
        const bool dVA = reg.all_of<ECS::DirtyTag::VertexAttributes>(selected);
        const bool dET = reg.all_of<ECS::DirtyTag::EdgeTopology>(selected);
        const bool dEA = reg.all_of<ECS::DirtyTag::EdgeAttributes>(selected);
        const bool dFT = reg.all_of<ECS::DirtyTag::FaceTopology>(selected);
        const bool dFA = reg.all_of<ECS::DirtyTag::FaceAttributes>(selected);
        const bool dTrans = reg.all_of<ECS::Components::Transform::IsDirtyTag>(selected);
        const bool dWorldUp = reg.all_of<ECS::Components::Transform::WorldUpdatedTag>(selected);
        const bool dDEC = reg.all_of<ECS::Components::DEC::DirtyTag>(selected);

        const bool anyDirty = dVP || dVA || dET || dEA || dFT || dFA || dTrans || dWorldUp || dDEC;

        if (anyDirty && ImGui::CollapsingHeader("Dirty Tags"))
        {
            if (dVP)  ImGui::BulletText("VertexPositions");
            if (dVA)  ImGui::BulletText("VertexAttributes");
            if (dET)  ImGui::BulletText("EdgeTopology");
            if (dEA)  ImGui::BulletText("EdgeAttributes");
            if (dFT)  ImGui::BulletText("FaceTopology");
            if (dFA)  ImGui::BulletText("FaceAttributes");
            if (dTrans) ImGui::BulletText("Transform::IsDirty");
            if (dWorldUp) ImGui::BulletText("Transform::WorldUpdated");
            if (dDEC) ImGui::BulletText("DEC::Dirty");
        }
    }

    ImGui::End();
}

} // namespace Runtime::EditorUI
