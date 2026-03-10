#pragma once

// InspectorController — Encapsulates the component property inspector
// panel (entity name, transform, surface info, graph/point-cloud/mesh
// visualization controls). Extracted from main.cpp to reduce god-object
// complexity.
//
// Owns: DrawInspectorPanel logic, ColorSourceWidget and VectorFieldWidget
// reusable ImGui helpers.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <imgui.h>
#include <entt/entity/registry.hpp>

import Runtime.Engine;
import Graphics;
import Geometry;
import ECS;
import Interface;

namespace Sandbox
{

class InspectorController
{
public:
    InspectorController() = default;
    InspectorController(const InspectorController&) = delete;
    InspectorController& operator=(const InspectorController&) = delete;

    // Called once from OnStart. Stores references for the controller's lifetime.
    void Init(Runtime::Engine& engine, entt::entity& cachedSelected)
    {
        m_Engine = &engine;
        m_CachedSelected = &cachedSelected;
    }

    void Draw()
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
                    Graphics::GeometryGpuData* geo = m_Engine->GetGeometryStorage().GetUnchecked(sc.Geometry);

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

            // 4. Graph — rendering controls for graph entities.
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
                    const char* modeNames[] = {"Flat Disc", "Surfel", "EWA Splatting", "Sphere"};
                    int modeIdx = static_cast<int>(gd.NodeRenderMode);
                    if (modeIdx < 0 || modeIdx > 3) modeIdx = 0;
                    if (ImGui::Combo("Node Render Mode", &modeIdx, modeNames, 4))
                        gd.NodeRenderMode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);
                    ImGui::SliderFloat("Node Size", &gd.DefaultNodeRadius, 0.0005f, 0.05f, "%.5f",
                                       ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Node Size Multiplier", &gd.NodeSizeMultiplier, 0.1f, 10.0f, "%.2f",
                                       ImGuiSliderFlags_Logarithmic);
                    float nc[4] = {
                        gd.DefaultNodeColor.r, gd.DefaultNodeColor.g, gd.DefaultNodeColor.b, gd.DefaultNodeColor.a
                    };
                    if (ImGui::ColorEdit4("Node Color", nc))
                        gd.DefaultNodeColor = glm::vec4(nc[0], nc[1], nc[2], nc[3]);

                    ImGui::SeparatorText("Edge Settings");
                    float ec[4] = {
                        gd.DefaultEdgeColor.r, gd.DefaultEdgeColor.g, gd.DefaultEdgeColor.b, gd.DefaultEdgeColor.a
                    };
                    if (ImGui::ColorEdit4("Edge Color", ec))
                        gd.DefaultEdgeColor = glm::vec4(ec[0], ec[1], ec[2], ec[3]);
                    ImGui::SliderFloat("Edge Width", &gd.EdgeWidth, 0.5f, 5.0f);
                    ImGui::Checkbox("Edge Overlay", &gd.EdgesOverlay);

                    // Per-edge color toggle (shown when per-edge color data exists).
                    if (gd.HasEdgeColors())
                    {
                        if (auto* line = reg.try_get<ECS::Line::Component>(selected))
                            ImGui::Checkbox("Per-Edge Colors##Graph", &line->ShowPerEdgeColors);
                    }

                    // PropertySet-driven color visualization.
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

            // 5. Point Cloud — PropertySet-backed (PointCloud::Data).
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

                    const char* modeNames[] = {"Flat Disc", "Surfel", "EWA Splatting", "Sphere"};
                    int modeIdx = static_cast<int>(pcd.RenderMode);
                    if (modeIdx < 0 || modeIdx > 3) modeIdx = 0;
                    if (ImGui::Combo("Render Mode##PCD", &modeIdx, modeNames, 4))
                        pcd.RenderMode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);

                    ImGui::SliderFloat("Default Radius##PCD", &pcd.DefaultRadius, 0.0005f, 0.1f, "%.5f",
                                       ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Size Multiplier##PCD", &pcd.SizeMultiplier, 0.1f, 10.0f, "%.2f",
                                       ImGuiSliderFlags_Logarithmic);

                    float dc[4] = {pcd.DefaultColor.r, pcd.DefaultColor.g, pcd.DefaultColor.b, pcd.DefaultColor.a};
                    if (ImGui::ColorEdit4("Default Color##PCD", dc))
                        pcd.DefaultColor = glm::vec4(dc[0], dc[1], dc[2], dc[3]);

                    // PropertySet-driven color visualization.
                    if (pcd.CloudRef)
                    {
                        const auto* ptPs = &pcd.CloudRef->PointProperties();

                        if (ColorSourceWidget("Point Color Source", pcd.Visualization.VertexColors, ptPs, "PCDVtx"))
                            reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);

                        VectorFieldWidget(pcd.Visualization, ptPs, "PCDVF");
                    }
                }
            }

            // 6. Visualization — per-entity rendering mode toggles for meshes.
            if (reg.all_of<ECS::Surface::Component>(selected))
            {
                auto& sc = reg.get<ECS::Surface::Component>(selected);
                Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
                if (auto* geo = m_Engine->GetGeometryStorage().GetUnchecked(sc.Geometry))
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

                        // Per-vertex color toggle (shown when vertex color data exists).
                        if (!sc.CachedVertexColors.empty())
                            ImGui::Checkbox("Per-Vertex Colors", &sc.ShowPerVertexColors);
                        // Per-face color toggle (shown when face color data exists).
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
                        float wc[4] = {line->Color.r, line->Color.g, line->Color.b, line->Color.a};
                        if (ImGui::ColorEdit4("Wire Color", wc))
                            line->Color = glm::vec4(wc[0], wc[1], wc[2], wc[3]);
                        ImGui::SliderFloat("Wire Width", &line->Width, 0.5f, 5.0f);
                        ImGui::Checkbox("Overlay##Wire", &line->Overlay);

                        // Per-edge color toggle (shown when per-edge data exists).
                        if (line->HasPerEdgeColors)
                            ImGui::Checkbox("Per-Edge Colors", &line->ShowPerEdgeColors);
                    }

                    if (auto* pt = reg.try_get<ECS::Point::Component>(selected))
                    {
                        ImGui::SeparatorText("Vertex Settings");
                        const char* modeNames[] = {"Flat Disc", "Surfel", "EWA Splatting"};
                        int modeIdx = static_cast<int>(pt->Mode);
                        if (modeIdx < 0 || modeIdx > 2) modeIdx = 0;
                        if (ImGui::Combo("Render Mode", &modeIdx, modeNames, 3))
                            pt->Mode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);
                        ImGui::SliderFloat("Vertex Size", &pt->Size, 0.0005f, 0.05f, "%.5f",
                                           ImGuiSliderFlags_Logarithmic);
                        float vc[4] = {pt->Color.r, pt->Color.g, pt->Color.b, pt->Color.a};
                        if (ImGui::ColorEdit4("Vertex Color", vc))
                            pt->Color = glm::vec4(vc[0], vc[1], vc[2], vc[3]);
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

private:
    Runtime::Engine* m_Engine = nullptr;
    entt::entity* m_CachedSelected = nullptr;

    // =========================================================================
    // ColorSourceWidget — reusable ImGui widget for PropertySet color source
    // selection. Shows a property selector combo, colormap picker, range
    // sliders, and binning control.
    // =========================================================================
    static bool ColorSourceWidget(const char* label, Graphics::ColorSource& src,
                                  const Geometry::PropertySet* ps, const char* suffix)
    {
        bool changed = false;
        char idBuf[128];

        ImGui::SeparatorText(label);

        // Property selector combo.
        if (ps)
        {
            auto props = Graphics::EnumerateColorableProperties(*ps);

            snprintf(idBuf, sizeof(idBuf), "Property##%s", suffix);
            const char* currentName = src.PropertyName.empty() ? "(none)" : src.PropertyName.c_str();
            if (ImGui::BeginCombo(idBuf, currentName))
            {
                if (ImGui::Selectable("(none)", src.PropertyName.empty()))
                {
                    src.PropertyName.clear();
                    changed = true;
                }
                for (const auto& p : props)
                {
                    const char* typeLabel = "";
                    switch (p.Type)
                    {
                    case Graphics::PropertyDataType::Scalar: typeLabel = " [float]";
                        break;
                    case Graphics::PropertyDataType::Vec3: typeLabel = " [vec3]";
                        break;
                    case Graphics::PropertyDataType::Vec4: typeLabel = " [vec4]";
                        break;
                    }
                    char itemLabel[256];
                    snprintf(itemLabel, sizeof(itemLabel), "%s%s", p.Name.c_str(), typeLabel);
                    if (ImGui::Selectable(itemLabel, src.PropertyName == p.Name))
                    {
                        src.PropertyName = p.Name;
                        src.AutoRange = true; // Reset auto-range on property change.
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (src.PropertyName.empty())
            return changed;

        // Colormap selector.
        snprintf(idBuf, sizeof(idBuf), "Colormap##%s", suffix);
        int mapIdx = static_cast<int>(src.Map);
        const char* mapNames[] = {"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
        if (ImGui::Combo(idBuf, &mapIdx, mapNames, 6))
        {
            src.Map = static_cast<Graphics::Colormap::Type>(mapIdx);
            changed = true;
        }

        // Auto-range checkbox.
        snprintf(idBuf, sizeof(idBuf), "Auto Range##%s", suffix);
        if (ImGui::Checkbox(idBuf, &src.AutoRange))
            changed = true;

        // Range sliders (disabled when auto-range is on).
        if (!src.AutoRange)
        {
            snprintf(idBuf, sizeof(idBuf), "Range Min##%s", suffix);
            if (ImGui::DragFloat(idBuf, &src.RangeMin, 0.01f))
                changed = true;
            snprintf(idBuf, sizeof(idBuf), "Range Max##%s", suffix);
            if (ImGui::DragFloat(idBuf, &src.RangeMax, 0.01f))
                changed = true;
        }
        else
        {
            ImGui::Text("Range: [%.4f, %.4f]", src.RangeMin, src.RangeMax);
        }

        // Bins slider.
        snprintf(idBuf, sizeof(idBuf), "Bins##%s", suffix);
        int bins = static_cast<int>(src.Bins);
        if (ImGui::SliderInt(idBuf, &bins, 0, 32, bins == 0 ? "Continuous" : "%d"))
        {
            src.Bins = static_cast<uint32_t>(std::max(0, bins));
            changed = true;
        }

        return changed;
    }

    // =========================================================================
    // VectorFieldWidget — UI for managing vector field overlays.
    // =========================================================================
    static bool VectorFieldWidget(Graphics::VisualizationConfig& config,
                                  const Geometry::PropertySet* ps, const char* suffix)
    {
        bool changed = false;
        char idBuf[128];

        ImGui::SeparatorText("Vector Fields");

        // Available vec3 properties.
        std::vector<Graphics::PropertyInfo> vecProps;
        if (ps)
            vecProps = Graphics::EnumerateVectorProperties(*ps);

        // Add new vector field.
        snprintf(idBuf, sizeof(idBuf), "Add Vector Field##%s", suffix);
        if (!vecProps.empty() && ImGui::BeginCombo(idBuf, "Add..."))
        {
            for (const auto& p : vecProps)
            {
                if (ImGui::Selectable(p.Name.c_str()))
                {
                    Graphics::VectorFieldEntry entry;
                    entry.PropertyName = p.Name;
                    config.VectorFields.push_back(std::move(entry));
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        // List existing vector fields.
        for (size_t i = 0; i < config.VectorFields.size();)
        {
            auto& vf = config.VectorFields[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::Text("%s", vf.PropertyName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                config.VectorFields.erase(config.VectorFields.begin() + static_cast<ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                continue;
            }

            ImGui::DragFloat("Scale", &vf.Scale, 0.01f, 0.001f, 100.0f);
            ImGui::SliderFloat("Width", &vf.EdgeWidth, 0.5f, 5.0f);
            float vc[4] = {vf.Color.r, vf.Color.g, vf.Color.b, vf.Color.a};
            if (ImGui::ColorEdit4("Color", vc))
                vf.Color = glm::vec4(vc[0], vc[1], vc[2], vc[3]);
            ImGui::Checkbox("Overlay", &vf.Overlay);

            // Per-vector color property selector.
            if (ps)
            {
                auto colorableProps = Graphics::EnumerateColorableProperties(*ps);
                const char* colorPreview = vf.ColorPropertyName.empty()
                                               ? "(Uniform)"
                                               : vf.ColorPropertyName.c_str();
                if (ImGui::BeginCombo("Arrow Color", colorPreview))
                {
                    if (ImGui::Selectable("(Uniform)", vf.ColorPropertyName.empty()))
                    {
                        vf.ColorPropertyName.clear();
                        changed = true;
                    }
                    for (const auto& cp : colorableProps)
                    {
                        if (ImGui::Selectable(cp.Name.c_str(), vf.ColorPropertyName == cp.Name))
                        {
                            vf.ColorPropertyName = cp.Name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                // Per-vector length property selector.
                auto scalarProps = Graphics::EnumerateScalarProperties(*ps);
                const char* lenPreview = vf.LengthPropertyName.empty()
                                             ? "(Uniform)"
                                             : vf.LengthPropertyName.c_str();
                if (ImGui::BeginCombo("Arrow Length", lenPreview))
                {
                    if (ImGui::Selectable("(Uniform)", vf.LengthPropertyName.empty()))
                    {
                        vf.LengthPropertyName.clear();
                        changed = true;
                    }
                    for (const auto& sp : scalarProps)
                    {
                        if (ImGui::Selectable(sp.Name.c_str(), vf.LengthPropertyName == sp.Name))
                        {
                            vf.LengthPropertyName = sp.Name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::PopID();
            ++i;
        }

        return changed;
    }
};

} // namespace Sandbox
