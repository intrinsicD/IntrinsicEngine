// Runtime.EditorUI.GeometryWorkflowController — Geometry operator UI state,
// panel drawing, menu dispatch, and the shared ApplyGeometryOperator pipeline
// (CPU mesh -> operator -> GPU re-upload).

module;

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <span>
#include <glm/glm.hpp>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Graphics;
import Geometry;
import ECS;
import Interface;

namespace Runtime::EditorUI
{

void GeometryWorkflowController::Init(Runtime::Engine& engine, entt::entity& cachedSelected)
{
    m_Engine = &engine;
    m_CachedSelected = &cachedSelected;
}

void GeometryWorkflowController::RegisterPanelsAndMenu()
{
    Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); });
    Interface::GUI::RegisterMainMenuBar("Geometry", [this]() { DrawMenu(); });
}

// --- Selection context ---

GeometryWorkflowController::SelectionContext GeometryWorkflowController::GetSelectionContext() const
{
    SelectionContext context{};
    context.Selected = *m_CachedSelected;

    auto& reg = m_Engine->GetScene().GetRegistry();
    context.HasSelection = context.Selected != entt::null && reg.valid(context.Selected);
    context.HasSurface = context.HasSelection && reg.all_of<ECS::Surface::Component>(context.Selected);
    return context;
}

bool GeometryWorkflowController::DrawOperatorPanelHeader(const SelectionContext& context,
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

void GeometryWorkflowController::OpenWorkflowPanel()
{
    Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); });
}

void GeometryWorkflowController::OpenRemeshingPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Remeshing", [this]() { DrawRemeshingPanel(); });
}

void GeometryWorkflowController::OpenSimplificationPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Simplification", [this]() { DrawSimplificationPanel(); });
}

void GeometryWorkflowController::OpenSmoothingPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Smoothing", [this]() { DrawSmoothingPanel(); });
}

void GeometryWorkflowController::OpenSubdivisionPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Subdivision", [this]() { DrawSubdivisionPanel(); });
}

void GeometryWorkflowController::OpenRepairPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Repair", [this]() { DrawRepairPanel(); });
}

void GeometryWorkflowController::OpenWorkflowStack()
{
    OpenWorkflowPanel();
    OpenRemeshingPanel();
    OpenSimplificationPanel();
    OpenSmoothingPanel();
    OpenSubdivisionPanel();
    OpenRepairPanel();
}

// --- Core operator pipeline ---

void GeometryWorkflowController::ApplyOperator(entt::entity entity,
                                                 const std::function<void(Geometry::Halfedge::Mesh&)>& op)
{
    auto& reg = m_Engine->GetScene().GetRegistry();
    auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity);
    auto* sc = reg.try_get<ECS::Surface::Component>(entity);
    auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
    if (!collider || !collider->CollisionRef || !sc) return;

    // 1. Acquire the authoritative halfedge mesh when available. Falling back to
    // the collider triangle soup is lossy for repeated topology edits because a
    // failed AddTriangle silently drops faces and manifests as "holes" on the
    // next operator application.
    Geometry::Halfedge::Mesh mesh;
    if (meshData && meshData->MeshRef)
    {
        mesh = *meshData->MeshRef;
    }
    else if (collider->CollisionRef->SourceMesh)
    {
        mesh = *collider->CollisionRef->SourceMesh;
    }
    else
    {
        Geometry::MeshUtils::TriangleSoupBuildParams buildParams;
        buildParams.WeldVertices = true;
        buildParams.WeldEpsilon = 1e-6f;

        auto built = Geometry::MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
            collider->CollisionRef->Positions,
            collider->CollisionRef->Indices,
            collider->CollisionRef->Aux,
            buildParams);
        if (!built)
        {
            return;
        }
        mesh = std::move(*built);
    }

    if (mesh.VertexProperties().Get<glm::vec2>("v:texcoord"))
    {
        Geometry::Halfedge::Mesh::VertexAttributeTransfer uvTransfer;
        uvTransfer.Name = "v:texcoord";
        uvTransfer.Rule = Geometry::Halfedge::Mesh::VertexAttributeTransfer::Policy::Average;
        mesh.SetVertexAttributeTransferRules(std::span<const Geometry::Halfedge::Mesh::VertexAttributeTransfer>(&uvTransfer, 1));
    }
    else
    {
        mesh.ClearVertexAttributeTransferRules();
    }

    // 2. Apply operator
    op(mesh);
    mesh.GarbageCollection();

    // 3. Extract back
    std::vector<glm::vec3> newPos;
    std::vector<uint32_t> newIdx;
    std::vector<glm::vec4> newAux;
    Geometry::MeshUtils::ExtractIndexedTriangles(mesh, newPos, newIdx, &newAux);

    collider->CollisionRef->Positions = std::move(newPos);
    collider->CollisionRef->Aux = std::move(newAux);
    collider->CollisionRef->Indices = std::move(newIdx);
    collider->CollisionRef->SourceMesh = std::make_shared<Geometry::Halfedge::Mesh>(mesh);

    std::vector<glm::vec3> newNormals(collider->CollisionRef->Positions.size(), glm::vec3(0, 1, 0));
    Geometry::MeshUtils::CalculateNormals(collider->CollisionRef->Positions, collider->CollisionRef->Indices,
                                          newNormals);

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
    uploadReq.Aux = collider->CollisionRef->Aux;
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

    auto& md = reg.emplace_or_replace<ECS::Mesh::Data>(entity);
    md.MeshRef = collider->CollisionRef->SourceMesh;
    md.AttributesDirty = true;

    m_Engine->GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
}

// --- Menu ---

void GeometryWorkflowController::DrawMenu()
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

void GeometryWorkflowController::DrawWorkflowPanel()
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

void GeometryWorkflowController::DrawRemeshingPanel()
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

void GeometryWorkflowController::DrawSimplificationPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Simplification reduces triangle count while preserving overall shape. Keep this panel separate from remeshing and smoothing so decimation can be inserted wherever a workflow needs it."))
    {
        static constexpr const char* kQuadricTypes[] = {"Plane", "Triangle", "Point"};
        static constexpr const char* kProbabilisticModes[] = {"Deterministic", "Isotropic", "Covariance"};
        static constexpr const char* kResidences[] = {"Vertices", "Faces", "Vertices + Faces"};
        static constexpr const char* kPlacementPolicies[] = {
            "Keep Survivor",
            "Quadric Minimizer",
            "Best of Endpoints + Minimizer"
        };

        ImGui::DragInt("Target Faces", &m_SimplificationUi.TargetFaces, 10.0f, 10, 1000000);
        ImGui::Checkbox("Preserve Boundary", &m_SimplificationUi.PreserveBoundary);
        ImGui::DragFloat("Hausdorff Error", &m_SimplificationUi.HausdorffError,
                         0.001f, 0.0f, 10.0f, "%.4f");
        ImGui::DragFloat("Max Normal Deviation (deg)", &m_SimplificationUi.MaxNormalDeviationDeg,
                         1.0f, 0.0f, 180.0f, "%.1f");

        ImGui::SeparatorText("Quadrics");
        ImGui::Combo("Quadric Type", &m_SimplificationUi.QuadricType, kQuadricTypes, IM_ARRAYSIZE(kQuadricTypes));
        ImGui::Combo("Probabilistic Mode", &m_SimplificationUi.ProbabilisticMode, kProbabilisticModes, IM_ARRAYSIZE(kProbabilisticModes));
        ImGui::Combo("Quadric Residence", &m_SimplificationUi.Residence, kResidences, IM_ARRAYSIZE(kResidences));
        ImGui::Combo("Placement Policy", &m_SimplificationUi.PlacementPolicy, kPlacementPolicies, IM_ARRAYSIZE(kPlacementPolicies));
        ImGui::Checkbox("Average Vertex Quadrics", &m_SimplificationUi.AverageVertexQuadrics);
        ImGui::Checkbox("Average Face Quadrics", &m_SimplificationUi.AverageFaceQuadrics);

        const auto quadricType = static_cast<Geometry::Simplification::QuadricType>(m_SimplificationUi.QuadricType);
        const auto probabilisticMode = static_cast<Geometry::Simplification::QuadricProbabilisticMode>(m_SimplificationUi.ProbabilisticMode);

        if (quadricType == Geometry::Simplification::QuadricType::Point
            && probabilisticMode != Geometry::Simplification::QuadricProbabilisticMode::Deterministic)
        {
            ImGui::TextDisabled("Point quadrics currently use deterministic point-fit energy only.");
        }

        if (quadricType != Geometry::Simplification::QuadricType::Point
            && probabilisticMode == Geometry::Simplification::QuadricProbabilisticMode::Isotropic)
        {
            ImGui::DragFloat("Position StdDev", &m_SimplificationUi.PositionStdDev, 0.001f, 0.0f, 10.0f, "%.5f");
            if (quadricType == Geometry::Simplification::QuadricType::Plane)
            {
                ImGui::DragFloat("Normal StdDev", &m_SimplificationUi.NormalStdDev, 0.001f, 0.0f, 10.0f, "%.5f");
            }
        }
        else if (quadricType != Geometry::Simplification::QuadricType::Point
                 && probabilisticMode == Geometry::Simplification::QuadricProbabilisticMode::Covariance)
        {
            if (quadricType == Geometry::Simplification::QuadricType::Plane)
            {
                ImGui::InputText("Face Position Covariance", m_SimplificationUi.FacePositionCovarianceProperty,
                                 IM_ARRAYSIZE(m_SimplificationUi.FacePositionCovarianceProperty));
                ImGui::InputText("Face Normal Covariance", m_SimplificationUi.FaceNormalCovarianceProperty,
                                 IM_ARRAYSIZE(m_SimplificationUi.FaceNormalCovarianceProperty));
            }
            else if (quadricType == Geometry::Simplification::QuadricType::Triangle)
            {
                ImGui::InputText("Vertex Position Covariance", m_SimplificationUi.VertexPositionCovarianceProperty,
                                 IM_ARRAYSIZE(m_SimplificationUi.VertexPositionCovarianceProperty));
            }
            else
            {
                ImGui::TextDisabled("Point quadrics are deterministic; covariance inputs are ignored.");
            }
        }

        if (ImGui::Button("Run QEM Simplification"))
        {
            const auto ui = m_SimplificationUi;
            ApplyOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh)
            {
                Geometry::Simplification::SimplificationParams params;
                params.TargetFaces = static_cast<std::size_t>(std::max(ui.TargetFaces, 0));
                params.PreserveBoundary = ui.PreserveBoundary;
                params.HausdorffError = ui.HausdorffError;
                params.MaxNormalDeviationDegrees = ui.MaxNormalDeviationDeg;
                params.Quadric.Type = static_cast<Geometry::Simplification::QuadricType>(ui.QuadricType);
                params.Quadric.ProbabilisticMode = static_cast<Geometry::Simplification::QuadricProbabilisticMode>(ui.ProbabilisticMode);
                params.Quadric.Residence = static_cast<Geometry::Simplification::QuadricResidence>(ui.Residence);
                params.Quadric.PlacementPolicy = static_cast<Geometry::Simplification::CollapsePlacementPolicy>(ui.PlacementPolicy);
                params.Quadric.AverageVertexQuadrics = ui.AverageVertexQuadrics;
                params.Quadric.AverageFaceQuadrics = ui.AverageFaceQuadrics;
                params.Quadric.PositionStdDev = ui.PositionStdDev;
                params.Quadric.NormalStdDev = ui.NormalStdDev;
                params.Quadric.VertexPositionCovarianceProperty = ui.VertexPositionCovarianceProperty;
                params.Quadric.FacePositionCovarianceProperty = ui.FacePositionCovarianceProperty;
                params.Quadric.FaceNormalCovarianceProperty = ui.FaceNormalCovarianceProperty;
                if (params.Quadric.Type == Geometry::Simplification::QuadricType::Point)
                {
                    params.Quadric.ProbabilisticMode = Geometry::Simplification::QuadricProbabilisticMode::Deterministic;
                }
                static_cast<void>(Geometry::Simplification::Simplify(mesh, params));
            });
        }
    }
}

void GeometryWorkflowController::DrawSmoothingPanel()
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

void GeometryWorkflowController::DrawSubdivisionPanel()
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

void GeometryWorkflowController::DrawRepairPanel()
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

} // namespace Runtime::EditorUI
