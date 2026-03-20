// Runtime.EditorUI.SpatialDebugController — Spatial structure debug
// visualization (Octree, Bounds, KDTree, BVH, ConvexHull, Contact manifolds).
// Manages retained line overlays with change-detection caching and transient
// contact manifold emission.

module;

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <entt/entity/registry.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Graphics.Components;
import Graphics.BVHDebugDraw;
import Graphics.BoundingDebugDraw;
import Graphics.ConvexHullDebugDraw;
import Graphics.DebugDraw;
import Graphics.Geometry;
import Graphics.KDTreeDebugDraw;
import Graphics.OctreeDebugDraw;
import Geometry;
import ECS;

namespace
{
    // Shared helper to reduce glm::vec3 <-> float[3] boilerplate for ImGui color edits.
    bool ColorEdit3(const char* label, glm::vec3& color)
    {
        float rgb[3] = {color.r, color.g, color.b};
        if (ImGui::ColorEdit3(label, rgb))
        {
            color = glm::vec3(rgb[0], rgb[1], rgb[2]);
            return true;
        }
        return false;
    }
} // anonymous namespace

namespace Runtime::EditorUI
{

// =========================================================================
// Public interface
// =========================================================================

bool SpatialDebugController::AnyActive() const
{
    return DrawOctree || DrawBounds || DrawKDTree || DrawBVH || DrawConvexHull || DrawContacts;
}

void SpatialDebugController::Update(Runtime::Engine& engine, entt::entity selected)
{
    if (!AnyActive())
    {
        ReleaseAll(engine);
        return;
    }

    if (selected == entt::null || !engine.GetScene().GetRegistry().valid(selected))
    {
        ReleaseAll(engine);
        return;
    }

    auto& reg = engine.GetScene().GetRegistry();
    auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected);
    auto* xf = reg.try_get<ECS::Components::Transform::Component>(selected);

    if (!collider || !collider->CollisionRef || !xf)
    {
        ReleaseAll(engine);
        return;
    }

    if (DrawOctree)
        EnsureRetainedOctreeOverlay(engine, selected, *collider->CollisionRef, *xf);
    else
        ReleaseCachedOctreeOverlay(engine);

    if (DrawBounds)
        EnsureRetainedBoundsOverlay(engine, selected, collider->CollisionRef->LocalAABB, collider->WorldOBB,
                                    *xf);
    else
        ReleaseRetainedLineOverlay(engine, m_BoundsOverlay);

    if (DrawKDTree)
        EnsureRetainedKDTreeOverlay(engine, selected, *collider->CollisionRef, *xf);
    else
        ReleaseRetainedLineOverlay(engine, m_KDTreeOverlay);

    if (DrawBVH)
        EnsureRetainedBVHOverlay(engine, selected, *collider->CollisionRef, *xf);
    else
        ReleaseRetainedLineOverlay(engine, m_BVHOverlay);

    if (DrawConvexHull)
        EnsureRetainedConvexHullOverlay(engine, selected, *collider->CollisionRef, *xf);
    else
        ReleaseRetainedLineOverlay(engine, m_ConvexHullOverlay);

    ReleaseRetainedLineOverlay(engine, m_ContactOverlay);
    if (DrawContacts)
        EmitContactManifolds(engine, selected, *collider);
}

void SpatialDebugController::ReleaseAll(Runtime::Engine& engine)
{
    ReleaseCachedOctreeOverlay(engine);
    ReleaseRetainedLineOverlay(engine, m_BoundsOverlay);
    ReleaseRetainedLineOverlay(engine, m_KDTreeOverlay);
    ReleaseRetainedLineOverlay(engine, m_BVHOverlay);
    ReleaseRetainedLineOverlay(engine, m_ConvexHullOverlay);
    ReleaseRetainedLineOverlay(engine, m_ContactOverlay);
}

void SpatialDebugController::DrawUI(Runtime::Engine& engine)
{
    ImGui::Spacing();
    ImGui::SeparatorText("Spatial Debug");

    ImGui::Checkbox("Draw Selected MeshCollider Octree", &DrawOctree);
    ImGui::Checkbox("Draw Selected MeshCollider Bounds", &DrawBounds);
    ImGui::Checkbox("Draw Selected MeshCollider KD-Tree", &DrawKDTree);
    ImGui::Checkbox("Draw Selected MeshCollider BVH", &DrawBVH);
    ImGui::Checkbox("Draw Selected MeshCollider Convex Hull", &DrawConvexHull);
    ImGui::Checkbox("Draw Contact Manifolds", &DrawContacts);
    ImGui::Checkbox("Bounds Overlay (no depth test)", &BoundsSettings.Overlay);
    ImGui::Checkbox("Draw World AABB", &BoundsSettings.DrawAABB);
    ImGui::Checkbox("Draw World OBB", &BoundsSettings.DrawOBB);
    ImGui::Checkbox("Draw Bounding Sphere", &BoundsSettings.DrawBoundingSphere);
    ImGui::SliderFloat("Bounds Alpha", &BoundsSettings.Alpha, 0.05f, 1.0f, "%.2f");

    ColorEdit3("AABB Color", BoundsSettings.AABBColor);
    ColorEdit3("OBB Color", BoundsSettings.OBBColor);
    ColorEdit3("Sphere Color", BoundsSettings.SphereColor);

    ImGui::SeparatorText("KD-Tree");
    ImGui::Checkbox("KD Overlay (no depth test)", &KDTreeSettings.Overlay);
    ImGui::Checkbox("KD Leaf Only", &KDTreeSettings.LeafOnly);
    ImGui::Checkbox("KD Draw Internal", &KDTreeSettings.DrawInternal);
    ImGui::Checkbox("KD Occupied Only", &KDTreeSettings.OccupiedOnly);
    ImGui::Checkbox("KD Draw Split Planes", &KDTreeSettings.DrawSplitPlanes);
    ImGui::SliderInt("KD Max Depth", reinterpret_cast<int*>(&KDTreeSettings.MaxDepth), 0, 32);
    ImGui::SliderFloat("KD Alpha", &KDTreeSettings.Alpha, 0.05f, 1.0f, "%.2f");

    ColorEdit3("KD Leaf Color", KDTreeSettings.LeafColor);
    ColorEdit3("KD Internal Color", KDTreeSettings.InternalColor);
    ColorEdit3("KD Split Color", KDTreeSettings.SplitPlaneColor);

    ImGui::SeparatorText("Convex Hull");
    ImGui::Checkbox("Hull Overlay (no depth test)", &ConvexHullSettings.Overlay);
    ImGui::SliderFloat("Hull Alpha", &ConvexHullSettings.Alpha, 0.05f, 1.0f, "%.2f");
    ColorEdit3("Hull Color", ConvexHullSettings.Color);

    ImGui::SeparatorText("BVH");
    ImGui::Checkbox("BVH Overlay (no depth test)", &BVHSettings.Overlay);
    ImGui::Checkbox("BVH Leaf Only", &BVHSettings.LeafOnly);
    ImGui::Checkbox("BVH Draw Internal", &BVHSettings.DrawInternal);
    ImGui::SliderInt("BVH Max Depth", reinterpret_cast<int*>(&BVHSettings.MaxDepth), 0, 32);
    ImGui::SliderInt("BVH Leaf Triangles", reinterpret_cast<int*>(&BVHSettings.LeafTriangleCount), 1, 64);
    ImGui::SliderFloat("BVH Alpha", &BVHSettings.Alpha, 0.05f, 1.0f, "%.2f");

    ColorEdit3("BVH Leaf Color", BVHSettings.LeafColor);
    ColorEdit3("BVH Internal Color", BVHSettings.InternalColor);

    ImGui::SeparatorText("Octree");
    ImGui::Checkbox("Overlay (no depth test)", &OctreeSettings.Overlay);
    ImGui::Checkbox("Leaf Only", &OctreeSettings.LeafOnly);
    ImGui::Checkbox("Occupied Only", &OctreeSettings.OccupiedOnly);
    ImGui::Checkbox("Color By Depth", &OctreeSettings.ColorByDepth);
    ImGui::SliderInt("Max Depth", reinterpret_cast<int*>(&OctreeSettings.MaxDepth), 0, 16);
    ImGui::SliderFloat("Alpha", &OctreeSettings.Alpha, 0.05f, 1.0f, "%.2f");

    if (!OctreeSettings.ColorByDepth)
        ColorEdit3("Base Color", OctreeSettings.BaseColor);

    ImGui::SeparatorText("Contact Manifold");
    ImGui::Checkbox("Contact Overlay (no depth test)", &ContactOverlay);
    ImGui::SliderFloat("Normal Scale", &ContactNormalScale, 0.05f, 2.0f, "%.2f");
    ImGui::SliderFloat("Point Radius", &ContactPointRadius, 0.005f, 0.2f, "%.3f");

    if (AnyActive())
    {
        const entt::entity selected = engine.GetSelection().GetSelectedEntity(engine.GetScene());
        if (selected == entt::null || !engine.GetScene().GetRegistry().valid(selected))
        {
            ImGui::TextDisabled("No valid selected entity.");
        }
        else
        {
            auto* collider = engine.GetScene().GetRegistry().try_get<ECS::MeshCollider::Component>(selected);
            if (!collider || !collider->CollisionRef)
            {
                ImGui::TextDisabled("Selected entity has no MeshCollider.");
            }
        }
    }
}

// =========================================================================
// Shared overlay lifecycle helpers
// =========================================================================

void SpatialDebugController::ReleaseRetainedLineOverlay(Runtime::Engine& engine, RetainedLineOverlaySlot& slot)
{
    auto& reg = engine.GetScene().GetRegistry();
    if (slot.Entity != entt::null && reg.valid(slot.Entity))
        reg.destroy(slot.Entity);

    if (slot.Geometry.IsValid())
        engine.GetGeometryStorage().Remove(slot.Geometry, engine.GetDevice().GetGlobalFrameNumber());

    slot = {};
}

bool SpatialDebugController::UpdateRetainedLineOverlay(Runtime::Engine& engine, RetainedLineOverlaySlot& slot,
                                                        const std::function<void(Graphics::DebugDraw&)>& emit)
{
    Graphics::DebugDraw capture;
    emit(capture);

    if (!capture.GetTriangles().empty() || !capture.GetPoints().empty())
    {
        ReleaseRetainedLineOverlay(engine, slot);
        return false;
    }

    const auto depthLines = capture.GetLines();
    const auto overlayLines = capture.GetOverlayLines();
    if (depthLines.empty() && overlayLines.empty())
    {
        ReleaseRetainedLineOverlay(engine, slot);
        return false;
    }
    if (!depthLines.empty() && !overlayLines.empty())
    {
        ReleaseRetainedLineOverlay(engine, slot);
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
        engine.GetDeviceShared(), engine.GetGraphicsBackend().GetTransferManager(), upload,
        &engine.GetGeometryStorage());
    (void)token;
    if (!gpuData)
    {
        ReleaseRetainedLineOverlay(engine, slot);
        return false;
    }

    const Geometry::GeometryHandle oldGeometry = slot.Geometry;
    const Geometry::GeometryHandle newGeometry = engine.GetGeometryStorage().Add(std::move(gpuData));

    auto& reg = engine.GetScene().GetRegistry();
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
        engine.GetGeometryStorage().Remove(oldGeometry, engine.GetDevice().GetGlobalFrameNumber());

    return true;
}

// =========================================================================
// Cached collider helpers
// =========================================================================

bool SpatialDebugController::EnsureSelectedColliderKDTree(entt::entity selected,
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

bool SpatialDebugController::EnsureSelectedColliderConvexHull(entt::entity selected,
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

// =========================================================================
// Octree overlay (specialized upload)
// =========================================================================

void SpatialDebugController::ReleaseCachedOctreeOverlay(Runtime::Engine& engine)
{
    auto& reg = engine.GetScene().GetRegistry();
    if (m_OctreeOverlayEntity != entt::null && reg.valid(m_OctreeOverlayEntity))
        reg.destroy(m_OctreeOverlayEntity);

    if (m_OctreeOverlayGeometry.IsValid())
    {
        engine.GetGeometryStorage().Remove(m_OctreeOverlayGeometry, engine.GetDevice().GetGlobalFrameNumber());
        m_OctreeOverlayGeometry = {};
    }

    m_OctreeOverlayEntity = entt::null;
    m_OctreeOverlaySourceEntity = entt::null;
    m_CachedOctreeWorld = glm::mat4(1.0f);
    m_CachedOctreeSettings = {};
    m_CachedOctreeLocalAABB = {};
    m_HasCachedOctreeAabb = false;
}

bool SpatialDebugController::EnsureRetainedOctreeOverlay(Runtime::Engine& engine, entt::entity selected,
                                                          const Graphics::GeometryCollisionData& collision,
                                                          const ECS::Components::Transform::Component& xf)
{
    Graphics::OctreeDebugDrawSettings settings = OctreeSettings;
    settings.Enabled = true;

    const glm::mat4 worldMatrix = ECS::Components::Transform::GetMatrix(xf);
    const bool cacheValid =
        (m_OctreeOverlayEntity != entt::null) &&
        engine.GetScene().GetRegistry().valid(m_OctreeOverlayEntity) &&
        m_OctreeOverlayGeometry.IsValid() &&
        (m_OctreeOverlaySourceEntity == selected) &&
        OctreeSettingsEqual(m_CachedOctreeSettings, settings) &&
        MatricesNearlyEqual(m_CachedOctreeWorld, worldMatrix) &&
        m_HasCachedOctreeAabb &&
        Vec3NearlyEqual(m_CachedOctreeLocalAABB.Min, collision.LocalAABB.Min) &&
        Vec3NearlyEqual(m_CachedOctreeLocalAABB.Max, collision.LocalAABB.Max);

    if (cacheValid)
        return true;

    if (collision.LocalOctree.m_Nodes.empty())
    {
        ReleaseCachedOctreeOverlay(engine);
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

    struct StackItem
    {
        Geometry::Octree::NodeIndex Node;
        std::uint32_t Depth;
    };
    std::array<StackItem, kMaxOctreeTraversalStack> stack{};
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
        ReleaseCachedOctreeOverlay(engine);
        return false;
    }

    Graphics::GeometryUploadRequest upload{};
    upload.Positions = positions;
    upload.Indices = indices;
    upload.Topology = Graphics::PrimitiveTopology::Lines;
    upload.UploadMode = Graphics::GeometryUploadMode::Direct;

    auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
        engine.GetDeviceShared(), engine.GetGraphicsBackend().GetTransferManager(), upload,
        &engine.GetGeometryStorage());
    (void)token;
    if (!gpuData)
    {
        ReleaseCachedOctreeOverlay(engine);
        return false;
    }

    const Geometry::GeometryHandle oldGeometry = m_OctreeOverlayGeometry;
    const Geometry::GeometryHandle newGeometry = engine.GetGeometryStorage().Add(std::move(gpuData));

    auto& reg = engine.GetScene().GetRegistry();
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
    m_CachedOctreeSettings = settings;
    m_CachedOctreeWorld = worldMatrix;
    m_CachedOctreeLocalAABB = collision.LocalAABB;
    m_HasCachedOctreeAabb = true;

    if (oldGeometry.IsValid() && oldGeometry != newGeometry)
        engine.GetGeometryStorage().Remove(oldGeometry, engine.GetDevice().GetGlobalFrameNumber());

    return true;
}

// =========================================================================
// Bounds overlay
// =========================================================================

bool SpatialDebugController::EnsureRetainedBoundsOverlay(Runtime::Engine& engine, entt::entity selected,
                                                          const Geometry::AABB& localAabb,
                                                          const Geometry::OBB& worldObb,
                                                          const ECS::Components::Transform::Component& xf)
{
    const glm::mat4 worldMatrix = ECS::Components::Transform::GetMatrix(xf);
    const bool cacheValid =
        (m_BoundsOverlaySourceEntity == selected) &&
        m_BoundsOverlay.Geometry.IsValid() &&
        OctreeSettingsEqual(
            {
                .Enabled = true, .Overlay = m_CachedBoundsSettings.Overlay, .ColorByDepth = false,
                .MaxDepth = 0u, .LeafOnly = false, .DrawInternal = false, .OccupiedOnly = false,
                .Alpha = m_CachedBoundsSettings.Alpha, .BaseColor = m_CachedBoundsSettings.AABBColor
            },
            {
                .Enabled = true, .Overlay = BoundsSettings.Overlay, .ColorByDepth = false, .MaxDepth = 0u,
                .LeafOnly = false, .DrawInternal = false, .OccupiedOnly = false, .Alpha = BoundsSettings.Alpha,
                .BaseColor = BoundsSettings.AABBColor
            }) &&
        m_CachedBoundsSettings.DrawAABB == BoundsSettings.DrawAABB &&
        m_CachedBoundsSettings.DrawOBB == BoundsSettings.DrawOBB &&
        m_CachedBoundsSettings.DrawBoundingSphere == BoundsSettings.DrawBoundingSphere &&
        Vec3NearlyEqual(m_CachedBoundsSettings.AABBColor, BoundsSettings.AABBColor) &&
        Vec3NearlyEqual(m_CachedBoundsSettings.OBBColor, BoundsSettings.OBBColor) &&
        Vec3NearlyEqual(m_CachedBoundsSettings.SphereColor, BoundsSettings.SphereColor) &&
        MatricesNearlyEqual(m_CachedBoundsWorld, worldMatrix) &&
        m_HasCachedBoundsAabb &&
        Vec3NearlyEqual(m_CachedBoundsLocalAabb.Min, localAabb.Min) &&
        Vec3NearlyEqual(m_CachedBoundsLocalAabb.Max, localAabb.Max);

    if (cacheValid)
        return true;

    Graphics::BoundingDebugDrawSettings settings = BoundsSettings;
    settings.Enabled = true;
    const bool ok = UpdateRetainedLineOverlay(engine, m_BoundsOverlay, [&](Graphics::DebugDraw& dd)
    {
        DrawBoundingVolumes(dd, localAabb, worldObb, settings);
    });

    if (!ok)
        return false;

    m_BoundsOverlaySourceEntity = selected;
    m_CachedBoundsSettings = settings;
    m_CachedBoundsWorld = worldMatrix;
    m_CachedBoundsLocalAabb = localAabb;
    m_HasCachedBoundsAabb = true;
    return true;
}

// =========================================================================
// KD-Tree overlay
// =========================================================================

bool SpatialDebugController::EnsureRetainedKDTreeOverlay(Runtime::Engine& engine, entt::entity selected,
                                                          const Graphics::GeometryCollisionData& collision,
                                                          const ECS::Components::Transform::Component& xf)
{
    if (!EnsureSelectedColliderKDTree(selected, collision))
    {
        ReleaseRetainedLineOverlay(engine, m_KDTreeOverlay);
        m_KDTreeOverlaySourceEntity = entt::null;
        return false;
    }

    const glm::mat4 worldMatrix = ECS::Components::Transform::GetMatrix(xf);
    const bool cacheValid =
        (m_KDTreeOverlaySourceEntity == selected) &&
        m_KDTreeOverlay.Geometry.IsValid() &&
        m_CachedKDTreeSettings.Overlay == KDTreeSettings.Overlay &&
        m_CachedKDTreeSettings.LeafOnly == KDTreeSettings.LeafOnly &&
        m_CachedKDTreeSettings.DrawInternal == KDTreeSettings.DrawInternal &&
        m_CachedKDTreeSettings.OccupiedOnly == KDTreeSettings.OccupiedOnly &&
        m_CachedKDTreeSettings.DrawSplitPlanes == KDTreeSettings.DrawSplitPlanes &&
        m_CachedKDTreeSettings.MaxDepth == KDTreeSettings.MaxDepth &&
        std::abs(m_CachedKDTreeSettings.Alpha - KDTreeSettings.Alpha) <= 1e-4f &&
        Vec3NearlyEqual(m_CachedKDTreeSettings.LeafColor, KDTreeSettings.LeafColor) &&
        Vec3NearlyEqual(m_CachedKDTreeSettings.InternalColor, KDTreeSettings.InternalColor) &&
        Vec3NearlyEqual(m_CachedKDTreeSettings.SplitPlaneColor, KDTreeSettings.SplitPlaneColor) &&
        MatricesNearlyEqual(m_CachedKDTreeWorld, worldMatrix);

    if (cacheValid)
        return true;

    Graphics::KDTreeDebugDrawSettings settings = KDTreeSettings;
    settings.Enabled = true;
    const bool ok = UpdateRetainedLineOverlay(engine, m_KDTreeOverlay, [&](Graphics::DebugDraw& dd)
    {
        Graphics::DrawKDTree(dd, m_SelectedColliderKDTree, settings, worldMatrix);
    });

    if (!ok)
        return false;

    m_KDTreeOverlaySourceEntity = selected;
    m_CachedKDTreeSettings = settings;
    m_CachedKDTreeWorld = worldMatrix;
    return true;
}

// =========================================================================
// BVH overlay
// =========================================================================

bool SpatialDebugController::EnsureRetainedBVHOverlay(Runtime::Engine& engine, entt::entity selected,
                                                       const Graphics::GeometryCollisionData& collision,
                                                       const ECS::Components::Transform::Component& xf)
{
    const glm::mat4 worldMatrix = ECS::Components::Transform::GetMatrix(xf);
    const bool cacheValid =
        (m_BVHOverlaySourceEntity == selected) &&
        m_BVHOverlay.Geometry.IsValid() &&
        m_CachedBVHSettings.Overlay == BVHSettings.Overlay &&
        m_CachedBVHSettings.LeafOnly == BVHSettings.LeafOnly &&
        m_CachedBVHSettings.DrawInternal == BVHSettings.DrawInternal &&
        m_CachedBVHSettings.MaxDepth == BVHSettings.MaxDepth &&
        m_CachedBVHSettings.LeafTriangleCount == BVHSettings.LeafTriangleCount &&
        std::abs(m_CachedBVHSettings.Alpha - BVHSettings.Alpha) <= 1e-4f &&
        Vec3NearlyEqual(m_CachedBVHSettings.LeafColor, BVHSettings.LeafColor) &&
        Vec3NearlyEqual(m_CachedBVHSettings.InternalColor, BVHSettings.InternalColor) &&
        MatricesNearlyEqual(m_CachedBVHWorld, worldMatrix) &&
        m_CachedBVHPositionCount == collision.Positions.size() &&
        m_CachedBVHIndexCount == collision.Indices.size();

    if (cacheValid)
        return true;

    Graphics::BVHDebugDrawSettings settings = BVHSettings;
    settings.Enabled = true;
    const bool ok = UpdateRetainedLineOverlay(engine, m_BVHOverlay, [&](Graphics::DebugDraw& dd)
    {
        Graphics::DrawBVH(dd, collision.Positions, collision.Indices, settings, worldMatrix);
    });

    if (!ok)
        return false;

    m_BVHOverlaySourceEntity = selected;
    m_CachedBVHSettings = settings;
    m_CachedBVHWorld = worldMatrix;
    m_CachedBVHPositionCount = collision.Positions.size();
    m_CachedBVHIndexCount = collision.Indices.size();
    return true;
}

// =========================================================================
// Convex Hull overlay
// =========================================================================

bool SpatialDebugController::EnsureRetainedConvexHullOverlay(Runtime::Engine& engine, entt::entity selected,
                                                              const Graphics::GeometryCollisionData& collision,
                                                              const ECS::Components::Transform::Component& xf)
{
    if (!EnsureSelectedColliderConvexHull(selected, collision))
    {
        ReleaseRetainedLineOverlay(engine, m_ConvexHullOverlay);
        m_ConvexHullOverlaySourceEntity = entt::null;
        return false;
    }

    const glm::mat4 worldMatrix = ECS::Components::Transform::GetMatrix(xf);
    const bool cacheValid =
        (m_ConvexHullOverlaySourceEntity == selected) &&
        m_ConvexHullOverlay.Geometry.IsValid() &&
        m_CachedConvexHullSettings.Overlay == ConvexHullSettings.Overlay &&
        std::abs(m_CachedConvexHullSettings.Alpha - ConvexHullSettings.Alpha) <= 1e-4f &&
        Vec3NearlyEqual(m_CachedConvexHullSettings.Color, ConvexHullSettings.Color) &&
        MatricesNearlyEqual(m_CachedConvexHullWorld, worldMatrix);

    if (cacheValid)
        return true;

    Graphics::ConvexHullDebugDrawSettings settings = ConvexHullSettings;
    settings.Enabled = true;
    const bool ok = UpdateRetainedLineOverlay(engine, m_ConvexHullOverlay, [&](Graphics::DebugDraw& dd)
    {
        Graphics::DrawConvexHull(dd, m_SelectedColliderHullMesh, settings, worldMatrix);
    });

    if (!ok)
        return false;

    m_ConvexHullOverlaySourceEntity = selected;
    m_CachedConvexHullSettings = settings;
    m_CachedConvexHullWorld = worldMatrix;
    return true;
}

// =========================================================================
// Contact manifold (transient DebugDraw)
// =========================================================================

void SpatialDebugController::EmitContactManifolds(Runtime::Engine& engine, entt::entity selected,
                                                   const ECS::MeshCollider::Component& selectedCollider)
{
    auto& dd = engine.GetRenderOrchestrator().GetDebugDraw();
    auto& reg = engine.GetScene().GetRegistry();
    auto colliders = reg.view<ECS::MeshCollider::Component>();

    const uint32_t pointAColor = Graphics::DebugDraw::PackColorF(1.0f, 0.85f, 0.2f, 1.0f);
    const uint32_t pointBColor = Graphics::DebugDraw::PackColorF(1.0f, 0.2f, 0.2f, 1.0f);
    const uint32_t normalColor = Graphics::DebugDraw::PackColorF(0.2f, 0.85f, 1.0f, 1.0f);

    for (auto [otherEntity, otherCollider] : colliders.each())
    {
        if (otherEntity == selected || !otherCollider.CollisionRef)
            continue;

        auto manifold = Geometry::ComputeContact(selectedCollider.WorldOBB, otherCollider.WorldOBB);
        if (!manifold)
            continue;

        const glm::vec3 mid = (manifold->ContactPointA + manifold->ContactPointB) * 0.5f;
        const glm::vec3 normalEnd = mid + manifold->Normal * (ContactNormalScale + manifold->PenetrationDepth);

        if (ContactOverlay)
        {
            dd.OverlaySphere(manifold->ContactPointA, ContactPointRadius, pointAColor, 12);
            dd.OverlaySphere(manifold->ContactPointB, ContactPointRadius, pointBColor, 12);
            dd.OverlayLine(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
            dd.OverlayLine(mid, normalEnd, normalColor);
        }
        else
        {
            dd.Sphere(manifold->ContactPointA, ContactPointRadius, pointAColor, 12);
            dd.Sphere(manifold->ContactPointB, ContactPointRadius, pointBColor, 12);
            dd.Line(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
            dd.Arrow(mid, normalEnd, glm::max(0.02f, ContactPointRadius), normalColor);
        }
    }
}

} // namespace Runtime::EditorUI
