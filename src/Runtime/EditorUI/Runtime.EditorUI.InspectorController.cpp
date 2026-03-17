// Runtime.EditorUI.InspectorController — Component property inspector panel
// for entity name, transform, surface info, graph/point-cloud/mesh
// visualization controls. Uses ColorSourceWidget and VectorFieldWidget for
// PropertySet-driven color mapping.

module;

#include <array>
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <entt/entity/registry.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.PointCloudKMeans;
import Graphics;
import Geometry;
import ECS;
import Interface;

namespace Runtime::EditorUI
{
namespace
{
    [[nodiscard]] constexpr const char* GeometryDomainLabel(GeometryProcessingDomain domain) noexcept
    {
        switch (domain)
        {
        case GeometryProcessingDomain::SurfaceMesh: return "Surface Mesh";
        case GeometryProcessingDomain::MeshVertices: return "Mesh Vertices";
        case GeometryProcessingDomain::GraphVertices: return "Graph Nodes";
        case GeometryProcessingDomain::PointCloudPoints: return "Point Cloud Points";
        case GeometryProcessingDomain::None:
        default: return "None";
        }
    }

    [[nodiscard]] constexpr GeometryProcessingDomain ToUiDomain(Runtime::PointCloudKMeans::Domain domain) noexcept
    {
        switch (domain)
        {
        case Runtime::PointCloudKMeans::Domain::MeshVertices: return GeometryProcessingDomain::MeshVertices;
        case Runtime::PointCloudKMeans::Domain::GraphVertices: return GeometryProcessingDomain::GraphVertices;
        case Runtime::PointCloudKMeans::Domain::PointCloudPoints: return GeometryProcessingDomain::PointCloudPoints;
        case Runtime::PointCloudKMeans::Domain::Auto:
        default: return GeometryProcessingDomain::None;
        }
    }

    [[nodiscard]] constexpr const char* KMeansResultProperty(Runtime::PointCloudKMeans::Domain domain) noexcept
    {
        return domain == Runtime::PointCloudKMeans::Domain::PointCloudPoints
            ? "p:kmeans_color"
            : "v:kmeans_color";
    }

    void DrawDomainBadges(GeometryProcessingDomain domains)
    {
        bool first = true;
        const auto draw = [&](GeometryProcessingDomain domain)
        {
            if (!HasAnyDomain(domains, domain))
                return;
            if (!first)
                ImGui::SameLine();
            ImGui::TextDisabled("[%s]", GeometryDomainLabel(domain));
            first = false;
        };

        draw(GeometryProcessingDomain::SurfaceMesh);
        draw(GeometryProcessingDomain::MeshVertices);
        draw(GeometryProcessingDomain::GraphVertices);
        draw(GeometryProcessingDomain::PointCloudPoints);
    }

    struct KMeansStatus
    {
        bool JobPending = false;
        Geometry::KMeans::Backend LastBackend = Geometry::KMeans::Backend::CPU;
        uint32_t LastIterations = 0;
        bool LastConverged = false;
        float LastInertia = 0.0f;
        uint32_t LastMaxDistanceIndex = 0;
        double LastDurationMs = 0.0;
    };

    [[nodiscard]] std::optional<KMeansStatus> ReadKMeansStatus(const entt::registry& reg,
                                                               entt::entity entity,
                                                               Runtime::PointCloudKMeans::Domain domain)
    {
        switch (domain)
        {
        case Runtime::PointCloudKMeans::Domain::MeshVertices:
            if (const auto* mesh = reg.try_get<ECS::Mesh::Data>(entity))
            {
                return KMeansStatus{mesh->KMeansJobPending,
                                    mesh->KMeansLastBackend,
                                    mesh->KMeansLastIterations,
                                    mesh->KMeansLastConverged,
                                    mesh->KMeansLastInertia,
                                    mesh->KMeansLastMaxDistanceIndex,
                                    mesh->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::GraphVertices:
            if (const auto* graph = reg.try_get<ECS::Graph::Data>(entity))
            {
                return KMeansStatus{graph->KMeansJobPending,
                                    graph->KMeansLastBackend,
                                    graph->KMeansLastIterations,
                                    graph->KMeansLastConverged,
                                    graph->KMeansLastInertia,
                                    graph->KMeansLastMaxDistanceIndex,
                                    graph->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::PointCloudPoints:
            if (const auto* pointCloud = reg.try_get<ECS::PointCloud::Data>(entity))
            {
                return KMeansStatus{pointCloud->KMeansJobPending,
                                    pointCloud->KMeansLastBackend,
                                    pointCloud->KMeansLastIterations,
                                    pointCloud->KMeansLastConverged,
                                    pointCloud->KMeansLastInertia,
                                    pointCloud->KMeansLastMaxDistanceIndex,
                                    pointCloud->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::Auto:
        default:
            break;
        }

        return std::nullopt;
    }
}

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

                    if (ColorSourceWidget("Node Color Source", gd.Visualization.VertexColors, vtxPs,
                                          "GraphVtx"))
                        reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                    if (ColorSourceWidget("Edge Color Source", gd.Visualization.EdgeColors, edgePs,
                                          "GraphEdge"))
                        reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);

                    VectorFieldWidget(gd.Visualization, vtxPs, "GraphVF");
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
                }
            }
        }

        const auto processing = GetGeometryProcessingCapabilities(reg, selected);
        if (processing.HasAny())
        {
            if (ImGui::CollapsingHeader("Geometry Processing", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextWrapped("Algorithms are exposed by the mathematical domain available on the selected entity's authoritative data, not by the inspector section they were loaded from.");
                DrawDomainBadges(processing.Domains);

                const auto kMeansDomains = processing.Domains & GetSupportedDomains(GeometryProcessingAlgorithm::KMeans);
                ImGui::SeparatorText("Point-Set Algorithms");
                ImGui::TextUnformatted("K-Means");
                DrawDomainBadges(kMeansDomains);

                if (kMeansDomains == GeometryProcessingDomain::None)
                {
                    ImGui::TextDisabled("No compatible point-set domain is available on the selected entity.");
                }
                else
                {
                    std::array<Runtime::PointCloudKMeans::Domain, 3> kMeansDomainValues{};
                    std::array<const char*, 3> kMeansDomainLabels{};
                    int kMeansDomainCount = 0;

                    if (HasAnyDomain(kMeansDomains, GeometryProcessingDomain::MeshVertices))
                    {
                        kMeansDomainValues[kMeansDomainCount] = Runtime::PointCloudKMeans::Domain::MeshVertices;
                        kMeansDomainLabels[kMeansDomainCount++] = GeometryDomainLabel(GeometryProcessingDomain::MeshVertices);
                    }
                    if (HasAnyDomain(kMeansDomains, GeometryProcessingDomain::GraphVertices))
                    {
                        kMeansDomainValues[kMeansDomainCount] = Runtime::PointCloudKMeans::Domain::GraphVertices;
                        kMeansDomainLabels[kMeansDomainCount++] = GeometryDomainLabel(GeometryProcessingDomain::GraphVertices);
                    }
                    if (HasAnyDomain(kMeansDomains, GeometryProcessingDomain::PointCloudPoints))
                    {
                        kMeansDomainValues[kMeansDomainCount] = Runtime::PointCloudKMeans::Domain::PointCloudPoints;
                        kMeansDomainLabels[kMeansDomainCount++] = GeometryDomainLabel(GeometryProcessingDomain::PointCloudPoints);
                    }

                    m_PointSetKMeansUi.PreferredDomain = std::clamp(m_PointSetKMeansUi.PreferredDomain, 0, kMeansDomainCount - 1);
                    if (kMeansDomainCount > 1)
                    {
                        ImGui::Combo("Domain##KMeans", &m_PointSetKMeansUi.PreferredDomain,
                                     kMeansDomainLabels.data(), kMeansDomainCount);
                    }

                    const auto selectedKMeansDomain = kMeansDomainValues[m_PointSetKMeansUi.PreferredDomain];
                    const auto targetInfo = Runtime::PointCloudKMeans::DescribeTarget(*m_Engine, selected, selectedKMeansDomain);

                    ImGui::Text("Points: %zu", targetInfo.PointCount);
                    ImGui::TextDisabled("Published color property: %s", KMeansResultProperty(selectedKMeansDomain));

                    ImGui::DragInt("Clusters##ProcKMeans", &m_PointSetKMeansUi.ClusterCount, 1.0f, 1, 4096);
                    ImGui::DragInt("Max Iterations##ProcKMeans", &m_PointSetKMeansUi.MaxIterations, 1.0f, 1, 4096);
                    ImGui::DragInt("Seed##ProcKMeans", &m_PointSetKMeansUi.Seed, 1.0f, 0, std::numeric_limits<int>::max());

                    {
                        const char* initItems[] = {"Random", "Hierarchical"};
                        ImGui::Combo("Initialization##ProcKMeans", &m_PointSetKMeansUi.Initialization,
                                     initItems, IM_ARRAYSIZE(initItems));
                    }

                    if (targetInfo.SupportsCuda)
                    {
                        const char* backendItems[] = {
                            "CPU",
#ifdef INTRINSIC_HAS_CUDA
                            "CUDA"
#endif
                        };
                        m_PointSetKMeansUi.Backend = std::clamp(m_PointSetKMeansUi.Backend, 0,
                                                                IM_ARRAYSIZE(backendItems) - 1);
                        ImGui::Combo("Backend##ProcKMeans", &m_PointSetKMeansUi.Backend,
                                     backendItems, IM_ARRAYSIZE(backendItems));
                    }
                    else
                    {
                        m_PointSetKMeansUi.Backend = static_cast<int>(Geometry::KMeans::Backend::CPU);
                        ImGui::TextDisabled("Backend: CPU (CUDA path currently uses authoritative point-cloud buffers only)");
                    }

                    const bool canRunKMeans = targetInfo.IsValid() && !targetInfo.JobPending;
                    if (!canRunKMeans)
                        ImGui::BeginDisabled();

                    if (ImGui::Button("Run K-Means##GeometryProcessing"))
                    {
                        Geometry::KMeans::Params params{};
                        params.ClusterCount = static_cast<uint32_t>(std::max(m_PointSetKMeansUi.ClusterCount, 1));
                        params.MaxIterations = static_cast<uint32_t>(std::max(m_PointSetKMeansUi.MaxIterations, 1));
                        params.Seed = static_cast<uint32_t>(std::max(m_PointSetKMeansUi.Seed, 0));
                        params.Init = static_cast<Geometry::KMeans::Initialization>(m_PointSetKMeansUi.Initialization);
                        params.Compute = static_cast<Geometry::KMeans::Backend>(m_PointSetKMeansUi.Backend);
                        static_cast<void>(Runtime::PointCloudKMeans::Schedule(*m_Engine, selected, params,
                                                                              selectedKMeansDomain));
                    }

                    if (!canRunKMeans)
                        ImGui::EndDisabled();

                    if (targetInfo.SupportsCuda)
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Release Compute Buffers##GeometryProcessing"))
                            Runtime::PointCloudKMeans::ReleaseEntityBuffers(*m_Engine, selected);
                    }

                    if (const auto stats = ReadKMeansStatus(reg, selected, selectedKMeansDomain))
                    {
                        ImGui::Text("Job Pending: %s", stats->JobPending ? "Yes" : "No");
                        ImGui::Text("Last Backend: %s", stats->LastBackend == Geometry::KMeans::Backend::CUDA ? "CUDA" : "CPU");
                        ImGui::Text("Last Iterations: %u", stats->LastIterations);
                        ImGui::Text("Last Converged: %s", stats->LastConverged ? "Yes" : "No");
                        ImGui::Text("Last Inertia: %.6f", stats->LastInertia);
                        ImGui::Text("Last Max-Distance Index: %u", stats->LastMaxDistanceIndex);
                        ImGui::Text("Last Duration: %.3f ms", stats->LastDurationMs);
                    }
                }

                ImGui::SeparatorText("Surface Topology Algorithms");
                const bool hasSurfaceTopology = HasAnyDomain(processing.Domains, GeometryProcessingDomain::SurfaceMesh);
                DrawDomainBadges(hasSurfaceTopology ? GeometryProcessingDomain::SurfaceMesh
                                                    : GeometryProcessingDomain::None);

                if (!hasSurfaceTopology)
                {
                    ImGui::TextDisabled("Topology-changing surface operators require a selected surface mesh with collider-backed authority.");
                }
                else if (!m_GeometryWorkflow)
                {
                    ImGui::TextDisabled("Geometry workflow controller is unavailable.");
                }
                else
                {
                    if (ImGui::Button("Open Remeshing##GeometryProcessing"))
                        m_GeometryWorkflow->OpenAlgorithmPanel(GeometryProcessingAlgorithm::Remeshing);
                    ImGui::SameLine();
                    if (ImGui::Button("Open Simplification##GeometryProcessing"))
                        m_GeometryWorkflow->OpenAlgorithmPanel(GeometryProcessingAlgorithm::Simplification);
                    ImGui::SameLine();
                    if (ImGui::Button("Open Smoothing##GeometryProcessing"))
                        m_GeometryWorkflow->OpenAlgorithmPanel(GeometryProcessingAlgorithm::Smoothing);

                    if (ImGui::Button("Open Subdivision##GeometryProcessing"))
                        m_GeometryWorkflow->OpenAlgorithmPanel(GeometryProcessingAlgorithm::Subdivision);
                    ImGui::SameLine();
                    if (ImGui::Button("Open Repair##GeometryProcessing"))
                        m_GeometryWorkflow->OpenAlgorithmPanel(GeometryProcessingAlgorithm::Repair);
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
