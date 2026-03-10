#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <filesystem>
#include <cmath>
#include <string>
#include <array>
#include <limits>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <tiny_gltf.h>
#include <unordered_set>

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.RenderOrchestrator;
import Runtime.SelectionModule;
import Runtime.Selection;
import Runtime.EditorUI;

import Core.Logging;
import Core.Filesystem;
import Core.Assets;
import Core.FrameGraph;
import Core.FeatureRegistry;
import Core.Hash;
import Core.Input;
import Graphics;
import Geometry;
import ECS;
import RHI;
import Interface;

using namespace Core;
using namespace Runtime;

namespace
{
    struct HiddenEditorEntityTag {};
    struct RetainedLineOverlaySlot
    {
        entt::entity Entity = entt::null;
        Geometry::GeometryHandle Geometry{};
    };

    [[nodiscard]] bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (std::abs(a[c][r] - b[c][r]) > eps)
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool Vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
    }

    [[nodiscard]] bool OctreeSettingsEqual(const Graphics::OctreeDebugDrawSettings& a,
                                           const Graphics::OctreeDebugDrawSettings& b)
    {
        return a.Enabled == b.Enabled &&
               a.Overlay == b.Overlay &&
               a.ColorByDepth == b.ColorByDepth &&
               a.MaxDepth == b.MaxDepth &&
               a.LeafOnly == b.LeafOnly &&
               a.DrawInternal == b.DrawInternal &&
               a.OccupiedOnly == b.OccupiedOnly &&
               std::abs(a.Alpha - b.Alpha) <= 1e-4f &&
               Vec3NearlyEqual(a.BaseColor, b.BaseColor);
    }

    [[nodiscard]] glm::vec3 DepthRamp(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        constexpr std::array<glm::vec3, 5> k = {
            glm::vec3{0.267f, 0.005f, 0.329f},
            glm::vec3{0.230f, 0.322f, 0.546f},
            glm::vec3{0.128f, 0.566f, 0.550f},
            glm::vec3{0.369f, 0.788f, 0.382f},
            glm::vec3{0.993f, 0.906f, 0.144f},
        };

        const float x = t * 4.0f;
        const int i0 = std::clamp(static_cast<int>(x), 0, 3);
        const int i1 = i0 + 1;
        const float alpha = x - static_cast<float>(i0);
        return k[i0] * (1.0f - alpha) + k[i1] * alpha;
    }

    [[nodiscard]] uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha)
    {
        return Graphics::DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, alpha);
    }

    void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                      glm::vec3& outLo, glm::vec3& outHi)
    {
        glm::vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
            {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
            {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
        };

        outLo = glm::vec3(std::numeric_limits<float>::max());
        outHi = glm::vec3(std::numeric_limits<float>::lowest());

        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 transformed = glm::vec3(m * glm::vec4(corner, 1.0f));
            outLo = glm::min(outLo, transformed);
            outHi = glm::max(outHi, transformed);
        }
    }
}

// --- The Application Class ---
class SandboxApp : public Engine
{
public:
    explicit SandboxApp(const Runtime::EngineConfig& config = {"Sandbox", 1600, 900})
        : Engine(config)
    {
    }

    struct GeometryRemeshingUiState
    {
        float TargetLength = 0.05f;
        int Iterations = 5;
        bool PreserveBoundary = true;
    };

    struct GeometrySimplificationUiState
    {
        int TargetFaces = 1000;
        bool PreserveBoundary = true;
    };

    struct GeometrySmoothingUiState
    {
        int Iterations = 10;
        float Lambda = 0.5f;
        bool PreserveBoundary = true;
    };

    struct GeometrySubdivisionUiState
    {
        int Iterations = 1;
    };

    struct GeometrySelectionContext
    {
        entt::entity Selected = entt::null;
        bool HasSelection = false;
        bool HasSurface = false;
    };

    // Resources
    Assets::AssetHandle m_DuckModel{};
    Assets::AssetHandle m_DuckTexture{};

    Assets::AssetHandle m_DuckMaterialHandle;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;
    entt::entity m_DuckEntity = entt::null;

    // Camera State
    entt::entity m_CameraEntity = entt::null;
    Graphics::CameraComponent m_Camera;

    // Editor / Selection Settings
    entt::entity m_CachedSelectedEntity = entt::null; // Updated by SelectionChanged sink — avoids per-frame polling.
    int m_SelectMouseButton = 0; // 0=LMB, 1=RMB, 2=MMB. Default: LMB for scene interaction; camera orbit is RMB-only.
    GeometryRemeshingUiState m_GeometryRemeshingUi{};
    GeometrySimplificationUiState m_GeometrySimplificationUi{};
    GeometrySmoothingUiState m_GeometrySmoothingUi{};
    GeometrySubdivisionUiState m_GeometrySubdivisionUi{};

    // Octree Debug Visualization Settings (shared between UI and OnUpdate)
    Graphics::OctreeDebugDrawSettings m_OctreeDebugSettings{};
    bool m_DrawSelectedColliderOctree = false;
    entt::entity m_OctreeOverlayEntity = entt::null;
    Geometry::GeometryHandle m_OctreeOverlayGeometry{};
    entt::entity m_OctreeOverlaySourceEntity = entt::null;
    Graphics::OctreeDebugDrawSettings m_CachedOctreeOverlaySettings{};
    glm::mat4 m_CachedOctreeOverlayWorld{1.0f};
    Geometry::AABB m_CachedOctreeOverlayLocalAABB{};
    bool m_HasCachedOctreeOverlayAabb = false;

    Graphics::BoundingDebugDrawSettings m_BoundsDebugSettings{};
    bool m_DrawSelectedColliderBounds = false;
    RetainedLineOverlaySlot m_BoundsOverlay{};
    entt::entity m_BoundsOverlaySourceEntity = entt::null;
    Graphics::BoundingDebugDrawSettings m_CachedBoundsOverlaySettings{};
    glm::mat4 m_CachedBoundsOverlayWorld{1.0f};
    Geometry::AABB m_CachedBoundsOverlayLocalAabb{};
    bool m_HasCachedBoundsOverlayAabb = false;

    Graphics::KDTreeDebugDrawSettings m_KDTreeDebugSettings{};
    bool m_DrawSelectedColliderKDTree = false;
    RetainedLineOverlaySlot m_KDTreeOverlay{};
    entt::entity m_KDTreeOverlaySourceEntity = entt::null;
    Graphics::KDTreeDebugDrawSettings m_CachedKDTreeOverlaySettings{};
    glm::mat4 m_CachedKDTreeOverlayWorld{1.0f};

    Graphics::BVHDebugDrawSettings m_BVHDebugSettings{};
    bool m_DrawSelectedColliderBVH = false;
    RetainedLineOverlaySlot m_BVHOverlay{};
    entt::entity m_BVHOverlaySourceEntity = entt::null;
    Graphics::BVHDebugDrawSettings m_CachedBVHOverlaySettings{};
    glm::mat4 m_CachedBVHOverlayWorld{1.0f};
    size_t m_CachedBVHOverlayPositionCount = 0;
    size_t m_CachedBVHOverlayIndexCount = 0;

    // Transform Gizmo
    Graphics::TransformGizmo m_Gizmo;

    bool m_DrawSelectedColliderConvexHull = false;
    bool m_DrawSelectedColliderContacts = false;
    bool m_ContactDebugOverlay = true;
    float m_ContactNormalScale = 0.3f;
    float m_ContactPointRadius = 0.03f;
    RetainedLineOverlaySlot m_ContactOverlay{};

    Geometry::KDTree m_SelectedColliderKDTree{};
    entt::entity m_SelectedKDTreeEntity = entt::null;

    Graphics::ConvexHullDebugDrawSettings m_ConvexHullDebugSettings{};
    Geometry::Halfedge::Mesh m_SelectedColliderHullMesh{};
    entt::entity m_SelectedHullEntity = entt::null;
    RetainedLineOverlaySlot m_ConvexHullOverlay{};
    entt::entity m_ConvexHullOverlaySourceEntity = entt::null;
    Graphics::ConvexHullDebugDrawSettings m_CachedConvexHullOverlaySettings{};
    glm::mat4 m_CachedConvexHullOverlayWorld{1.0f};

    [[nodiscard]] GeometrySelectionContext GetGeometrySelectionContext()
    {
        GeometrySelectionContext context{};
        context.Selected = m_CachedSelectedEntity;

        auto& reg = GetScene().GetRegistry();
        context.HasSelection = context.Selected != entt::null && reg.valid(context.Selected);
        context.HasSurface = context.HasSelection && reg.all_of<ECS::Surface::Component>(context.Selected);
        return context;
    }

    [[nodiscard]] bool DrawGeometryOperatorPanelHeader(const GeometrySelectionContext& context,
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

        ImGui::TextDisabled("Operators apply in sequence to the selected surface mesh, so panels can be mixed into one workflow.");
        ImGui::Separator();
        return true;
    }

    void OpenGeometryWorkflowPanel()
    {
        Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawGeometryWorkflowPanel(); });
    }

    void OpenGeometryRemeshingPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Remeshing", [this]() { DrawGeometryRemeshingPanel(); });
    }

    void OpenGeometrySimplificationPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Simplification", [this]() { DrawGeometrySimplificationPanel(); });
    }

    void OpenGeometrySmoothingPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Smoothing", [this]() { DrawGeometrySmoothingPanel(); });
    }

    void OpenGeometrySubdivisionPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Subdivision", [this]() { DrawGeometrySubdivisionPanel(); });
    }

    void OpenGeometryRepairPanel()
    {
        Interface::GUI::RegisterPanel("Geometry - Repair", [this]() { DrawGeometryRepairPanel(); });
    }

    void OpenGeometryWorkflowStack()
    {
        OpenGeometryWorkflowPanel();
        OpenGeometryRemeshingPanel();
        OpenGeometrySimplificationPanel();
        OpenGeometrySmoothingPanel();
        OpenGeometrySubdivisionPanel();
        OpenGeometryRepairPanel();
    }

    void DrawGeometryMenu()
    {
        if (!ImGui::BeginMenu("Geometry"))
            return;

        if (ImGui::BeginMenu("Workflow"))
        {
            if (ImGui::MenuItem("Overview"))
                OpenGeometryWorkflowPanel();
            if (ImGui::MenuItem("Open Workflow Stack"))
                OpenGeometryWorkflowStack();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Remeshing"))
        {
            if (ImGui::MenuItem("Isotropic Remeshing"))
                OpenGeometryRemeshingPanel();
            if (ImGui::MenuItem("Adaptive Remeshing"))
                OpenGeometryRemeshingPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Simplification"))
        {
            if (ImGui::MenuItem("QEM Simplification"))
                OpenGeometrySimplificationPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Smoothing"))
        {
            if (ImGui::MenuItem("Uniform Laplacian"))
                OpenGeometrySmoothingPanel();
            if (ImGui::MenuItem("Cotan Laplacian"))
                OpenGeometrySmoothingPanel();
            if (ImGui::MenuItem("Taubin Smoothing"))
                OpenGeometrySmoothingPanel();
            if (ImGui::MenuItem("Implicit Smoothing"))
                OpenGeometrySmoothingPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Subdivision"))
        {
            if (ImGui::MenuItem("Loop Subdivision"))
                OpenGeometrySubdivisionPanel();
            if (ImGui::MenuItem("Catmull-Clark Subdivision"))
                OpenGeometrySubdivisionPanel();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Repair"))
        {
            if (ImGui::MenuItem("Mesh Repair"))
                OpenGeometryRepairPanel();
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    void DrawGeometryWorkflowPanel()
    {
        const auto context = GetGeometrySelectionContext();

        ImGui::TextWrapped("Geometry tools are organized by workflow and algorithm family. Open only the panels you need, or open the full stack when chaining remeshing, smoothing, simplification, subdivision, and repair together.");
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
            OpenGeometryWorkflowStack();
        if (ImGui::Button("Open Remeshing"))
            OpenGeometryRemeshingPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Simplification"))
            OpenGeometrySimplificationPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Smoothing"))
            OpenGeometrySmoothingPanel();
        if (ImGui::Button("Open Subdivision"))
            OpenGeometrySubdivisionPanel();
        ImGui::SameLine();
        if (ImGui::Button("Open Repair"))
            OpenGeometryRepairPanel();

        ImGui::SeparatorText("Approach Map");
        ImGui::BulletText("Remeshing: Isotropic and Adaptive remeshing share the same workflow surface.");
        ImGui::BulletText("Smoothing: Uniform, Cotan, Taubin, and Implicit smoothing stay grouped together for side-by-side comparison.");
        ImGui::BulletText("Simplification, Subdivision, and Repair remain focused single-purpose panels.");
        ImGui::BulletText("Panels compose naturally because each operator rewrites the selected surface mesh in place.");
    }


    bool EnsureSelectedColliderKDTree(entt::entity selected,
                                     const Graphics::GeometryCollisionData& collision)
    {
        const bool cacheValid =
            (m_SelectedKDTreeEntity == selected) &&
            !m_SelectedColliderKDTree.Nodes().empty();

        if (cacheValid)
            return true;

        m_SelectedColliderKDTree = Geometry::KDTree{};
        m_SelectedKDTreeEntity = entt::null;

        if (collision.Positions.empty())
            return false;

        Geometry::KDTreeBuildParams params{};
        params.LeafSize = 24;
        params.MaxDepth = 24;

        auto build = m_SelectedColliderKDTree.BuildFromPoints(collision.Positions, params);
        if (!build)
            return false;

        m_SelectedKDTreeEntity = selected;
        return true;
    }

    bool EnsureSelectedColliderConvexHull(entt::entity selected,
                                         const Graphics::GeometryCollisionData& collision)
    {
        const bool cacheValid =
            (m_SelectedHullEntity == selected) &&
            !m_SelectedColliderHullMesh.IsEmpty();

        if (cacheValid)
            return true;

        m_SelectedColliderHullMesh = Geometry::Halfedge::Mesh{};
        m_SelectedHullEntity = entt::null;

        if (collision.Positions.size() < 4)
            return false;

        Geometry::ConvexHullBuilder::ConvexHullParams params{};
        params.BuildMesh = true;
        params.ComputePlanes = false;

        auto hull = Geometry::ConvexHullBuilder::Build(collision.Positions, params);
        if (!hull || hull->Mesh.IsEmpty())
            return false;

        m_SelectedColliderHullMesh = std::move(hull->Mesh);
        m_SelectedHullEntity = selected;
        return true;
    }

    void ReleaseCachedOctreeOverlay()
    {
        auto& reg = GetScene().GetRegistry();
        if (m_OctreeOverlayEntity != entt::null && reg.valid(m_OctreeOverlayEntity))
            reg.destroy(m_OctreeOverlayEntity);

        if (m_OctreeOverlayGeometry.IsValid())
        {
            GetGeometryStorage().Remove(m_OctreeOverlayGeometry, GetDevice().GetGlobalFrameNumber());
            m_OctreeOverlayGeometry = {};
        }

        m_OctreeOverlayEntity = entt::null;
        m_OctreeOverlaySourceEntity = entt::null;
        m_CachedOctreeOverlayWorld = glm::mat4(1.0f);
        m_CachedOctreeOverlaySettings = {};
        m_CachedOctreeOverlayLocalAABB = {};
        m_HasCachedOctreeOverlayAabb = false;
    }

    void ReleaseRetainedLineOverlay(RetainedLineOverlaySlot& slot)
    {
        auto& reg = GetScene().GetRegistry();
        if (slot.Entity != entt::null && reg.valid(slot.Entity))
            reg.destroy(slot.Entity);

        if (slot.Geometry.IsValid())
            GetGeometryStorage().Remove(slot.Geometry, GetDevice().GetGlobalFrameNumber());

        slot = {};
    }

    bool UpdateRetainedLineOverlay(RetainedLineOverlaySlot& slot,
                                   const std::function<void(Graphics::DebugDraw&)>& emit)
    {
        Graphics::DebugDraw capture;
        emit(capture);

        if (!capture.GetTriangles().empty() || !capture.GetPoints().empty())
        {
            ReleaseRetainedLineOverlay(slot);
            return false;
        }

        const auto depthLines = capture.GetLines();
        const auto overlayLines = capture.GetOverlayLines();
        if (depthLines.empty() && overlayLines.empty())
        {
            ReleaseRetainedLineOverlay(slot);
            return false;
        }
        if (!depthLines.empty() && !overlayLines.empty())
        {
            ReleaseRetainedLineOverlay(slot);
            return false;
        }

        const auto segments = !overlayLines.empty() ? overlayLines : depthLines;
        const bool overlay = !overlayLines.empty();

        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> edgeColors;
        positions.reserve(segments.size() * 2u);
        indices.reserve(segments.size() * 2u);
        edgeColors.reserve(segments.size());

        auto appendSegment = [&](const glm::vec3& a, const glm::vec3& b, uint32_t color)
        {
            const auto base = static_cast<uint32_t>(positions.size());
            positions.push_back(a);
            positions.push_back(b);
            indices.push_back(base + 0u);
            indices.push_back(base + 1u);
            edgeColors.push_back(color);
        };

        for (const auto& seg : segments)
        {
            if (seg.ColorStart == seg.ColorEnd)
            {
                appendSegment(seg.Start, seg.End, seg.ColorStart);
            }
            else
            {
                const glm::vec3 mid = (seg.Start + seg.End) * 0.5f;
                appendSegment(seg.Start, mid, seg.ColorStart);
                appendSegment(mid, seg.End, seg.ColorEnd);
            }
        }

        Graphics::GeometryUploadRequest upload{};
        upload.Positions = positions;
        upload.Indices = indices;
        upload.Topology = Graphics::PrimitiveTopology::Lines;
        upload.UploadMode = Graphics::GeometryUploadMode::Direct;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            GetDeviceShared(), GetGraphicsBackend().GetTransferManager(), upload, &GetGeometryStorage());
        (void)token;
        if (!gpuData)
        {
            ReleaseRetainedLineOverlay(slot);
            return false;
        }

        const Geometry::GeometryHandle oldGeometry = slot.Geometry;
        const Geometry::GeometryHandle newGeometry = GetGeometryStorage().Add(std::move(gpuData));

        auto& reg = GetScene().GetRegistry();
        if (slot.Entity == entt::null || !reg.valid(slot.Entity))
        {
            slot.Entity = reg.create();
            reg.emplace<HiddenEditorEntityTag>(slot.Entity);
        }

        auto& line = reg.emplace_or_replace<ECS::Line::Component>(slot.Entity);
        line.Geometry = newGeometry;
        line.EdgeView = newGeometry;
        line.EdgeCount = static_cast<uint32_t>(indices.size() / 2u);
        line.Color = glm::vec4(1.0f);
        line.Width = 1.5f;
        line.Overlay = overlay;
        line.HasPerEdgeColors = true;
        line.ShowPerEdgeColors = true;
        line.CachedEdgeColors = std::move(edgeColors);

        slot.Geometry = newGeometry;
        if (oldGeometry.IsValid() && oldGeometry != newGeometry)
            GetGeometryStorage().Remove(oldGeometry, GetDevice().GetGlobalFrameNumber());

        return true;
    }

    bool EnsureRetainedOctreeOverlay(entt::entity selected,
                                     const Graphics::GeometryCollisionData& collision,
                                     const ECS::Components::Transform::Component& xf)
    {
        Graphics::OctreeDebugDrawSettings settings = m_OctreeDebugSettings;
        settings.Enabled = true;

        const glm::mat4 worldMatrix = GetMatrix(xf);
        const bool cacheValid =
            (m_OctreeOverlayEntity != entt::null) &&
            GetScene().GetRegistry().valid(m_OctreeOverlayEntity) &&
            m_OctreeOverlayGeometry.IsValid() &&
            (m_OctreeOverlaySourceEntity == selected) &&
            OctreeSettingsEqual(m_CachedOctreeOverlaySettings, settings) &&
            MatricesNearlyEqual(m_CachedOctreeOverlayWorld, worldMatrix) &&
            m_HasCachedOctreeOverlayAabb &&
            Vec3NearlyEqual(m_CachedOctreeOverlayLocalAABB.Min, collision.LocalAABB.Min) &&
            Vec3NearlyEqual(m_CachedOctreeOverlayLocalAABB.Max, collision.LocalAABB.Max);

        if (cacheValid)
            return true;

        if (collision.LocalOctree.m_Nodes.empty())
        {
            ReleaseCachedOctreeOverlay();
            return false;
        }

        constexpr std::array<uint32_t, 24> kBoxEdges = {
            0u, 1u, 1u, 3u, 3u, 2u, 2u, 0u,
            4u, 5u, 5u, 7u, 7u, 6u, 6u, 4u,
            0u, 4u, 1u, 5u, 2u, 6u, 3u, 7u,
        };

        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> edgeColors;

        auto appendBox = [&](const glm::vec3& lo, const glm::vec3& hi, uint32_t color)
        {
            const uint32_t base = static_cast<uint32_t>(positions.size());
            positions.push_back({lo.x, lo.y, lo.z});
            positions.push_back({hi.x, lo.y, lo.z});
            positions.push_back({lo.x, hi.y, lo.z});
            positions.push_back({hi.x, hi.y, lo.z});
            positions.push_back({lo.x, lo.y, hi.z});
            positions.push_back({hi.x, lo.y, hi.z});
            positions.push_back({lo.x, hi.y, hi.z});
            positions.push_back({hi.x, hi.y, hi.z});

            for (uint32_t edge : kBoxEdges)
                indices.push_back(base + edge);

            edgeColors.insert(edgeColors.end(), kBoxEdges.size() / 2u, color);
        };

        struct StackItem { Geometry::Octree::NodeIndex Node; std::uint32_t Depth; };
        std::array<StackItem, 512> stack{};
        std::size_t sp = 0;
        stack[sp++] = {0u, 0u};

        const auto& nodes = collision.LocalOctree.m_Nodes;
        while (sp > 0)
        {
            const StackItem item = stack[--sp];
            if (item.Node >= nodes.size())
                continue;

            const auto& node = nodes[item.Node];
            if (item.Depth > settings.MaxDepth)
                continue;

            const bool isLeaf = node.IsLeaf;
            const bool drawThis =
                (!settings.OccupiedOnly || node.NumElements > 0) &&
                ((settings.LeafOnly && isLeaf) || (!settings.LeafOnly && (isLeaf || settings.DrawInternal)));

            if (drawThis)
            {
                const float t = (settings.MaxDepth > 0u)
                    ? (static_cast<float>(item.Depth) / static_cast<float>(settings.MaxDepth))
                    : 0.0f;
                const glm::vec3 rgb = settings.ColorByDepth ? DepthRamp(t) : settings.BaseColor;
                const uint32_t color = PackWithAlpha(rgb, settings.Alpha);

                glm::vec3 lo, hi;
                TransformAABB(node.Aabb.Min, node.Aabb.Max, worldMatrix, lo, hi);
                appendBox(lo, hi, color);
            }

            if (!node.IsLeaf && node.BaseChildIndex != Geometry::Octree::kInvalidIndex)
            {
                std::uint32_t childOffset = 0;
                for (int child = 0; child < 8; ++child)
                {
                    if (!node.ChildExists(child))
                        continue;

                    const auto childIndex = node.BaseChildIndex + childOffset;
                    ++childOffset;
                    if (sp < stack.size())
                        stack[sp++] = {childIndex, item.Depth + 1u};
                }
            }
        }

        if (positions.empty() || indices.empty())
        {
            ReleaseCachedOctreeOverlay();
            return false;
        }

        Graphics::GeometryUploadRequest upload{};
        upload.Positions = positions;
        upload.Indices = indices;
        upload.Topology = Graphics::PrimitiveTopology::Lines;
        upload.UploadMode = Graphics::GeometryUploadMode::Direct;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            GetDeviceShared(), GetGraphicsBackend().GetTransferManager(), upload, &GetGeometryStorage());
        (void)token;
        if (!gpuData)
        {
            ReleaseCachedOctreeOverlay();
            return false;
        }

        const Geometry::GeometryHandle oldGeometry = m_OctreeOverlayGeometry;
        const Geometry::GeometryHandle newGeometry = GetGeometryStorage().Add(std::move(gpuData));

        auto& reg = GetScene().GetRegistry();
        if (m_OctreeOverlayEntity == entt::null || !reg.valid(m_OctreeOverlayEntity))
        {
            m_OctreeOverlayEntity = reg.create();
            reg.emplace<HiddenEditorEntityTag>(m_OctreeOverlayEntity);
        }

        auto& line = reg.emplace_or_replace<ECS::Line::Component>(m_OctreeOverlayEntity);
        line.Geometry = newGeometry;
        line.EdgeView = newGeometry;
        line.EdgeCount = static_cast<uint32_t>(indices.size() / 2u);
        line.Color = glm::vec4(1.0f);
        line.Width = 1.5f;
        line.Overlay = settings.Overlay;
        line.HasPerEdgeColors = true;
        line.ShowPerEdgeColors = true;
        line.CachedEdgeColors = std::move(edgeColors);

        m_OctreeOverlayGeometry = newGeometry;
        m_OctreeOverlaySourceEntity = selected;
        m_CachedOctreeOverlaySettings = settings;
        m_CachedOctreeOverlayWorld = worldMatrix;
        m_CachedOctreeOverlayLocalAABB = collision.LocalAABB;
        m_HasCachedOctreeOverlayAabb = true;

        if (oldGeometry.IsValid() && oldGeometry != newGeometry)
            GetGeometryStorage().Remove(oldGeometry, GetDevice().GetGlobalFrameNumber());

        return true;
    }

    bool EnsureRetainedBoundsOverlay(entt::entity selected,
                                     const Geometry::AABB& localAabb,
                                     const Geometry::OBB& worldObb,
                                     const ECS::Components::Transform::Component& xf)
    {
        const glm::mat4 worldMatrix = GetMatrix(xf);
        const bool cacheValid =
            (m_BoundsOverlaySourceEntity == selected) &&
            m_BoundsOverlay.Geometry.IsValid() &&
            OctreeSettingsEqual(
                { .Enabled = true, .Overlay = m_CachedBoundsOverlaySettings.Overlay, .ColorByDepth = false, .MaxDepth = 0u, .LeafOnly = false, .DrawInternal = false, .OccupiedOnly = false, .Alpha = m_CachedBoundsOverlaySettings.Alpha, .BaseColor = m_CachedBoundsOverlaySettings.AABBColor },
                { .Enabled = true, .Overlay = m_BoundsDebugSettings.Overlay, .ColorByDepth = false, .MaxDepth = 0u, .LeafOnly = false, .DrawInternal = false, .OccupiedOnly = false, .Alpha = m_BoundsDebugSettings.Alpha, .BaseColor = m_BoundsDebugSettings.AABBColor }) &&
            m_CachedBoundsOverlaySettings.DrawAABB == m_BoundsDebugSettings.DrawAABB &&
            m_CachedBoundsOverlaySettings.DrawOBB == m_BoundsDebugSettings.DrawOBB &&
            m_CachedBoundsOverlaySettings.DrawBoundingSphere == m_BoundsDebugSettings.DrawBoundingSphere &&
            Vec3NearlyEqual(m_CachedBoundsOverlaySettings.AABBColor, m_BoundsDebugSettings.AABBColor) &&
            Vec3NearlyEqual(m_CachedBoundsOverlaySettings.OBBColor, m_BoundsDebugSettings.OBBColor) &&
            Vec3NearlyEqual(m_CachedBoundsOverlaySettings.SphereColor, m_BoundsDebugSettings.SphereColor) &&
            MatricesNearlyEqual(m_CachedBoundsOverlayWorld, worldMatrix) &&
            m_HasCachedBoundsOverlayAabb &&
            Vec3NearlyEqual(m_CachedBoundsOverlayLocalAabb.Min, localAabb.Min) &&
            Vec3NearlyEqual(m_CachedBoundsOverlayLocalAabb.Max, localAabb.Max);

        if (cacheValid)
            return true;

        Graphics::BoundingDebugDrawSettings settings = m_BoundsDebugSettings;
        settings.Enabled = true;
        const bool ok = UpdateRetainedLineOverlay(m_BoundsOverlay, [&](Graphics::DebugDraw& dd)
        {
            DrawBoundingVolumes(dd, localAabb, worldObb, settings);
        });

        if (!ok)
            return false;

        m_BoundsOverlaySourceEntity = selected;
        m_CachedBoundsOverlaySettings = settings;
        m_CachedBoundsOverlayWorld = worldMatrix;
        m_CachedBoundsOverlayLocalAabb = localAabb;
        m_HasCachedBoundsOverlayAabb = true;
        return true;
    }

    bool EnsureRetainedKDTreeOverlay(entt::entity selected,
                                     const Graphics::GeometryCollisionData& collision,
                                     const ECS::Components::Transform::Component& xf)
    {
        if (!EnsureSelectedColliderKDTree(selected, collision))
        {
            ReleaseRetainedLineOverlay(m_KDTreeOverlay);
            m_KDTreeOverlaySourceEntity = entt::null;
            return false;
        }

        const glm::mat4 worldMatrix = GetMatrix(xf);
        const bool cacheValid =
            (m_KDTreeOverlaySourceEntity == selected) &&
            m_KDTreeOverlay.Geometry.IsValid() &&
            m_CachedKDTreeOverlaySettings.Overlay == m_KDTreeDebugSettings.Overlay &&
            m_CachedKDTreeOverlaySettings.LeafOnly == m_KDTreeDebugSettings.LeafOnly &&
            m_CachedKDTreeOverlaySettings.DrawInternal == m_KDTreeDebugSettings.DrawInternal &&
            m_CachedKDTreeOverlaySettings.OccupiedOnly == m_KDTreeDebugSettings.OccupiedOnly &&
            m_CachedKDTreeOverlaySettings.DrawSplitPlanes == m_KDTreeDebugSettings.DrawSplitPlanes &&
            m_CachedKDTreeOverlaySettings.MaxDepth == m_KDTreeDebugSettings.MaxDepth &&
            std::abs(m_CachedKDTreeOverlaySettings.Alpha - m_KDTreeDebugSettings.Alpha) <= 1e-4f &&
            Vec3NearlyEqual(m_CachedKDTreeOverlaySettings.LeafColor, m_KDTreeDebugSettings.LeafColor) &&
            Vec3NearlyEqual(m_CachedKDTreeOverlaySettings.InternalColor, m_KDTreeDebugSettings.InternalColor) &&
            Vec3NearlyEqual(m_CachedKDTreeOverlaySettings.SplitPlaneColor, m_KDTreeDebugSettings.SplitPlaneColor) &&
            MatricesNearlyEqual(m_CachedKDTreeOverlayWorld, worldMatrix);

        if (cacheValid)
            return true;

        Graphics::KDTreeDebugDrawSettings settings = m_KDTreeDebugSettings;
        settings.Enabled = true;
        const bool ok = UpdateRetainedLineOverlay(m_KDTreeOverlay, [&](Graphics::DebugDraw& dd)
        {
            DrawKDTree(dd, m_SelectedColliderKDTree, settings, worldMatrix);
        });

        if (!ok)
            return false;

        m_KDTreeOverlaySourceEntity = selected;
        m_CachedKDTreeOverlaySettings = settings;
        m_CachedKDTreeOverlayWorld = worldMatrix;
        return true;
    }

    bool EnsureRetainedBVHOverlay(entt::entity selected,
                                  const Graphics::GeometryCollisionData& collision,
                                  const ECS::Components::Transform::Component& xf)
    {
        const glm::mat4 worldMatrix = GetMatrix(xf);
        const bool cacheValid =
            (m_BVHOverlaySourceEntity == selected) &&
            m_BVHOverlay.Geometry.IsValid() &&
            m_CachedBVHOverlaySettings.Overlay == m_BVHDebugSettings.Overlay &&
            m_CachedBVHOverlaySettings.LeafOnly == m_BVHDebugSettings.LeafOnly &&
            m_CachedBVHOverlaySettings.DrawInternal == m_BVHDebugSettings.DrawInternal &&
            m_CachedBVHOverlaySettings.MaxDepth == m_BVHDebugSettings.MaxDepth &&
            m_CachedBVHOverlaySettings.LeafTriangleCount == m_BVHDebugSettings.LeafTriangleCount &&
            std::abs(m_CachedBVHOverlaySettings.Alpha - m_BVHDebugSettings.Alpha) <= 1e-4f &&
            Vec3NearlyEqual(m_CachedBVHOverlaySettings.LeafColor, m_BVHDebugSettings.LeafColor) &&
            Vec3NearlyEqual(m_CachedBVHOverlaySettings.InternalColor, m_BVHDebugSettings.InternalColor) &&
            MatricesNearlyEqual(m_CachedBVHOverlayWorld, worldMatrix) &&
            m_CachedBVHOverlayPositionCount == collision.Positions.size() &&
            m_CachedBVHOverlayIndexCount == collision.Indices.size();

        if (cacheValid)
            return true;

        Graphics::BVHDebugDrawSettings settings = m_BVHDebugSettings;
        settings.Enabled = true;
        const bool ok = UpdateRetainedLineOverlay(m_BVHOverlay, [&](Graphics::DebugDraw& dd)
        {
            DrawBVH(dd, collision.Positions, collision.Indices, settings, worldMatrix);
        });

        if (!ok)
            return false;

        m_BVHOverlaySourceEntity = selected;
        m_CachedBVHOverlaySettings = settings;
        m_CachedBVHOverlayWorld = worldMatrix;
        m_CachedBVHOverlayPositionCount = collision.Positions.size();
        m_CachedBVHOverlayIndexCount = collision.Indices.size();
        return true;
    }

    bool EnsureRetainedConvexHullOverlay(entt::entity selected,
                                         const Graphics::GeometryCollisionData& collision,
                                         const ECS::Components::Transform::Component& xf)
    {
        if (!EnsureSelectedColliderConvexHull(selected, collision))
        {
            ReleaseRetainedLineOverlay(m_ConvexHullOverlay);
            m_ConvexHullOverlaySourceEntity = entt::null;
            return false;
        }

        const glm::mat4 worldMatrix = GetMatrix(xf);
        const bool cacheValid =
            (m_ConvexHullOverlaySourceEntity == selected) &&
            m_ConvexHullOverlay.Geometry.IsValid() &&
            m_CachedConvexHullOverlaySettings.Overlay == m_ConvexHullDebugSettings.Overlay &&
            std::abs(m_CachedConvexHullOverlaySettings.Alpha - m_ConvexHullDebugSettings.Alpha) <= 1e-4f &&
            Vec3NearlyEqual(m_CachedConvexHullOverlaySettings.Color, m_ConvexHullDebugSettings.Color) &&
            MatricesNearlyEqual(m_CachedConvexHullOverlayWorld, worldMatrix);

        if (cacheValid)
            return true;

        Graphics::ConvexHullDebugDrawSettings settings = m_ConvexHullDebugSettings;
        settings.Enabled = true;
        const bool ok = UpdateRetainedLineOverlay(m_ConvexHullOverlay, [&](Graphics::DebugDraw& dd)
        {
            DrawConvexHull(dd, m_SelectedColliderHullMesh, settings, worldMatrix);
        });

        if (!ok)
            return false;

        m_ConvexHullOverlaySourceEntity = selected;
        m_CachedConvexHullOverlaySettings = settings;
        m_CachedConvexHullOverlayWorld = worldMatrix;
        return true;
    }

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        auto& gfx = GetGraphicsBackend();


        m_CameraEntity = GetScene().CreateEntity("Main Camera");
        m_Camera = GetScene().GetRegistry().emplace<Graphics::CameraComponent>(m_CameraEntity);
        GetScene().GetRegistry().emplace<Graphics::OrbitControlComponent>(m_CameraEntity);

        // Cache selected entity via dispatcher sink instead of polling every frame.
        GetScene().GetDispatcher().sink<ECS::Events::SelectionChanged>().connect<
            [](entt::entity& cached, const ECS::Events::SelectionChanged& evt) {
                cached = evt.Entity;
            }>(m_CachedSelectedEntity);

        // Connect SelectionModule to the scene dispatcher so GPU pick results
        // arrive via GpuPickCompleted event instead of per-frame polling.
        GetSelection().ConnectToScene(GetScene());

        auto textureLoader = [this, &gfx](const std::filesystem::path& path, Core::Assets::AssetHandle handle)
            -> std::shared_ptr<RHI::Texture>
        {
            auto result = Graphics::TextureLoader::LoadAsync(path, GetDevice(),
                gfx.GetTransferManager(), gfx.GetTextureSystem());

            if (result)
            {
                // When the transfer finishes, publish the real descriptor for this texture's bindless slot.
                RegisterAssetLoad(handle, result->Token, [this, texHandle = result->TextureHandle]()
                {
                    auto& g = GetGraphicsBackend();
                    if (const auto* data = g.GetTextureSystem().Get(texHandle))
                    {
                        // Flip bindless slot from default -> real view/sampler.
                        // This is the critical publish step in Phase 1.
                        // (No GPU waits; safe because token completion implies transfer queue copy is done.)
                        g.GetBindlessSystem().EnqueueUpdate(data->BindlessSlot, data->Image->GetView(), data->Sampler);
                    }
                });

                GetAssetManager().MoveToProcessing(handle);
                return std::move(result->Texture);
            }

            Log::Warn("Texture load failed: {} ({})", path.string(), Graphics::AssetErrorToString(result.error()));
            return {};
        };
        m_DuckTexture = GetAssetManager().Load<RHI::Texture>(Filesystem::GetAssetPath("textures/DuckCM.png"),
                                                              textureLoader);

        auto modelLoader = [&](const std::string& path, Assets::AssetHandle handle)
            -> std::unique_ptr<Graphics::Model>
        {
            auto result = Graphics::ModelLoader::LoadAsync(
                GetDeviceShared(), gfx.GetTransferManager(), GetGeometryStorage(), path,
                GetIORegistry(), GetIOBackend());

            if (result)
            {
                // 1. Notify Engine to track the GPU work
                RegisterAssetLoad(handle, result->Token);

                // 2. Notify AssetManager to wait
                GetAssetManager().MoveToProcessing(handle);

                // 3. Return the model (valid CPU pointers, GPU buffers are allocated but content is uploading)
                return std::move(result->ModelData);
            }

            Log::Warn("Model load failed: {} ({})", path, Graphics::AssetErrorToString(result.error()));
            return nullptr; // Failed
        };

        m_DuckModel = GetAssetManager().Load<Graphics::Model>(
            Filesystem::GetAssetPath("models/Duck.glb"),
            modelLoader
        );


        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        Graphics::MaterialData matData;
        matData.AlbedoID = gfx.GetDefaultTextureIndex(); // Fallback until loaded
        matData.RoughnessFactor = 1.0f;
        matData.MetallicFactor = 0.0f;

        auto DuckMaterial = std::make_unique<Graphics::Material>(
            GetRenderOrchestrator().GetMaterialSystem(),
            matData
        );

        // Link the texture asset to the material (this registers the listener)
        DuckMaterial->SetAlbedoTexture(m_DuckTexture);

        // Track handle only; AssetManager owns the actual Material object.
        m_DuckMaterialHandle = GetAssetManager().Create("DuckMaterial", std::move(DuckMaterial));
        GetAssetPipeline().TrackMaterial(m_DuckMaterialHandle);

        Log::Info("Asset Load Requested. Waiting for background thread...");

        // --- Register client features in the central FeatureRegistry ---
        auto& features = GetFeatureRegistry();
        using Cat = Core::FeatureCategory;

        // Client ECS system
        {
            Core::FeatureInfo info{};
            info.Name = "AxisRotator";
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::System;
            info.Description = "Continuous rotation animation for tagged entities";
            info.Enabled = true;
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*) {});
        }

        // UI panels
        auto registerPanelFeature = [&features](const std::string& name, const std::string& desc) {
            Core::FeatureInfo info{};
            info.Name = name;
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::Panel;
            info.Description = desc;
            info.Enabled = true;
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*) {});
        };
        registerPanelFeature("Hierarchy", "Scene entity hierarchy browser");
        registerPanelFeature("Inspector", "Component property editor");
        registerPanelFeature("Assets", "Asset manager browser");
        registerPanelFeature("Stats", "Performance statistics and debug controls");
        registerPanelFeature("View Settings", "Selection outline and viewport display settings");
        registerPanelFeature("Render Target Viewer", "Render target debug visualization");
        registerPanelFeature("Geometry Workflow", "Workflow hub for geometry processing tools");
        registerPanelFeature("Geometry - Remeshing", "Remeshing operators: isotropic and adaptive workflows");
        registerPanelFeature("Geometry - Simplification", "Mesh simplification operators");
        registerPanelFeature("Geometry - Smoothing", "Surface smoothing operators grouped by approach");
        registerPanelFeature("Geometry - Subdivision", "Subdivision operators for surface refinement");
        registerPanelFeature("Geometry - Repair", "Mesh cleanup and repair operators");
        registerPanelFeature("Status Bar", "Bottom-of-viewport frame summary (frame time, entity count, active renderer)");

        Log::Info("FeatureRegistry: {} total features after client registration", features.Count());

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Assets", [this]() { GetAssetManager().AssetsUiPanel(); });
        OpenGeometryWorkflowPanel();

        // Register shared editor-facing panels (Feature browser, FrameGraph inspector, Selection config).
        Runtime::EditorUI::RegisterDefaultPanels(*this);
        Interface::GUI::RegisterMainMenuBar("Geometry", [this]() { DrawGeometryMenu(); });

        Interface::GUI::RegisterPanel("Stats", [this]()
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)GetScene().Size());

            // --- Render Pipeline ---
            ImGui::SeparatorText("Render Pipeline");
            {
                if (ImGui::Button("Hot-swap: DefaultPipeline"))
                {
                    // Request swap; RenderSystem owns lifetime and applies at the start of the next frame.
                    auto pipeline = std::make_unique<Graphics::DefaultPipeline>();
                    pipeline->SetFeatureRegistry(&GetFeatureRegistry());
                    GetRenderOrchestrator().GetRenderSystem().RequestPipelineSwap(std::move(pipeline));
                }
            }

            // --- Selection Debug ---
            ImGui::Separator();
            ImGui::Text("Select Mouse Button: %d", m_SelectMouseButton);

            const entt::entity selected = m_CachedSelectedEntity;
            const bool selectedValid = (selected != entt::null) && GetScene().GetRegistry().valid(selected);

            ImGui::Text("Selected: %u (%s)",
                        static_cast<uint32_t>(static_cast<entt::id_type>(selected)),
                        selectedValid ? "valid" : "invalid");

            if (selectedValid)
            {
                const auto& reg = GetScene().GetRegistry();
                const bool hasSelectedTag = reg.all_of<ECS::Components::Selection::SelectedTag>(selected);
                const bool hasSelectableTag = reg.all_of<ECS::Components::Selection::SelectableTag>(selected);
                const bool hasSurface = reg.all_of<ECS::Surface::Component>(selected);
                const bool hasMeshCollider = reg.all_of<ECS::MeshCollider::Component>(selected);
                const bool hasGraph = reg.all_of<ECS::Graph::Data>(selected);
                const bool hasPointCloud = reg.all_of<ECS::PointCloud::Data>(selected);

                ImGui::Text("Tags: Selectable=%d Selected=%d", (int)hasSelectableTag, (int)hasSelectedTag);
                ImGui::Text("Components: Surface=%d MeshCollider=%d Graph=%d PointCloud=%d",
                            (int)hasSurface, (int)hasMeshCollider, (int)hasGraph, (int)hasPointCloud);

                uint32_t selfPickId = 0u;
                if (const auto* pid = reg.try_get<ECS::Components::Selection::PickID>(selected))
                    selfPickId = pid->Value;

                uint32_t outlineIds[Graphics::Passes::SelectionOutlinePass::kMaxSelectedIds] = {};
                const uint32_t outlineCount = Graphics::Passes::AppendOutlineRenderablePickIds(reg, selected, outlineIds);
                ImGui::Text("PickIDs: Self=%u OutlineResolved=%u", selfPickId, outlineCount);
                for (uint32_t i = 0; i < outlineCount; ++i)
                    ImGui::BulletText("Outline PickID[%u] = %u", i, outlineIds[i]);

                if (outlineCount == 0u)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       "No renderable outline PickIDs resolved for this selection.");

                const auto gpuPick = GetRenderOrchestrator().GetRenderSystem().GetLastPickResult();
                ImGui::Text("Last GPU Pick: Hit=%d EntityID=%u", (int)gpuPick.HasHit, gpuPick.EntityID);
            }
        });

        Interface::GUI::RegisterPanel("Status Bar", [this]()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float statusBarHeight = ImGui::GetFrameHeight() + 10.0f;

            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight));
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusBarHeight));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus;

            const float fps = ImGui::GetIO().Framerate;
            const float frameMs = fps > 0.0f ? (1000.0f / fps) : 0.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            // Draws a vertical separator line at the current cursor position (no SeparatorEx in this ImGui version).
            auto VerticalSeparator = []()
            {
                ImGui::SameLine();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float h    = ImGui::GetFrameHeight();
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(pos.x, pos.y),
                    ImVec2(pos.x, pos.y + h),
                    ImGui::GetColorU32(ImGuiCol_Separator));
                ImGui::Dummy(ImVec2(1.0f, h));
                ImGui::SameLine();
            };

            if (ImGui::Begin("##IntrinsicStatusBar", nullptr, flags))
            {
                ImGui::Text("Frame: %.2f ms (%.1f FPS)", frameMs, fps);
                VerticalSeparator();
                ImGui::Text("Entities: %d", static_cast<int>(GetScene().Size()));
                VerticalSeparator();
                ImGui::Text("Render Mode: DefaultPipeline");
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }, false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        registerPanelFeature("Viewport Toolbar", "Transform gizmo mode switching and snap controls");

        Interface::GUI::RegisterPanel("Viewport Toolbar", [this]()
        {
            auto& cfg = m_Gizmo.GetConfig();

            // --- Mode selection (horizontal radio buttons) ---
            ImGui::SeparatorText("Gizmo Mode");

            int mode = static_cast<int>(cfg.Mode);
            if (ImGui::RadioButton("Translate (W)", mode == 0)) m_Gizmo.SetMode(Graphics::GizmoMode::Translate);
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate (E)", mode == 1)) m_Gizmo.SetMode(Graphics::GizmoMode::Rotate);
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale (R)", mode == 2)) m_Gizmo.SetMode(Graphics::GizmoMode::Scale);

            // --- Space toggle ---
            ImGui::SeparatorText("Transform Space");
            int space = static_cast<int>(cfg.Space);
            if (ImGui::RadioButton("World", space == 0)) cfg.Space = Graphics::GizmoSpace::World;
            ImGui::SameLine();
            if (ImGui::RadioButton("Local (X)", space == 1)) cfg.Space = Graphics::GizmoSpace::Local;

            // --- Pivot strategy ---
            ImGui::SeparatorText("Pivot");
            int pivot = static_cast<int>(cfg.Pivot);
            if (ImGui::RadioButton("Centroid", pivot == 0)) cfg.Pivot = Graphics::GizmoPivot::Centroid;
            ImGui::SameLine();
            if (ImGui::RadioButton("First Selected", pivot == 1)) cfg.Pivot = Graphics::GizmoPivot::FirstSelected;

            // --- Snap settings ---
            ImGui::SeparatorText("Snapping");
            ImGui::Checkbox("Enable Snap", &cfg.SnapEnabled);

            if (cfg.SnapEnabled)
            {
                ImGui::DragFloat("Translate Snap", &cfg.TranslateSnap, 0.05f, 0.01f, 10.0f, "%.2f");
                ImGui::DragFloat("Rotate Snap (deg)", &cfg.RotateSnap, 1.0f, 1.0f, 90.0f, "%.1f");
                ImGui::DragFloat("Scale Snap", &cfg.ScaleSnap, 0.01f, 0.01f, 1.0f, "%.2f");
            }


            // --- Visual settings ---
            ImGui::SeparatorText("Appearance");
            ImGui::SliderFloat("Handle Size", &cfg.HandleLength, 0.3f, 3.0f, "%.1f");
            ImGui::SliderFloat("Handle Thickness", &cfg.HandleThickness, 1.0f, 8.0f, "%.1f px");

            // --- Status ---
            ImGui::Separator();
            const char* stateNames[] = { "Idle", "Hovered", "Active" };
            ImGui::Text("State: %s", stateNames[static_cast<int>(m_Gizmo.GetState())]);
        });

        Interface::GUI::RegisterOverlay("Transform Gizmo", [this]()
        {
            m_Gizmo.DrawImGui();
        });

        // View Settings panel for configuring selection outline, post-process, etc.
        Interface::GUI::RegisterPanel("View Settings", [this]()
        {
            // --- Post-Processing ---
            auto* postSettings = GetRenderOrchestrator().GetRenderSystem().GetPostProcessSettings();
            if (postSettings)
            {
                ImGui::SeparatorText("Post Processing");

                // Tone mapping
                const char* toneMapOps[] = { "ACES", "Reinhard", "Uncharted 2" };
                int toneOp = static_cast<int>(postSettings->ToneOperator);
                if (ImGui::Combo("Tone Mapping", &toneOp, toneMapOps, 3))
                    postSettings->ToneOperator = static_cast<Graphics::Passes::ToneMapOperator>(toneOp);

                ImGui::SliderFloat("Exposure", &postSettings->Exposure, 0.1f, 10.0f, "%.2f");

                // Bloom
                ImGui::Spacing();
                ImGui::Checkbox("Bloom", &postSettings->BloomEnabled);
                if (postSettings->BloomEnabled)
                {
                    ImGui::SliderFloat("Bloom Threshold", &postSettings->BloomThreshold, 0.0f, 5.0f, "%.2f");
                    ImGui::SliderFloat("Bloom Intensity", &postSettings->BloomIntensity, 0.0f, 1.0f, "%.3f");
                    ImGui::SliderFloat("Bloom Radius", &postSettings->BloomFilterRadius, 0.5f, 3.0f, "%.2f");
                }

                // Anti-Aliasing
                ImGui::Spacing();
                {
                    const char* aaModeNames[] = { "None", "FXAA", "SMAA" };
                    int aaIdx = static_cast<int>(postSettings->AntiAliasingMode);
                    if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModeNames, 3))
                        postSettings->AntiAliasingMode = static_cast<Graphics::Passes::AAMode>(aaIdx);
                }
                if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::FXAA)
                {
                    ImGui::SliderFloat("FXAA Contrast", &postSettings->FXAAContrastThreshold, 0.01f, 0.1f, "%.4f");
                    ImGui::SliderFloat("FXAA Relative", &postSettings->FXAARelativeThreshold, 0.01f, 0.2f, "%.4f");
                    ImGui::SliderFloat("FXAA Subpixel", &postSettings->FXAASubpixelBlending, 0.0f, 1.0f, "%.2f");
                }
                if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::SMAA)
                {
                    ImGui::SliderFloat("SMAA Edge Threshold", &postSettings->SMAAEdgeThreshold, 0.01f, 0.5f, "%.3f");
                    ImGui::SliderInt("SMAA Search Steps", &postSettings->SMAAMaxSearchSteps, 4, 32);
                    ImGui::SliderInt("SMAA Diag Steps", &postSettings->SMAAMaxSearchStepsDiag, 0, 16);
                }

                // Luminance Histogram
                ImGui::Spacing();
                ImGui::Checkbox("Exposure Histogram", &postSettings->HistogramEnabled);
                if (postSettings->HistogramEnabled)
                {
                    ImGui::SliderFloat("Min EV", &postSettings->HistogramMinEV, -20.0f, 0.0f, "%.1f");
                    ImGui::SliderFloat("Max EV", &postSettings->HistogramMaxEV, 0.0f, 20.0f, "%.1f");

                    const auto* histo = GetRenderOrchestrator().GetRenderSystem().GetHistogramReadback();
                    if (histo && histo->Valid)
                    {
                        // Find max bin for normalization.
                        uint32_t maxBin = 1;
                        for (uint32_t i = 0; i < Graphics::Passes::kHistogramBinCount; ++i)
                            maxBin = std::max(maxBin, histo->Bins[i]);

                        // Convert to float array for ImGui plot.
                        float plotData[Graphics::Passes::kHistogramBinCount];
                        for (uint32_t i = 0; i < Graphics::Passes::kHistogramBinCount; ++i)
                            plotData[i] = static_cast<float>(histo->Bins[i]) / static_cast<float>(maxBin);

                        ImGui::PlotHistogram("##LumHist", plotData,
                                             static_cast<int>(Graphics::Passes::kHistogramBinCount),
                                             0, nullptr, 0.0f, 1.0f, ImVec2(0, 80));

                        // Show average luminance and EV.
                        float avgEV = (histo->AverageLuminance > 1e-6f)
                                        ? std::log2(histo->AverageLuminance)
                                        : postSettings->HistogramMinEV;
                        ImGui::Text("Avg Luminance: %.4f  (%.1f EV)", histo->AverageLuminance, avgEV);
                    }
                    else
                    {
                        ImGui::TextDisabled("Histogram data not available yet.");
                    }
                }

                // Color Grading
                ImGui::Spacing();
                ImGui::Checkbox("Color Grading", &postSettings->ColorGradingEnabled);
                if (postSettings->ColorGradingEnabled)
                {
                    ImGui::SliderFloat("Saturation", &postSettings->Saturation, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Contrast##CG", &postSettings->Contrast, 0.5f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Temperature", &postSettings->ColorTempOffset, -1.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Tint", &postSettings->TintOffset, -1.0f, 1.0f, "%.2f");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Lift (Shadows)");
                    ImGui::SliderFloat("Lift R", &postSettings->LiftR, -0.5f, 0.5f, "%.3f");
                    ImGui::SliderFloat("Lift G", &postSettings->LiftG, -0.5f, 0.5f, "%.3f");
                    ImGui::SliderFloat("Lift B", &postSettings->LiftB, -0.5f, 0.5f, "%.3f");

                    ImGui::TextDisabled("Gamma (Midtones)");
                    ImGui::SliderFloat("Gamma R", &postSettings->GammaR, 0.2f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Gamma G", &postSettings->GammaG, 0.2f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Gamma B", &postSettings->GammaB, 0.2f, 3.0f, "%.2f");

                    ImGui::TextDisabled("Gain (Highlights)");
                    ImGui::SliderFloat("Gain R", &postSettings->GainR, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Gain G", &postSettings->GainG, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Gain B", &postSettings->GainB, 0.0f, 3.0f, "%.2f");

                    if (ImGui::Button("Reset Color Grading"))
                    {
                        postSettings->Saturation = 1.0f;
                        postSettings->Contrast = 1.0f;
                        postSettings->ColorTempOffset = 0.0f;
                        postSettings->TintOffset = 0.0f;
                        postSettings->LiftR = postSettings->LiftG = postSettings->LiftB = 0.0f;
                        postSettings->GammaR = postSettings->GammaG = postSettings->GammaB = 1.0f;
                        postSettings->GainR = postSettings->GainG = postSettings->GainB = 1.0f;
                    }
                }
            }

            // --- Selection Outline ---
            auto* outlineSettings = GetRenderOrchestrator().GetRenderSystem().GetSelectionOutlineSettings();
            if (!outlineSettings)
            {
                ImGui::TextDisabled("Selection outline settings not available.");
                return;
            }

            ImGui::SeparatorText("Selection Outline");

            // Outline mode
            {
                const char* modeNames[] = {"Solid", "Pulse", "Glow"};
                int currentMode = static_cast<int>(outlineSettings->Mode);
                if (ImGui::Combo("Outline Mode", &currentMode, modeNames, 3))
                    outlineSettings->Mode = static_cast<Graphics::Passes::OutlineMode>(currentMode);
            }

            // Selection color
            float selColor[4] = {
                outlineSettings->SelectionColor.r,
                outlineSettings->SelectionColor.g,
                outlineSettings->SelectionColor.b,
                outlineSettings->SelectionColor.a
            };
            if (ImGui::ColorEdit4("Selection Color", selColor))
            {
                outlineSettings->SelectionColor = glm::vec4(selColor[0], selColor[1], selColor[2], selColor[3]);
            }

            // Hover color
            float hoverColor[4] = {
                outlineSettings->HoverColor.r,
                outlineSettings->HoverColor.g,
                outlineSettings->HoverColor.b,
                outlineSettings->HoverColor.a
            };
            if (ImGui::ColorEdit4("Hover Color", hoverColor))
            {
                outlineSettings->HoverColor = glm::vec4(hoverColor[0], hoverColor[1], hoverColor[2], hoverColor[3]);
            }

            // Outline width
            ImGui::SliderFloat("Outline Width", &outlineSettings->OutlineWidth, 1.0f, 10.0f, "%.1f px");

            // Fill overlay
            ImGui::SliderFloat("Selection Fill", &outlineSettings->SelectionFillAlpha, 0.0f, 0.5f, "%.2f");
            ImGui::SliderFloat("Hover Fill", &outlineSettings->HoverFillAlpha, 0.0f, 0.5f, "%.2f");

            // Mode-specific settings
            if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Pulse)
            {
                ImGui::SliderFloat("Pulse Speed", &outlineSettings->PulseSpeed, 0.5f, 10.0f, "%.1f");
                ImGui::SliderFloat("Pulse Min Alpha", &outlineSettings->PulseMin, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Pulse Max Alpha", &outlineSettings->PulseMax, 0.0f, 1.0f, "%.2f");
            }
            else if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Glow)
            {
                ImGui::SliderFloat("Glow Falloff", &outlineSettings->GlowFalloff, 0.5f, 8.0f, "%.1f");
            }

            // Reset button
            if (ImGui::Button("Reset Outline Defaults"))
            {
                *outlineSettings = Graphics::Passes::SelectionOutlineSettings{};
            }

            // ---------------------------------------------------------------------
            // Octree Visualization (MeshCollider local octree)
            // ---------------------------------------------------------------------
            ImGui::Spacing();
            ImGui::SeparatorText("Spatial Debug");

            ImGui::Checkbox("Draw Selected MeshCollider Octree", &m_DrawSelectedColliderOctree);
            ImGui::Checkbox("Draw Selected MeshCollider Bounds", &m_DrawSelectedColliderBounds);
            ImGui::Checkbox("Draw Selected MeshCollider KD-Tree", &m_DrawSelectedColliderKDTree);
            ImGui::Checkbox("Draw Selected MeshCollider BVH", &m_DrawSelectedColliderBVH);
            ImGui::Checkbox("Draw Selected MeshCollider Convex Hull", &m_DrawSelectedColliderConvexHull);
            ImGui::Checkbox("Draw Contact Manifolds", &m_DrawSelectedColliderContacts);
            ImGui::Checkbox("Bounds Overlay (no depth test)", &m_BoundsDebugSettings.Overlay);
            ImGui::Checkbox("Draw World AABB", &m_BoundsDebugSettings.DrawAABB);
            ImGui::Checkbox("Draw World OBB", &m_BoundsDebugSettings.DrawOBB);
            ImGui::Checkbox("Draw Bounding Sphere", &m_BoundsDebugSettings.DrawBoundingSphere);
            ImGui::SliderFloat("Bounds Alpha", &m_BoundsDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float aabbColor[3] = {m_BoundsDebugSettings.AABBColor.r, m_BoundsDebugSettings.AABBColor.g, m_BoundsDebugSettings.AABBColor.b};
            if (ImGui::ColorEdit3("AABB Color", aabbColor))
                m_BoundsDebugSettings.AABBColor = glm::vec3(aabbColor[0], aabbColor[1], aabbColor[2]);

            float obbColor[3] = {m_BoundsDebugSettings.OBBColor.r, m_BoundsDebugSettings.OBBColor.g, m_BoundsDebugSettings.OBBColor.b};
            if (ImGui::ColorEdit3("OBB Color", obbColor))
                m_BoundsDebugSettings.OBBColor = glm::vec3(obbColor[0], obbColor[1], obbColor[2]);

            float sphereColor[3] = {m_BoundsDebugSettings.SphereColor.r, m_BoundsDebugSettings.SphereColor.g, m_BoundsDebugSettings.SphereColor.b};
            if (ImGui::ColorEdit3("Sphere Color", sphereColor))
                m_BoundsDebugSettings.SphereColor = glm::vec3(sphereColor[0], sphereColor[1], sphereColor[2]);

            ImGui::SeparatorText("KD-Tree");
            ImGui::Checkbox("KD Overlay (no depth test)", &m_KDTreeDebugSettings.Overlay);
            ImGui::Checkbox("KD Leaf Only", &m_KDTreeDebugSettings.LeafOnly);
            ImGui::Checkbox("KD Draw Internal", &m_KDTreeDebugSettings.DrawInternal);
            ImGui::Checkbox("KD Occupied Only", &m_KDTreeDebugSettings.OccupiedOnly);
            ImGui::Checkbox("KD Draw Split Planes", &m_KDTreeDebugSettings.DrawSplitPlanes);
            ImGui::SliderInt("KD Max Depth", reinterpret_cast<int*>(&m_KDTreeDebugSettings.MaxDepth), 0, 32);
            ImGui::SliderFloat("KD Alpha", &m_KDTreeDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float kdLeafColor[3] = {m_KDTreeDebugSettings.LeafColor.r, m_KDTreeDebugSettings.LeafColor.g, m_KDTreeDebugSettings.LeafColor.b};
            if (ImGui::ColorEdit3("KD Leaf Color", kdLeafColor))
                m_KDTreeDebugSettings.LeafColor = glm::vec3(kdLeafColor[0], kdLeafColor[1], kdLeafColor[2]);

            float kdInternalColor[3] = {m_KDTreeDebugSettings.InternalColor.r, m_KDTreeDebugSettings.InternalColor.g, m_KDTreeDebugSettings.InternalColor.b};
            if (ImGui::ColorEdit3("KD Internal Color", kdInternalColor))
                m_KDTreeDebugSettings.InternalColor = glm::vec3(kdInternalColor[0], kdInternalColor[1], kdInternalColor[2]);

            float kdSplitColor[3] = {m_KDTreeDebugSettings.SplitPlaneColor.r, m_KDTreeDebugSettings.SplitPlaneColor.g, m_KDTreeDebugSettings.SplitPlaneColor.b};
            if (ImGui::ColorEdit3("KD Split Color", kdSplitColor))
                m_KDTreeDebugSettings.SplitPlaneColor = glm::vec3(kdSplitColor[0], kdSplitColor[1], kdSplitColor[2]);

            ImGui::SeparatorText("Convex Hull");
            ImGui::Checkbox("Hull Overlay (no depth test)", &m_ConvexHullDebugSettings.Overlay);
            ImGui::SliderFloat("Hull Alpha", &m_ConvexHullDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");
            float hullColor[3] = {m_ConvexHullDebugSettings.Color.r, m_ConvexHullDebugSettings.Color.g, m_ConvexHullDebugSettings.Color.b};
            if (ImGui::ColorEdit3("Hull Color", hullColor))
                m_ConvexHullDebugSettings.Color = glm::vec3(hullColor[0], hullColor[1], hullColor[2]);


            ImGui::SeparatorText("BVH");
            ImGui::Checkbox("BVH Overlay (no depth test)", &m_BVHDebugSettings.Overlay);
            ImGui::Checkbox("BVH Leaf Only", &m_BVHDebugSettings.LeafOnly);
            ImGui::Checkbox("BVH Draw Internal", &m_BVHDebugSettings.DrawInternal);
            ImGui::SliderInt("BVH Max Depth", reinterpret_cast<int*>(&m_BVHDebugSettings.MaxDepth), 0, 32);
            ImGui::SliderInt("BVH Leaf Triangles", reinterpret_cast<int*>(&m_BVHDebugSettings.LeafTriangleCount), 1, 64);
            ImGui::SliderFloat("BVH Alpha", &m_BVHDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float bvhLeafColor[3] = {m_BVHDebugSettings.LeafColor.r, m_BVHDebugSettings.LeafColor.g, m_BVHDebugSettings.LeafColor.b};
            if (ImGui::ColorEdit3("BVH Leaf Color", bvhLeafColor))
                m_BVHDebugSettings.LeafColor = glm::vec3(bvhLeafColor[0], bvhLeafColor[1], bvhLeafColor[2]);

            float bvhInternalColor[3] = {m_BVHDebugSettings.InternalColor.r, m_BVHDebugSettings.InternalColor.g, m_BVHDebugSettings.InternalColor.b};
            if (ImGui::ColorEdit3("BVH Internal Color", bvhInternalColor))
                m_BVHDebugSettings.InternalColor = glm::vec3(bvhInternalColor[0], bvhInternalColor[1], bvhInternalColor[2]);

            ImGui::SeparatorText("Octree");
            ImGui::Checkbox("Overlay (no depth test)", &m_OctreeDebugSettings.Overlay);
            ImGui::Checkbox("Leaf Only", &m_OctreeDebugSettings.LeafOnly);
            ImGui::Checkbox("Occupied Only", &m_OctreeDebugSettings.OccupiedOnly);
            ImGui::Checkbox("Color By Depth", &m_OctreeDebugSettings.ColorByDepth);
            ImGui::SliderInt("Max Depth", reinterpret_cast<int*>(&m_OctreeDebugSettings.MaxDepth), 0, 16);
            ImGui::SliderFloat("Alpha", &m_OctreeDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            if (!m_OctreeDebugSettings.ColorByDepth)
            {
                float base[3] = {m_OctreeDebugSettings.BaseColor.r, m_OctreeDebugSettings.BaseColor.g, m_OctreeDebugSettings.BaseColor.b};
                if (ImGui::ColorEdit3("Base Color", base))
                    m_OctreeDebugSettings.BaseColor = glm::vec3(base[0], base[1], base[2]);
            }

            ImGui::SeparatorText("Contact Manifold");
            ImGui::Checkbox("Contact Overlay (no depth test)", &m_ContactDebugOverlay);
            ImGui::SliderFloat("Normal Scale", &m_ContactNormalScale, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("Point Radius", &m_ContactPointRadius, 0.005f, 0.2f, "%.3f");

            // NOTE: Actual DrawOctree() emission happens in OnUpdate() BEFORE renderSys.OnUpdate(),
            // because ImGui panels run AFTER the render graph has already executed.
            // The settings above will take effect on the next frame.

            // Show status feedback
            if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds || m_DrawSelectedColliderKDTree || m_DrawSelectedColliderBVH || m_DrawSelectedColliderConvexHull)
            {
                const entt::entity selected = m_CachedSelectedEntity;
                if (selected == entt::null || !GetScene().GetRegistry().valid(selected))
                {
                    ImGui::TextDisabled("No valid selected entity.");
                }
                else
                {
                    auto* collider = GetScene().GetRegistry().try_get<ECS::MeshCollider::Component>(selected);
                    if (!collider || !collider->CollisionRef)
                    {
                        ImGui::TextDisabled("Selected entity has no MeshCollider.");
                    }
                }
            }
        });
    }

    void OnUpdate(float dt) override
    {
        GetAssetManager().Update();

        bool uiCapturesMouse = Interface::GUI::WantCaptureMouse();
        bool uiCapturesKeyboard = Interface::GUI::WantCaptureKeyboard();
        bool inputCaptured = uiCapturesMouse || uiCapturesKeyboard;

        float aspectRatio = 1.0f;
        if (m_Window->GetWindowHeight() > 0)
        {
            aspectRatio = (float)m_Window->GetWindowWidth() / (float)m_Window->GetWindowHeight();
        }

        Graphics::CameraComponent* cameraComponent = nullptr;
        if (GetScene().GetRegistry().valid(m_CameraEntity))
        {
            // Check if it has Orbit controls
            cameraComponent = GetScene().GetRegistry().try_get<Graphics::CameraComponent>(m_CameraEntity);
            if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *orbit, m_Window->GetInput(), dt, inputCaptured);
            }
            // Check if it has Fly controls
            else if (auto* fly = GetScene().GetRegistry().try_get<Graphics::FlyControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *fly, m_Window->GetInput(), dt, inputCaptured);
            }

            if (m_Window->GetWindowWidth() != 0 && m_Window->GetWindowHeight() != 0)
            {
                Graphics::OnResize(*cameraComponent, m_Window->GetWindowWidth(), m_Window->GetWindowHeight());
            }

            // --- F Key: Focus camera on selected model ---
            if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::F))
            {
                if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
                {
                    const entt::entity selected = m_CachedSelectedEntity;
                    if (selected != entt::null && GetScene().GetRegistry().valid(selected))
                    {
                        auto* collider = GetScene().GetRegistry().try_get<ECS::MeshCollider::Component>(selected);
                        if (collider && collider->CollisionRef)
                        {
                            // Use the world-space OBB center as the new orbit target.
                            orbit->Target = collider->WorldOBB.Center;

                            // Compute an orbit distance that fits the object in view.
                            float radius = glm::length(collider->WorldOBB.Extents);
                            if (radius < 0.001f) radius = 1.0f;
                            float halfFov = glm::radians(cameraComponent->Fov) * 0.5f;
                            float fitDistance = radius / glm::tan(halfFov);
                            orbit->Distance = fitDistance * 1.5f; // Add margin.

                            // Reposition camera while preserving current viewing direction.
                            glm::vec3 viewDir = glm::normalize(cameraComponent->Position - orbit->Target);
                            cameraComponent->Position = orbit->Target + viewDir * orbit->Distance;
                        }
                    }
                }
            }

            // --- Q Key: Reset camera to defaults ---
            if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::Q))
            {
                if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
                {
                    // Restore orbit and camera to their struct-default values.
                    orbit->Target = glm::vec3(0.0f);
                    orbit->Distance = 5.0f;
                    cameraComponent->Position = glm::vec3(0.0f, 0.0f, 4.0f);
                    cameraComponent->Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }

            // --- Gizmo mode shortcuts: W=Translate, E=Rotate, R=Scale ---
            if (!uiCapturesKeyboard && !m_Gizmo.IsActive())
            {
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::W))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Translate);
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::E))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Rotate);
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::R))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Scale);
                // X toggles world/local space.
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::X))
                {
                    m_Gizmo.GetConfig().Space =
                        (m_Gizmo.GetConfig().Space == Graphics::GizmoSpace::World)
                            ? Graphics::GizmoSpace::Local
                            : Graphics::GizmoSpace::World;
                }
            }
        }

        {
            auto view = GetScene().GetRegistry().view<Graphics::CameraComponent>();
            for (auto [entity, cam] : view.each())
            {
                Graphics::UpdateMatrices(cam, aspectRatio);
            }
        }

        if (!m_IsEntitySpawned)
        {
            if (GetAssetManager().GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                // One line to rule them all
                m_DuckEntity = SpawnModel(m_DuckModel, m_DuckMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));
                if (m_DuckEntity != entt::null)
                    GetSelection().SetSelectedEntity(GetScene(), m_DuckEntity);

                // (Optional) If you need to add specific behaviors like rotation:
                // entt::entity duck = SpawnModel(...);
                // GetScene().GetRegistry().emplace<ECS::Components::AxisRotator::Component>(duck, ...);

                m_IsEntitySpawned = true;
                Log::Info("Duck Entity Spawned.");
            }
        }

        {
            auto view = GetScene().GetRegistry().view<
                ECS::Components::Transform::Component, ECS::MeshCollider::Component>();
            for (auto [entity, transform, collider] : view.each())
            {
                // World center: transform the local center point
                // Explicitly cast vec4 result to vec3
                glm::vec3 localCenter = collider.CollisionRef->LocalAABB.GetCenter();
                collider.WorldOBB.Center = glm::vec3(GetMatrix(transform) * glm::vec4(localCenter, 1.0f));

                // Extents: scale component-wise by absolute scale (handles negative/non-uniform scale)
                glm::vec3 localExtents = collider.CollisionRef->LocalAABB.GetExtents();
                collider.WorldOBB.Extents = localExtents * glm::abs(transform.Scale);

                // Rotation: object rotation in world space.
                collider.WorldOBB.Rotation = transform.Rotation;
            }
        }

        // ---------------------------------------------------------------------
        // Transform Gizmo: update BEFORE selection so active/hovered gizmo frames
        // can block scene interaction. Actual drawing/manipulation happens later
        // during the ImGui frame via the registered overlay callback.
        // ---------------------------------------------------------------------
        bool gizmoConsumedMouse = false;
        if (cameraComponent != nullptr)
        {
            gizmoConsumedMouse = m_Gizmo.Update(
                GetScene().GetRegistry(),
                *cameraComponent,
                m_Window->GetInput(),
                static_cast<uint32_t>(m_Window->GetWindowWidth()),
                static_cast<uint32_t>(m_Window->GetWindowHeight()),
                uiCapturesMouse);
        }

        // ---------------------------------------------------------------------
        // Debug Visualization: emit DebugDraw geometry BEFORE render system runs.
        // ImGui panels run AFTER render, so we emit here using settings from last frame.
        // ---------------------------------------------------------------------
        if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds || m_DrawSelectedColliderKDTree || m_DrawSelectedColliderBVH || m_DrawSelectedColliderConvexHull || m_DrawSelectedColliderContacts)
        {
            const entt::entity selected = m_CachedSelectedEntity;
            if (selected != entt::null && GetScene().GetRegistry().valid(selected))
            {
                auto& reg = GetScene().GetRegistry();
                auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected);
                auto* xf = reg.try_get<ECS::Components::Transform::Component>(selected);

                if (collider && collider->CollisionRef && xf)
                {
                    if (m_DrawSelectedColliderOctree)
                        EnsureRetainedOctreeOverlay(selected, *collider->CollisionRef, *xf);
                    else
                        ReleaseCachedOctreeOverlay();

                    if (m_DrawSelectedColliderBounds)
                        EnsureRetainedBoundsOverlay(selected, collider->CollisionRef->LocalAABB, collider->WorldOBB, *xf);
                    else
                        ReleaseRetainedLineOverlay(m_BoundsOverlay);

                    if (m_DrawSelectedColliderKDTree)
                        EnsureRetainedKDTreeOverlay(selected, *collider->CollisionRef, *xf);
                    else
                        ReleaseRetainedLineOverlay(m_KDTreeOverlay);

                    if (m_DrawSelectedColliderBVH)
                        EnsureRetainedBVHOverlay(selected, *collider->CollisionRef, *xf);
                    else
                        ReleaseRetainedLineOverlay(m_BVHOverlay);

                    if (m_DrawSelectedColliderConvexHull)
                        EnsureRetainedConvexHullOverlay(selected, *collider->CollisionRef, *xf);
                    else
                        ReleaseRetainedLineOverlay(m_ConvexHullOverlay);

                    // Contact manifolds use the transient DebugDraw path (not retained
                    // overlay) because they are derived from pairwise collider state each
                    // frame and include short-lived point/normal instrumentation.
                    ReleaseRetainedLineOverlay(m_ContactOverlay);
                    if (m_DrawSelectedColliderContacts)
                    {
                        auto& dd = GetRenderOrchestrator().GetDebugDraw();
                        auto colliders = reg.view<ECS::MeshCollider::Component>();

                        const uint32_t pointAColor = Graphics::DebugDraw::PackColorF(1.0f, 0.85f, 0.2f, 1.0f);
                        const uint32_t pointBColor = Graphics::DebugDraw::PackColorF(1.0f, 0.2f, 0.2f, 1.0f);
                        const uint32_t normalColor = Graphics::DebugDraw::PackColorF(0.2f, 0.85f, 1.0f, 1.0f);

                        for (auto [otherEntity, otherCollider] : colliders.each())
                        {
                            if (otherEntity == selected || !otherCollider.CollisionRef)
                                continue;

                            auto manifold = Geometry::ComputeContact(collider->WorldOBB, otherCollider.WorldOBB);
                            if (!manifold)
                                continue;

                            const glm::vec3 mid = (manifold->ContactPointA + manifold->ContactPointB) * 0.5f;
                            const glm::vec3 normalEnd = mid + manifold->Normal * (m_ContactNormalScale + manifold->PenetrationDepth);

                            if (m_ContactDebugOverlay)
                            {
                                dd.OverlaySphere(manifold->ContactPointA, m_ContactPointRadius, pointAColor, 12);
                                dd.OverlaySphere(manifold->ContactPointB, m_ContactPointRadius, pointBColor, 12);
                                dd.OverlayLine(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
                                dd.OverlayLine(mid, normalEnd, normalColor);
                            }
                            else
                            {
                                dd.Sphere(manifold->ContactPointA, m_ContactPointRadius, pointAColor, 12);
                                dd.Sphere(manifold->ContactPointB, m_ContactPointRadius, pointBColor, 12);
                                dd.Line(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
                                dd.Arrow(mid, normalEnd, glm::max(0.02f, m_ContactPointRadius), normalColor);
                            }
                        }
                    }
                }
                else
                {
                    ReleaseCachedOctreeOverlay();
                    ReleaseRetainedLineOverlay(m_BoundsOverlay);
                    ReleaseRetainedLineOverlay(m_KDTreeOverlay);
                    ReleaseRetainedLineOverlay(m_BVHOverlay);
                    ReleaseRetainedLineOverlay(m_ConvexHullOverlay);
                    ReleaseRetainedLineOverlay(m_ContactOverlay);
                }
            }
            else
            {
                ReleaseCachedOctreeOverlay();
                ReleaseRetainedLineOverlay(m_BoundsOverlay);
                ReleaseRetainedLineOverlay(m_KDTreeOverlay);
                ReleaseRetainedLineOverlay(m_BVHOverlay);
                ReleaseRetainedLineOverlay(m_ConvexHullOverlay);
                ReleaseRetainedLineOverlay(m_ContactOverlay);
            }
        }
        else
        {
            ReleaseCachedOctreeOverlay();
            ReleaseRetainedLineOverlay(m_BoundsOverlay);
            ReleaseRetainedLineOverlay(m_KDTreeOverlay);
            ReleaseRetainedLineOverlay(m_BVHOverlay);
            ReleaseRetainedLineOverlay(m_ConvexHullOverlay);
            ReleaseRetainedLineOverlay(m_ContactOverlay);
        }

        // ---------------------------------------------------------------------
        // Selection: delegate click-to-pick-to-registry-tags to the Engine module.
        // ---------------------------------------------------------------------
        if (cameraComponent != nullptr)
        {
            auto& renderSys = GetRenderOrchestrator().GetRenderSystem();

            // Keep module config in sync with the UI setting.
            GetSelection().GetConfig().MouseButton = m_SelectMouseButton;

            GetSelection().Update(
                GetScene(),
                renderSys,
                cameraComponent,
                *m_Window,
                uiCapturesMouse || gizmoConsumedMouse);

            // Draw
            renderSys.OnUpdate(GetScene(), *cameraComponent, GetAssetManager());
        }
    }

    void OnRender() override
    {
    }

    void OnRegisterSystems(Core::FrameGraph& graph, float deltaTime) override
    {
        using namespace Core::Hash;
        if (GetFeatureRegistry().IsEnabled("AxisRotator"_id))
            ECS::Systems::AxisRotator::RegisterSystem(graph, GetScene().GetRegistry(), deltaTime);
    }

    void DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy");

        // Editor settings
        if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Pick mouse button:");
            ImGui::SameLine();
            ImGui::RadioButton("LMB", &m_SelectMouseButton, 0);
            ImGui::SameLine();
            ImGui::RadioButton("RMB", &m_SelectMouseButton, 1);
            ImGui::SameLine();
            ImGui::RadioButton("MMB", &m_SelectMouseButton, 2);
        }

        const entt::entity selected = m_CachedSelectedEntity;

        GetScene().GetRegistry().view<entt::entity>().each([&](auto entityID)
        {
            if (GetScene().GetRegistry().all_of<HiddenEditorEntityTag>(entityID))
                return;

            // Try to get tag, default to "Entity"
            std::string name = "Entity";
            if (GetScene().GetRegistry().all_of<ECS::Components::NameTag::Component>(entityID))
            {
                name = GetScene().GetRegistry().get<ECS::Components::NameTag::Component>(entityID).Name;
            }

            // Selection flags
            ImGuiTreeNodeFlags flags = ((selected == entityID) ? ImGuiTreeNodeFlags_Selected : 0) |
                ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)), flags, "%s",
                                            name.c_str());

            if (ImGui::IsItemClicked())
            {
                GetSelection().SetSelectedEntity(GetScene(), entityID);
            }

            if (opened)
            {
                ImGui::TreePop();
            }
        });

        // Deselect only when clicking empty space in the hierarchy window.
        // (The previous IsMouseDown(...) version cleared selection even when clicking an item.)
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            GetSelection().ClearSelection(GetScene());
        }

        // Context Menu for creating new entities
        if (ImGui::BeginPopupContextWindow(nullptr, 1))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                GetScene().CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Create Demo Point Cloud"))
            {
                SpawnDemoPointCloud();
            }
            if (ImGui::MenuItem("Remove Entity"))
            {
                const entt::entity cur = m_CachedSelectedEntity;
                if (cur != entt::null && GetScene().GetRegistry().valid(cur))
                {
                    GetScene().GetRegistry().destroy(cur);
                    GetSelection().ClearSelection(GetScene());
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void ApplyGeometryOperator(entt::entity entity, const std::function<void(Geometry::Halfedge::Mesh&)>& op)
    {
        auto& reg = GetScene().GetRegistry();
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
        Geometry::MeshUtils::CalculateNormals(collider->CollisionRef->Positions, collider->CollisionRef->Indices, newNormals);

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
            auto aabb = Geometry::AABB{collider->CollisionRef->Positions[i0], collider->CollisionRef->Positions[i0]};
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i1]);
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        static_cast<void>(collider->CollisionRef->LocalOctree.Build(primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));

        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collider->CollisionRef->Positions;
        uploadReq.Indices = collider->CollisionRef->Indices;
        uploadReq.Normals = newNormals;
        uploadReq.Aux = newAux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            GetDeviceShared(), GetGraphicsBackend().GetTransferManager(), uploadReq, &GetGeometryStorage());

        auto oldHandle = sc->Geometry;
        sc->Geometry = GetGeometryStorage().Add(std::move(gpuData));

        if (oldHandle.IsValid())
        {
            GetGeometryStorage().Remove(oldHandle, GetDevice().GetGlobalFrameNumber());
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

        GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
    }

    // =========================================================================
    // SpawnDemoPointCloud — Create a hemisphere point cloud entity with normals,
    // colors, and estimated radii for testing point rendering.
    // =========================================================================
    void SpawnDemoPointCloud()
    {
        // Generate a uniform hemisphere point cloud (Fibonacci lattice on the
        // upper hemisphere for even angular spacing). N ≈ 500 points.
        constexpr std::size_t N = 500;
        constexpr float radius = 1.0f;

        Geometry::PointCloud::Cloud cloud;
        cloud.Reserve(N);
        cloud.EnableNormals();
        cloud.EnableColors();

        const float goldenRatio = (1.0f + std::sqrt(5.0f)) * 0.5f;
        const float goldenAngle = 2.0f * glm::pi<float>() / (goldenRatio * goldenRatio);

        for (std::size_t i = 0; i < N; ++i)
        {
            // Uniform distribution on the sphere via Fibonacci lattice
            const float t = static_cast<float>(i) / static_cast<float>(N - 1);
            const float phi = std::acos(1.0f - 2.0f * t);   // polar angle [0, π]
            const float theta = goldenAngle * static_cast<float>(i);

            // Only keep upper hemisphere (y > 0)
            const float y = std::cos(phi);
            if (y < -0.05f) continue;

            const float sinPhi = std::sin(phi);
            const float x = sinPhi * std::cos(theta);
            const float z = sinPhi * std::sin(theta);

            const glm::vec3 pos    = glm::vec3(x, y, z) * radius;
            const glm::vec3 normal = glm::normalize(pos);

            // Color: height-based gradient (blue at base → orange at top)
            const float h = (y + 0.05f) / 1.05f; // normalize to [0,1]
            const glm::vec4 color = glm::mix(
                glm::vec4(0.2f, 0.4f, 0.9f, 1.0f),  // blue
                glm::vec4(1.0f, 0.6f, 0.1f, 1.0f),  // orange
                h);

            auto ph = cloud.AddPoint(pos);
            cloud.Normal(ph) = normal;
            cloud.Color(ph)  = color;
        }

        if (cloud.Empty())
        {
            Log::Warn("SpawnDemoPointCloud: no points generated.");
            return;
        }

        // Estimate per-point radii from local density (Octree kNN).
        Geometry::PointCloud::RadiusEstimationParams radiiParams;
        radiiParams.KNeighbors = 6;
        radiiParams.ScaleFactor = 1.2f; // Slight overlap for hole-free rendering
        auto radiiResult = Geometry::PointCloud::EstimateRadii(cloud, radiiParams);

        // Create the ECS entity
        auto& scene = GetScene();
        entt::entity entity = scene.CreateEntity("Demo Point Cloud");

        // PointCloud::Data component (Cloud-backed canonical path)
        auto& pcd = scene.GetRegistry().emplace<ECS::PointCloud::Data>(entity);
        pcd.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>(std::move(cloud));
        pcd.GpuDirty = true;
        pcd.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        pcd.DefaultRadius = 0.02f;
        pcd.SizeMultiplier = 1.0f;

        // Make it selectable
        scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(entity);
        static uint32_t s_PointCloudPickId = 10000u;
        scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(entity, s_PointCloudPickId++);

        Log::Info("Spawned demo point cloud: {} points, normals={}, radii={}",
                  pcd.PointCount(), pcd.HasNormals() ? "yes" : "no", pcd.HasRadii() ? "yes" : "no");
    }

    void DrawGeometryRemeshingPanel()
    {
        const auto context = GetGeometrySelectionContext();
        if (DrawGeometryOperatorPanelHeader(context,
                "Use remeshing to regularize edge lengths. Isotropic remeshing targets a uniform metric; adaptive remeshing keeps room for size-field-driven workflows while still sharing the same mesh pipeline."))
        {
            ImGui::DragFloat("Target Length", &m_GeometryRemeshingUi.TargetLength, 0.01f, 0.001f, 10.0f);
            ImGui::DragInt("Iterations", &m_GeometryRemeshingUi.Iterations, 1.0f, 1, 20);
            ImGui::Checkbox("Preserve Boundary", &m_GeometryRemeshingUi.PreserveBoundary);

            ImGui::SeparatorText("Approaches");
            ImGui::TextDisabled("Uniform target edge length for evenly distributed tessellation.");
            if (ImGui::Button("Run Isotropic Remeshing"))
            {
                const auto ui = m_GeometryRemeshingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
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
                const auto ui = m_GeometryRemeshingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
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

    void DrawGeometrySimplificationPanel()
    {
        const auto context = GetGeometrySelectionContext();
        if (DrawGeometryOperatorPanelHeader(context,
                "Simplification reduces triangle count while preserving overall shape. Keep this panel separate from remeshing and smoothing so decimation can be inserted wherever a workflow needs it."))
        {
            ImGui::DragInt("Target Faces", &m_GeometrySimplificationUi.TargetFaces, 10.0f, 10, 1000000);
            ImGui::Checkbox("Preserve Boundary", &m_GeometrySimplificationUi.PreserveBoundary);
            if (ImGui::Button("Run QEM Simplification"))
            {
                const auto ui = m_GeometrySimplificationUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Simplification::SimplificationParams params;
                    params.TargetFaces = ui.TargetFaces;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Simplification::Simplify(mesh, params));
                });
            }
        }
    }

    void DrawGeometrySmoothingPanel()
    {
        const auto context = GetGeometrySelectionContext();
        if (DrawGeometryOperatorPanelHeader(context,
                "Smoothing approaches stay together so you can compare differential operators without hunting through unrelated UI. They remain independently accessible from the Geometry menu and can still be chained after remeshing or before subdivision."))
        {
            ImGui::DragInt("Iterations", &m_GeometrySmoothingUi.Iterations, 1.0f, 1, 100);
            ImGui::DragFloat("Lambda", &m_GeometrySmoothingUi.Lambda, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Preserve Boundary", &m_GeometrySmoothingUi.PreserveBoundary);

            ImGui::SeparatorText("Approaches");
            if (ImGui::Button("Run Uniform Laplacian"))
            {
                const auto ui = m_GeometrySmoothingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Smoothing::SmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::UniformLaplacian(mesh, params));
                });
            }
            if (ImGui::Button("Run Cotan Laplacian"))
            {
                const auto ui = m_GeometrySmoothingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Smoothing::SmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::CotanLaplacian(mesh, params));
                });
            }
            if (ImGui::Button("Run Taubin Smoothing"))
            {
                const auto ui = m_GeometrySmoothingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Smoothing::TaubinParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::Taubin(mesh, params));
                });
            }
            if (ImGui::Button("Run Implicit Smoothing"))
            {
                const auto ui = m_GeometrySmoothingUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Smoothing::ImplicitSmoothingParams params;
                    params.Iterations = ui.Iterations;
                    params.Lambda = ui.Lambda;
                    params.PreserveBoundary = ui.PreserveBoundary;
                    static_cast<void>(Geometry::Smoothing::ImplicitLaplacian(mesh, params));
                });
            }
        }
    }

    void DrawGeometrySubdivisionPanel()
    {
        const auto context = GetGeometrySelectionContext();
        if (DrawGeometryOperatorPanelHeader(context,
                "Subdivision is kept distinct from smoothing and remeshing because it changes topology with a refinement-first workflow. Open it alongside repair or smoothing when building higher-resolution assets."))
        {
            ImGui::DragInt("Iterations", &m_GeometrySubdivisionUi.Iterations, 1.0f, 1, 5);
            if (ImGui::Button("Run Loop Subdivision"))
            {
                const auto ui = m_GeometrySubdivisionUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Halfedge::Mesh out;
                    Geometry::Subdivision::SubdivisionParams params;
                    params.Iterations = ui.Iterations;
                    if (Geometry::Subdivision::Subdivide(mesh, out, params))
                        mesh = std::move(out);
                });
            }
            if (ImGui::Button("Run Catmull-Clark Subdivision"))
            {
                const auto ui = m_GeometrySubdivisionUi;
                ApplyGeometryOperator(context.Selected, [ui](Geometry::Halfedge::Mesh& mesh) {
                    Geometry::Halfedge::Mesh out;
                    Geometry::CatmullClark::SubdivisionParams params;
                    params.Iterations = ui.Iterations;
                    if (Geometry::CatmullClark::Subdivide(mesh, out, params))
                        mesh = std::move(out);
                });
            }
        }
    }

    void DrawGeometryRepairPanel()
    {
        const auto context = GetGeometrySelectionContext();
        if (DrawGeometryOperatorPanelHeader(context,
                "Repair stays as a standalone cleanup pass so it can be inserted before or after heavier operators without dragging the rest of the geometry UI along."))
        {
            if (ImGui::Button("Run Mesh Repair"))
            {
                ApplyGeometryOperator(context.Selected, [](Geometry::Halfedge::Mesh& mesh) {
                    static_cast<void>(Geometry::MeshRepair::Repair(mesh));
                });
            }
        }
    }

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
                    case Graphics::PropertyDataType::Scalar: typeLabel = " [float]"; break;
                    case Graphics::PropertyDataType::Vec3:   typeLabel = " [vec3]"; break;
                    case Graphics::PropertyDataType::Vec4:   typeLabel = " [vec4]"; break;
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
        const char* mapNames[] = { "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat" };
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
        for (size_t i = 0; i < config.VectorFields.size(); )
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
                const char* colorPreview = vf.ColorPropertyName.empty() ? "(Uniform)" : vf.ColorPropertyName.c_str();
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
                const char* lenPreview = vf.LengthPropertyName.empty() ? "(Uniform)" : vf.LengthPropertyName.c_str();
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

    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        const entt::entity selected = m_CachedSelectedEntity;

        if (selected != entt::null && GetScene().GetRegistry().valid(selected))
        {
            auto& reg = GetScene().GetRegistry();

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
                    Graphics::GeometryGpuData* geo = GetGeometryStorage().GetUnchecked(sc.Geometry);

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
            // Graph::Data fields propagate to Line/Point components via
            // GraphGeometrySyncSystem each frame, so edits here take effect
            // immediately without manual component manipulation.
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
                    const char* modeNames[] = { "Flat Disc", "Surfel", "EWA Splatting", "Sphere" };
                    int modeIdx = static_cast<int>(gd.NodeRenderMode);
                    if (modeIdx < 0 || modeIdx > 3) modeIdx = 0;
                    if (ImGui::Combo("Node Render Mode", &modeIdx, modeNames, 4))
                        gd.NodeRenderMode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);
                    ImGui::SliderFloat("Node Size", &gd.DefaultNodeRadius, 0.0005f, 0.05f, "%.5f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Node Size Multiplier", &gd.NodeSizeMultiplier, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                    float nc[4] = {gd.DefaultNodeColor.r, gd.DefaultNodeColor.g, gd.DefaultNodeColor.b, gd.DefaultNodeColor.a};
                    if (ImGui::ColorEdit4("Node Color", nc))
                        gd.DefaultNodeColor = glm::vec4(nc[0], nc[1], nc[2], nc[3]);

                    ImGui::SeparatorText("Edge Settings");
                    float ec[4] = {gd.DefaultEdgeColor.r, gd.DefaultEdgeColor.g, gd.DefaultEdgeColor.b, gd.DefaultEdgeColor.a};
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

                        if (ColorSourceWidget("Node Color Source", gd.Visualization.VertexColors, vtxPs, "GraphVtx"))
                            reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                        if (ColorSourceWidget("Edge Color Source", gd.Visualization.EdgeColors, edgePs, "GraphEdge"))
                            reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);

                        VectorFieldWidget(gd.Visualization, vtxPs, "GraphVF");
                    }
                }
            }

            // 5. Point Cloud — PropertySet-backed (PointCloud::Data).
            // PointCloud::Data fields propagate to Point::Component via
            // PointCloudGeometrySyncSystem each frame.
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

                    const char* modeNames[] = { "Flat Disc", "Surfel", "EWA Splatting", "Sphere" };
                    int modeIdx = static_cast<int>(pcd.RenderMode);
                    if (modeIdx < 0 || modeIdx > 3) modeIdx = 0;
                    if (ImGui::Combo("Render Mode##PCD", &modeIdx, modeNames, 4))
                        pcd.RenderMode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);

                    ImGui::SliderFloat("Default Radius##PCD", &pcd.DefaultRadius, 0.0005f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Size Multiplier##PCD", &pcd.SizeMultiplier, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

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
            // Toggle is presence/absence of Line::Component and Point::Component.
            // Surface visibility is controlled via Surface::Component::Visible.
            // GPU view creation is handled by MeshViewLifecycleSystem — no manual
            // allocation needed in the Inspector.
            // (Graph and PointCloud entities manage their Line/Point components
            // via lifecycle systems — their controls are in sections 4/5/6 above.)
            if (reg.all_of<ECS::Surface::Component>(selected))
            {
                // Determine topology for context-appropriate labels.
                auto& sc = reg.get<ECS::Surface::Component>(selected);
                Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
                if (auto* geo = GetGeometryStorage().GetUnchecked(sc.Geometry))
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
                        const char* modeNames[] = { "Flat Disc", "Surfel", "EWA Splatting" };
                        int modeIdx = static_cast<int>(pt->Mode);
                        if (modeIdx < 0 || modeIdx > 2) modeIdx = 0;
                        if (ImGui::Combo("Render Mode", &modeIdx, modeNames, 3))
                            pt->Mode = static_cast<Geometry::PointCloud::RenderMode>(modeIdx);
                        ImGui::SliderFloat("Vertex Size", &pt->Size, 0.0005f, 0.05f, "%.5f", ImGuiSliderFlags_Logarithmic);
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

                            if (ColorSourceWidget("Vertex Color Source", md->Visualization.VertexColors, vtxPs, "MeshVtx"))
                            {
                                md->AttributesDirty = true;
                                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                            }
                            if (ColorSourceWidget("Edge Color Source", md->Visualization.EdgeColors, edgePs, "MeshEdge"))
                            {
                                md->AttributesDirty = true;
                                reg.emplace_or_replace<ECS::DirtyTag::EdgeAttributes>(selected);
                            }
                            if (ColorSourceWidget("Face Color Source", md->Visualization.FaceColors, facePs, "MeshFace"))
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
};

int main(int argc, char* argv[])
{
    Runtime::EngineConfig config{};
    config.AppName = "Sandbox";
    config.Width = 1600;
    config.Height = 900;

    // Parse command-line arguments for benchmark mode.
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--benchmark" && i + 1 < argc)
        {
            config.BenchmarkMode = true;
            config.BenchmarkFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--warmup" && i + 1 < argc)
        {
            config.BenchmarkWarmupFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--out" && i + 1 < argc)
        {
            config.BenchmarkOutputPath = argv[++i];
        }
    }

    SandboxApp app(config);
    app.Run();
    return 0;
}
