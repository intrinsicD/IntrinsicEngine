// Runtime.EditorUI.InspectorController — Component property inspector panel
// for entity name, transform, surface info, graph/point-cloud/mesh
// visualization controls. Uses ColorSourceWidget and VectorFieldWidget for
// PropertySet-driven color mapping.

module;

#include <cstring>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <entt/entity/registry.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Graphics.Components;
import Graphics.Geometry;
import ECS;
import Interface;

namespace Runtime::EditorUI
{
void InspectorController::Init(Runtime::Engine& engine,
                               entt::entity& cachedSelected,
                               GeometryWorkflowController* geometryWorkflow)
{
    m_Engine = &engine;
    m_CachedSelected = &cachedSelected;
    m_GeometryWorkflow = geometryWorkflow;
}

void InspectorController::Draw()
{
    ImGui::Begin("Inspector");

    const entt::entity selected = *m_CachedSelected;

    if (selected != entt::null && m_Engine->GetScene().GetRegistry().valid(selected))
    {
        auto& reg = m_Engine->GetScene().GetRegistry();

        // 1. Tag Component
        if (reg.all_of<ECS::Components::NameTag::Component>(selected))
        {
            auto& tag = reg.get<ECS::Components::NameTag::Component>(selected);
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, tag.Name.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            {
                tag.Name = std::string(buffer);
            }
        }

        ImGui::Separator();

        // 2. Transform Component
        if (reg.all_of<ECS::Components::Transform::Component>(selected))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& transform = reg.get<ECS::Components::Transform::Component>(selected);

                const bool posChanged = Interface::GUI::DrawVec3Control("Position", transform.Position);

                glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(transform.Rotation));
                const bool rotChanged = Interface::GUI::DrawVec3Control("Rotation", rotationDegrees);
                if (rotChanged)
                {
                    transform.Rotation = glm::quat(glm::radians(rotationDegrees));
                }

                const bool scaleChanged = Interface::GUI::DrawVec3Control("Scale", transform.Scale, 1.0f);

                if (posChanged || rotChanged || scaleChanged)
                {
                    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(selected);
                }
            }
        }

        // 3. Mesh Info
        if (reg.all_of<ECS::Surface::Component>(selected))
        {
            if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& sc = reg.get<ECS::Surface::Component>(selected);
                Graphics::GeometryGpuData* geo = m_Engine->GetGeometryStorage().GetIfValid(sc.Geometry);

                if (geo)
                {
                    ImGui::Text("Vertices: %lu", geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                    ImGui::Text("Indices: %u", geo->GetIndexCount());

                    std::string topoName = "Unknown";
                    switch (geo->GetTopology())
                    {
                    case Graphics::PrimitiveTopology::Triangles: topoName = "Triangles";
                        break;
                    case Graphics::PrimitiveTopology::Lines: topoName = "Lines";
                        break;
                    case Graphics::PrimitiveTopology::Points: topoName = "Points";
                        break;
                    }
                    ImGui::Text("Topology: %s", topoName.c_str());
                }
                else
                {
                    ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Invalid or unloaded Geometry Handle");
                }
            }
        }

        // 4. Graph
        if (reg.all_of<ECS::Graph::Data>(selected))
        {
            if (ImGui::CollapsingHeader("Graph", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& gd = reg.get<ECS::Graph::Data>(selected);
                ImGui::Text("Nodes: %zu", gd.NodeCount());
                ImGui::Text("Edges: %zu", gd.EdgeCount());
                ImGui::Text("Has Node Colors: %s", gd.HasNodeColors() ? "Yes" : "No");
                ImGui::Text("Has Edge Colors: %s", gd.HasEdgeColors() ? "Yes" : "No");

                ImGui::Checkbox("Visible##Graph", &gd.Visible);

                ImGui::SeparatorText("Node Settings");
                PointRenderModeCombo("Node Render Mode", gd.NodeRenderMode);
                ImGui::SliderFloat("Node Size", &gd.DefaultNodeRadius, 0.0005f, 0.05f, "%.5f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Node Size Multiplier", &gd.NodeSizeMultiplier, 0.1f, 10.0f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
                ColorEdit4("Node Color", gd.DefaultNodeColor);

                ImGui::SeparatorText("Edge Settings");
                ColorEdit4("Edge Color", gd.DefaultEdgeColor);
                ImGui::SliderFloat("Edge Width", &gd.EdgeWidth, 0.5f, 5.0f);
                ImGui::Checkbox("Edge Overlay", &gd.EdgesOverlay);

                if (gd.HasEdgeColors())
                {
                    if (auto* line = reg.try_get<ECS::Line::Component>(selected))
                        ImGui::Checkbox("Per-Edge Colors##Graph", &line->ShowPerEdgeColors);
                }

                if (gd.GraphRef)
                {
                    const auto* vtxPs = &gd.GraphRef->VertexProperties();
                    const auto* edgePs = &gd.GraphRef->EdgeProperties();
                    const auto* halfedgePs = &gd.GraphRef->HalfedgeProperties();

                    if (ColorSourceWidget("Node Color Source", gd.Visualization.VertexColors, vtxPs,
                                          "GraphVtx"))
                        reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                    if (ColorSourceWidget("Edge Color Source", gd.Visualization.EdgeColors, edgePs,
                                          "GraphEdge"))
                        reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);

                    VectorFieldWidget(gd.Visualization, vtxPs, "GraphVF");

                    if (ImGui::TreeNodeEx("Property Browser##Graph", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawPropertySetBrowserWidget("Vertex Properties", vtxPs, m_GraphVertexPropertiesUi, "GraphVertexProps");
                        DrawPropertySetBrowserWidget("Edge Properties", edgePs, m_GraphEdgePropertiesUi, "GraphEdgeProps");
                        DrawPropertySetBrowserWidget("Halfedge Properties", halfedgePs, m_GraphHalfedgePropertiesUi, "GraphHalfedgeProps");
                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNodeEx("Spectral Layout##Graph", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        static_cast<void>(DrawGraphSpectralWidget(*m_Engine, selected, m_GraphSpectralUi));
                        if (m_GeometryWorkflow)
                        {
                            if (ImGui::SmallButton("Open Dedicated Graph Spectral Panel"))
                                m_GeometryWorkflow->OpenGraphSpectralPanel();
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }

        // 5. Point Cloud
        if (reg.all_of<ECS::PointCloud::Data>(selected))
        {
            if (ImGui::CollapsingHeader("Point Cloud", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& pcd = reg.get<ECS::PointCloud::Data>(selected);
                ImGui::Text("Points: %zu", pcd.PointCount());
                ImGui::Text("Has Normals: %s", pcd.HasRenderableNormals() ? "Yes" : "No");
                ImGui::Text("Has Colors: %s", pcd.HasColors() ? "Yes" : "No");
                ImGui::Text("Has Radii: %s", pcd.HasRadii() ? "Yes" : "No");

                ImGui::SeparatorText("Rendering");
                ImGui::Checkbox("Visible##PCD", &pcd.Visible);

                PointRenderModeCombo("Render Mode##PCD", pcd.RenderMode);

                ImGui::SliderFloat("Default Radius##PCD", &pcd.DefaultRadius, 0.0005f, 0.1f, "%.5f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Size Multiplier##PCD", &pcd.SizeMultiplier, 0.1f, 10.0f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);

                ColorEdit4("Default Color##PCD", pcd.DefaultColor);

                if (pcd.CloudRef)
                {
                    const auto* ptPs = &pcd.CloudRef->PointProperties();

                    if (ColorSourceWidget("Point Color Source", pcd.Visualization.VertexColors, ptPs, "PCDVtx"))
                        reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);

                    VectorFieldWidget(pcd.Visualization, ptPs, "PCDVF");

                    if (ImGui::TreeNodeEx("Property Browser##PointCloud", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawPropertySetBrowserWidget("Point Properties", ptPs, m_PointCloudPropertiesUi, "PointCloudProps");
                        ImGui::TreePop();
                    }
                }
            }
        }

        const auto processing = GetGeometryProcessingCapabilities(reg, selected);
        const auto algorithms = ResolveGeometryProcessingEntries(reg, selected);
        if (processing.HasAny() && !algorithms.empty())
        {
            if (ImGui::CollapsingHeader("Geometry Processing", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextWrapped("Algorithms are exposed by the mathematical domain available on the selected entity's authoritative data, not by the inspector section they were loaded from.");
                DrawDomainBadges(processing.Domains);

                for (const auto& entry : algorithms)
                {
                    ImGui::SeparatorText(GeometryProcessingAlgorithmLabel(entry.Algorithm));
                    DrawDomainBadges(entry.Domains);

                    const bool open = ImGui::TreeNodeEx(GeometryProcessingAlgorithmLabel(entry.Algorithm),
                                                        ImGuiTreeNodeFlags_DefaultOpen);
                    if (!open)
                        continue;

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
                    }

                    if (m_GeometryWorkflow && entry.Algorithm != GeometryProcessingAlgorithm::KMeans)
                    {
                        if (ImGui::SmallButton((std::string("Open Dedicated Panel##")
                                             + GeometryProcessingAlgorithmLabel(entry.Algorithm)).c_str()))
                        {
                            m_GeometryWorkflow->OpenAlgorithmPanel(entry.Algorithm);
                        }
                    }

                    ImGui::TreePop();
                }
            }
        }

        // 6. Visualization — per-entity rendering mode toggles for meshes.
        if (reg.all_of<ECS::Surface::Component>(selected))
        {
            auto& sc = reg.get<ECS::Surface::Component>(selected);
            Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
            if (auto* geo = m_Engine->GetGeometryStorage().GetIfValid(sc.Geometry))
                topology = geo->GetTopology();

            const bool isTriangleMesh = (topology == Graphics::PrimitiveTopology::Triangles);
            const bool isLineMesh = (topology == Graphics::PrimitiveTopology::Lines);

            if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (isTriangleMesh)
                {
                    ImGui::Checkbox("Show Surface", &sc.Visible);

                    bool showWireframe = reg.all_of<ECS::Line::Component>(selected);
                    if (ImGui::Checkbox("Show Wireframe", &showWireframe))
                    {
                        if (showWireframe)
                            reg.emplace<ECS::Line::Component>(selected);
                        else
                            reg.remove<ECS::Line::Component>(selected);
                    }

                    if (!sc.CachedVertexColors.empty())
                        ImGui::Checkbox("Per-Vertex Colors", &sc.ShowPerVertexColors);
                    if (!sc.CachedFaceColors.empty())
                        ImGui::Checkbox("Per-Face Colors", &sc.ShowPerFaceColors);
                }
                else if (isLineMesh)
                {
                    ImGui::Checkbox("Show Edges", &sc.Visible);
                }

                bool showVertices = reg.all_of<ECS::Point::Component>(selected);
                if (ImGui::Checkbox("Show Vertices", &showVertices))
                {
                    if (showVertices)
                    {
                        auto& pt = reg.emplace<ECS::Point::Component>(selected);
                        pt.Geometry = sc.Geometry;
                    }
                    else
                        reg.remove<ECS::Point::Component>(selected);
                }

                if (auto* line = reg.try_get<ECS::Line::Component>(selected))
                {
                    ImGui::SeparatorText("Wireframe Settings");
                    ColorEdit4("Wire Color", line->Color);
                    ImGui::SliderFloat("Wire Width", &line->Width, 0.5f, 5.0f);
                    ImGui::Checkbox("Overlay##Wire", &line->Overlay);

                    if (line->HasPerEdgeColors)
                        ImGui::Checkbox("Per-Edge Colors", &line->ShowPerEdgeColors);
                }

                if (auto* pt = reg.try_get<ECS::Point::Component>(selected))
                {
                    ImGui::SeparatorText("Vertex Settings");
                    PointRenderModeCombo("Render Mode", pt->Mode);
                    ImGui::SliderFloat("Vertex Size", &pt->Size, 0.0005f, 0.05f, "%.5f",
                                       ImGuiSliderFlags_Logarithmic);
                    ColorEdit4("Vertex Color", pt->Color);
                }

                // PropertySet-driven color visualization for meshes.
                if (auto* md = reg.try_get<ECS::Mesh::Data>(selected))
                {
                    if (md->MeshRef)
                    {
                        const auto* vtxPs = &md->MeshRef->VertexProperties();
                        const auto* edgePs = &md->MeshRef->EdgeProperties();
                        const auto* halfedgePs = &md->MeshRef->HalfedgeProperties();
                        const auto* facePs = &md->MeshRef->FaceProperties();

                        if (ColorSourceWidget("Vertex Color Source", md->Visualization.VertexColors, vtxPs,
                                              "MeshVtx"))
                        {
                            md->AttributesDirty = true;
                            reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                        }
                        if (ColorSourceWidget("Edge Color Source", md->Visualization.EdgeColors, edgePs,
                                              "MeshEdge"))
                        {
                            md->AttributesDirty = true;
                            reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);
                        }
                        if (ColorSourceWidget("Face Color Source", md->Visualization.FaceColors, facePs,
                                              "MeshFace"))
                        {
                            md->AttributesDirty = true;
                            reg.emplace_or_replace<ECS::DirtyTag::FaceAttributes>(selected);
                        }

                        VectorFieldWidget(md->Visualization, vtxPs, "MeshVF");

                        if (ImGui::TreeNodeEx("Property Browser##Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            DrawPropertySetBrowserWidget("Vertex Properties", vtxPs, m_MeshVertexPropertiesUi, "MeshVertexProps");
                            DrawPropertySetBrowserWidget("Edge Properties", edgePs, m_MeshEdgePropertiesUi, "MeshEdgeProps");
                            DrawPropertySetBrowserWidget("Halfedge Properties", halfedgePs, m_MeshHalfedgePropertiesUi, "MeshHalfedgeProps");
                            DrawPropertySetBrowserWidget("Face Properties", facePs, m_MeshFacePropertiesUi, "MeshFaceProps");
                            ImGui::TreePop();
                        }

                        if (ImGui::TreeNodeEx("Spectral Modes##Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            static_cast<void>(DrawMeshSpectralWidget(*m_Engine, selected, m_MeshSpectralUi));
                            if (m_GeometryWorkflow)
                            {
                                if (ImGui::SmallButton("Open Dedicated Mesh Spectral Panel"))
                                    m_GeometryWorkflow->OpenMeshSpectralPanel();
                            }
                            ImGui::TreePop();
                        }
                    }
                }
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Select an entity to view details.");
    }

    ImGui::End();
}

} // namespace Runtime::EditorUI
