#pragma once

// GeometryWorkflowController — Encapsulates geometry operator UI state,
// panel drawing, menu dispatch, and the shared ApplyGeometryOperator
// pipeline. Extracted from main.cpp to reduce god-object complexity.
//
// Owns: per-operator UI state structs, selection context helpers, panel
// draw/open logic, and the CPU mesh -> operator -> GPU re-upload pipeline.

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Graphics;
import Geometry;
import ECS;
import Interface;

namespace Sandbox
{

class GeometryWorkflowController
{
public:
    GeometryWorkflowController() = default;
    GeometryWorkflowController(const GeometryWorkflowController&) = delete;
    GeometryWorkflowController& operator=(const GeometryWorkflowController&) = delete;

    // Called once from OnStart. Stores references for the controller's lifetime.
    void Init(Runtime::Engine& engine, entt::entity& cachedSelected)
    {
        m_Engine = &engine;
        m_CachedSelected = &cachedSelected;
    }

    // Register initial panels and menu bar. Called once from OnStart.
    void RegisterPanelsAndMenu()
    {
        Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); });
        Interface::GUI::RegisterMainMenuBar("Geometry", [this]() { DrawMenu(); });
    }

private:
    Runtime::Engine* m_Engine = nullptr;
    entt::entity* m_CachedSelected = nullptr;

    // --- Per-operator UI state ---
    struct RemeshingUiState
    {
        float TargetLength = 0.05f;
        int Iterations = 5;
        bool PreserveBoundary = true;
    };

    struct SimplificationUiState
    {
        int TargetFaces = 1000;
        bool PreserveBoundary = true;
    };

    struct SmoothingUiState
    {
        int Iterations = 10;
        float Lambda = 0.5f;
        bool PreserveBoundary = true;
    };

    struct SubdivisionUiState
    {
        int Iterations = 1;
    };

    RemeshingUiState m_RemeshingUi{};
    SimplificationUiState m_SimplificationUi{};
    SmoothingUiState m_SmoothingUi{};
    SubdivisionUiState m_SubdivisionUi{};

    // --- Selection context ---
    struct SelectionContext
    {
        entt::entity Selected = entt::null;
        bool HasSelection = false;
        bool HasSurface = false;
    };

    [[nodiscard]] SelectionContext GetSelectionContext() const
    {
        SelectionContext context{};
        context.Selected = *m_CachedSelected;

        auto& reg = m_Engine->GetScene().GetRegistry();
        context.HasSelection = context.Selected != entt::null && reg.valid(context.Selected);
        context.HasSurface = context.HasSelection && reg.all_of<ECS::Surface::Component>(context.Selected);
        return context;
    }

    [[nodiscard]] static bool DrawOperatorPanelHeader(const SelectionContext& context,
                                                      const char* description)
    {
        if (description != nullptr && description[0] != '\0')
        {
            ImGui::TextWrapped("%s", description);
            ImGui::Spacing();
        }

        if (!context.HasSelection)
        {
            ImGui::TextDisabled("Select an entity to process geometry.");
            return false;
        }

        ImGui::Text("Selected Entity: %u",
                    static_cast<uint32_t>(static_cast<entt::id_type>(context.Selected)));

        if (!context.HasSurface)
        {
            ImGui::TextDisabled("Selected entity does not have a Surface component.");
            return false;
        }

        ImGui::TextDisabled(
            "Operators apply in sequence to the selected surface mesh, so panels can be mixed into one workflow.");
        ImGui::Separator();
        return true;
    }

    // --- Panel openers ---
    void OpenWorkflowPanel()
    {
        Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); });
    }

    void OpenRemeshingPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Remeshing", [this]() { DrawRemeshingPanel(); });
    }

    void OpenSimplificationPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Simplification", [this]() { DrawSimplificationPanel(); });
    }

    void OpenSmoothingPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Smoothing", [this]() { DrawSmoothingPanel(); });
    }

    void OpenSubdivisionPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Subdivision", [this]() { DrawSubdivisionPanel(); });
    }

    void OpenRepairPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Repair", [this]() { DrawRepairPanel(); });
    }

    void OpenWorkflowStack()
    {
        OpenWorkflowPanel();
        OpenRemeshingPanel();
        OpenSimplificationPanel();
        OpenSmoothingPanel();
        OpenSubdivisionPanel();
        OpenRepairPanel();
    }

    // --- Core operator pipeline ---
    void ApplyOperator(entt::entity entity, const std::function<void(Geometry::Halfedge::Mesh&)>& op)
    {
        auto& reg = m_Engine->GetScene().GetRegistry();
        auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity);
        auto* sc = reg.try_get<ECS::Surface::Component>(entity);
        if (!collider || !collider->CollisionRef || !sc) return;

        // 1. Build Halfedge::Mesh from CPU data
        Geometry::Halfedge::Mesh mesh;
        std::vector<Geometry::VertexHandle> vhs(collider->CollisionRef->Positions.size());
        for (size_t i = 0; i < collider->CollisionRef->Positions.size(); ++i)
        {
            vhs[i] = mesh.AddVertex(collider->CollisionRef->Positions[i]);
        }
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            (void)mesh.AddTriangle(vhs[collider->CollisionRef->Indices[i]],
                                   vhs[collider->CollisionRef->Indices[i + 1]],
                                   vhs[collider->CollisionRef->Indices[i + 2]]);
        }

        // 2. Apply operator
        op(mesh);
        mesh.GarbageCollection();

        // 3. Extract back
        std::vector<glm::vec3> newPos;
        std::vector<uint32_t> newIdx;

        newPos.reserve(mesh.VertexCount());
        std::vector<uint32_t> vMap(mesh.VerticesSize(), 0);
        uint32_t currentIdx = 0;
        for (size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsDeleted(v))
            {
                vMap[i] = currentIdx++;
                newPos.push_back(mesh.Position(v));
            }
        }

        newIdx.reserve(mesh.FaceCount() * 3);
        for (size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsDeleted(f))
            {
                auto h0 = mesh.Halfedge(f);
                auto h1 = mesh.NextHalfedge(h0);
                auto h2 = mesh.NextHalfedge(h1);

                newIdx.push_back(vMap[mesh.ToVertex(h0).Index]);
                newIdx.push_back(vMap[mesh.ToVertex(h1).Index]);
                newIdx.push_back(vMap[mesh.ToVertex(h2).Index]);
            }
        }

        collider->CollisionRef->Positions = std::move(newPos);
        collider->CollisionRef->Indices = std::move(newIdx);

        std::vector<glm::vec3> newNormals(collider->CollisionRef->Positions.size(), glm::vec3(0, 1, 0));
        Geometry::MeshUtils::CalculateNormals(collider->CollisionRef->Positions, collider->CollisionRef->Indices,
                                              newNormals);

        std::vector<glm::vec4> newAux(collider->CollisionRef->Positions.size(), glm::vec4(0.0f));
        Geometry::MeshUtils::GenerateUVs(collider->CollisionRef->Positions, newAux);

        auto aabbs = Geometry::Convert(collider->CollisionRef->Positions);
        collider->CollisionRef->LocalAABB = Geometry::Union(aabbs);

        std::vector<Geometry::AABB> primitiveBounds;
        primitiveBounds.reserve(collider->CollisionRef->Indices.size() / 3);
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            const uint32_t i0 = collider->CollisionRef->Indices[i];
            const uint32_t i1 = collider->CollisionRef->Indices[i + 1];
            const uint32_t i2 = collider->CollisionRef->Indices[i + 2];
            auto aabb = Geometry::AABB{
                collider->CollisionRef->Positions[i0], collider->CollisionRef->Positions[i0]
            };
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i1]);
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        static_cast<void>(collider->CollisionRef->LocalOctree.Build(
            primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));

        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collider->CollisionRef->Positions;
        uploadReq.Indices = collider->CollisionRef->Indices;
        uploadReq.Normals = newNormals;
        uploadReq.Aux = newAux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            m_Engine->GetDeviceShared(), m_Engine->GetGraphicsBackend().GetTransferManager(), uploadReq,
            &m_Engine->GetGeometryStorage());

        auto oldHandle = sc->Geometry;
        sc->Geometry = m_Engine->GetGeometryStorage().Add(std::move(gpuData));

        if (oldHandle.IsValid())
        {
            m_Engine->GetGeometryStorage().Remove(oldHandle, m_Engine->GetDevice().GetGlobalFrameNumber());
        }

        sc->GpuSlot = ECS::Surface::Component::kInvalidSlot;
        reg.emplace_or_replace<ECS::Components::Transform::WorldUpdatedTag>(entity);

        if (auto* ev = reg.try_get<ECS::MeshEdgeView::Component>(entity))
            ev->Dirty = true;
        if (auto* pv = reg.try_get<ECS::MeshVertexView::Component>(entity))
            pv->Dirty = true;

        // Retain the mesh with PropertySets for visualization (color mapping,
        // isolines, vector fields). Copy since `mesh` is local and will be
        // destroyed after this scope.
        auto& md = reg.emplace_or_replace<ECS::Mesh::Data>(entity);
        md.MeshRef = std::make_shared<Geometry::Halfedge::Mesh>(std::move(mesh));
        md.AttributesDirty = true;

        m_Engine->GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
    }

    // --- Menu ---
    void DrawMenu()
    {
        if (!ImGui::BeginMenu("Geometry"))
            return;

        if (ImGui::BeginMenu("Workflow"))
        {
            if (ImGui::MenuItem("Overview"))
                OpenWorkflowPanel();
            if (ImGui::MenuItem("Open Workflow Stack"))
                OpenWorkflowStack();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Remeshing"))
        {
            if (ImGui::MenuItem("Isotropic Remeshing"))
                OpenRemeshingPanel();
            if (ImGui::MenuItem("Adaptive Remeshing"))
                OpenRemeshingPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Simplification"))
        {
            if (ImGui::MenuItem("QEM Simplification"))
                OpenSimplificationPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Smoothing"))
        {
            if (ImGui::MenuItem("Uniform Laplacian"))
                OpenSmoothingPanel();
            if (ImGui::MenuItem("Cotan Laplacian"))
                OpenSmoothingPanel();
            if (ImGui::MenuItem("Taubin Smoothing"))
                OpenSmoothingPanel();
            if (ImGui::MenuItem("Implicit Smoothing"))
                OpenSmoothingPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Subdivision"))
        {
            if (ImGui::MenuItem("Loop Subdivision"))
                OpenSubdivisionPanel();
            if (ImGui::MenuItem("Catmull-Clark Subdivision"))
                OpenSubdivisionPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Repair"))
        {
            if (ImGui::MenuItem("Mesh Repair"))
                OpenRepairPanel();
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    // --- Panel draw methods ---
    void DrawWorkflowPanel()
    {
        const auto context = GetSelectionContext();

        ImGui::TextWrapped(
            "Geometry tools are organized by workflow and algorithm family. Open only the panels you need, or open the full stack when chaining remeshing, smoothing, simplification, subdivision, and repair together.");
        ImGui::SeparatorText("Selection");
        if (context.HasSelection)
        {
            ImGui::Text("Selected Entity: %u",
                        static_cast<uint32_t>(static_cast<entt::id_type>(context.Selected)));
            ImGui::TextDisabled(context.HasSurface
                                    ? "Surface mesh detected. Geometry operators are available."
                                    : "Selected entity does not have a Surface component.");
        }
        else
        {
            ImGui::TextDisabled("Select an entity with a surface mesh to apply geometry operators.");
        }

        ImGui::SeparatorText("Open Panels");
        if (ImGui::Button("Open Workflow Stack"))
            OpenWorkflowStack();
        if (ImGui::Button("Open Remeshing"))
            OpenRemeshingPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Simplification"))
            OpenSimplificationPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Smoothing"))
            OpenSmoothingPanel();
        if (ImGui::Button("Open Subdivision"))
            OpenSubdivisionPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Repair"))
            OpenRepairPanel();

        ImGui::SeparatorText("Approach Map");
        ImGui::BulletText("Remeshing: Isotropic and Adaptive remeshing share the same workflow surface.");
        ImGui::BulletText(
            "Smoothing: Uniform, Cotan, Taubin, and Implicit smoothing stay grouped together for side-by-side comparison.");
        ImGui::BulletText("Simplification, Subdivision, and Repair remain focused single-purpose panels.");
        ImGui::BulletText(
            "Panels compose naturally because each operator rewrites the selected surface mesh in place.");
    }

    void DrawRemeshingPanel()
    {
        const auto context = GetSelectionContext();
        if (DrawOperatorPanelHeader(context,
                                    "Use remeshing to regularize edge lengths. Isotropic remeshing targets a uniform metric; adaptive remeshing keeps room for size-field-driven workflows while still sharing the same mesh pipeline."))
        {
            ImGui::DragFloat("Target Length", &m_RemeshingUi.TargetLength, 0.01f, 0.001f, 10.0f);
            ImGui::DragInt("Iterations", &m_RemeshingUi.Iterations, 1.0f, 1, 20);
            ImGui::Checkbox("Preserve Boundary", &m_RemeshingUi.PreserveBoundary);

            ImGui::SeparatorText("Approaches");
            ImGui::TextDisabled("Uniform target edge length for evenly distributed tessellation.");
            if (ImGui::Button("Run Isotropic Remeshing"))
            {
                const auto ui = m_RemeshingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Remeshing::RemeshingParams params;
                    params.TargetLength = ui.TargetLength;
                    params.Iterations = ui.Iterations;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Remeshing::Remesh(mesh, params));
                });
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Curvature-aware min/max edge lengths for adaptive workflows.");
            if (ImGui::Button("Run Adaptive Remeshing"))
            {
                const auto ui = m_RemeshingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
                    params.MinEdgeLength = ui.TargetLength * 0.5f;
                    params.MaxEdgeLength = ui.TargetLength * 2.0f;
                    params.Iterations = ui.Iterations;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params));
                });
            }
        }
    }

    void DrawSimplificationPanel()
    {
        const auto context = GetSelectionContext();
        if (DrawOperatorPanelHeader(context,
                                    "Simplification reduces triangle count while preserving overall shape. Keep this panel separate from remeshing and smoothing so decimation can be inserted wherever a workflow needs it."))
        {
            ImGui::DragInt("Target Faces", &m_SimplificationUi.TargetFaces, 10.0f, 10, 1000000);
            ImGui::Checkbox("Preserve Boundary", &m_SimplificationUi.PreserveBoundary);
            if (ImGui::Button("Run QEM Simplification"))
            {
                const auto ui = m_SimplificationUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Simplification::SimplificationParams params;
                    params.TargetFaces = ui.TargetFaces;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Simplification::Simplify(mesh, params));
                });
            }
        }
    }

    void DrawSmoothingPanel()
    {
        const auto context = GetSelectionContext();
        if (DrawOperatorPanelHeader(context,
                                    "Smoothing approaches stay together so you can compare differential operators without hunting through unrelated UI. They remain independently accessible from the Geometry menu and can still be chained after remeshing or before subdivision."))
        {
            ImGui::DragInt("Iterations", &m_SmoothingUi.Iterations, 1.0f, 1, 100);
            ImGui::DragFloat("Lambda", &m_SmoothingUi.Lambda, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Preserve Boundary", &m_SmoothingUi.PreserveBoundary);

            ImGui::SeparatorText("Approaches");
            if (ImGui::Button("Run Uniform Laplacian"))
            {
                const auto ui = m_SmoothingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Smoothing::SmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::UniformLaplacian(mesh, params));
                });
            }
            if (ImGui::Button("Run Cotan Laplacian"))
            {
                const auto ui = m_SmoothingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Smoothing::SmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::CotanLaplacian(mesh, params));
                });
            }
            if (ImGui::Button("Run Taubin Smoothing"))
            {
                const auto ui = m_SmoothingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Smoothing::TaubinParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::Taubin(mesh, params));
                });
            }
            if (ImGui::Button("Run Implicit Smoothing"))
            {
                const auto ui = m_SmoothingUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Smoothing::ImplicitSmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::ImplicitLaplacian(mesh, params));
                });
            }
        }
    }

    void DrawSubdivisionPanel()
    {
        const auto context = GetSelectionContext();
        if (DrawOperatorPanelHeader(context,
                                    "Subdivision is kept distinct from smoothing and remeshing because it changes topology with a refinement-first workflow. Open it alongside repair or smoothing when building higher-resolution assets."))
        {
            ImGui::DragInt("Iterations", &m_SubdivisionUi.Iterations, 1.0f, 1, 5);
            if (ImGui::Button("Run Loop Subdivision"))
            {
                const auto ui = m_SubdivisionUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Halfedge::Mesh out;
                    Geometry::Subdivision::SubdivisionParams params;
                    params.Iterations = ui.Iterations;
                    if (Geometry::Subdivision::Subdivide(mesh, out, params))
                        mesh = std::move(out);
                });
            }
            if (ImGui::Button("Run Catmull-Clark Subdivision"))
            {
                const auto ui = m_SubdivisionUi;
                ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
                {
                    Geometry::Halfedge::Mesh out;
                    Geometry::CatmullClark::SubdivisionParams params;
                    params.Iterations = ui.Iterations;
                    if (Geometry::CatmullClark::Subdivide(mesh, out, params))
                        mesh = std::move(out);
                });
            }
        }
    }

    void DrawRepairPanel()
    {
        const auto context = GetSelectionContext();
        if (DrawOperatorPanelHeader(context,
                                    "Repair stays as a standalone cleanup pass so it can be inserted before or after heavier operators without dragging the rest of the geometry UI along."))
        {
            if (ImGui::Button("Run Mesh Repair"))
            {
                ApplyOperator(context.Selected, [](Geometry::Halfedge::Mesh& mesh)
                {
                    static_cast<void>(Geometry::MeshRepair::Repair(mesh));
                });
            }
        }
    }
};

} // namespace Sandbox
