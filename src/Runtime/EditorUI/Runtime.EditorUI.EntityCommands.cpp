module;

#include <cassert>
#include <memory>
#include <optional>
#include <string>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

module Runtime.EditorUI;

import Core.Commands;

import ECS;

import Graphics.Components;
import Graphics.Components.DataAuthority;

import Runtime.Engine;
import Runtime.Selection;
import Runtime.SelectionModule;

// =========================================================================
// EntitySnapshot — captures restorable component state for undo/redo
// =========================================================================
//
// Design notes:
//   - GPU-managed state (GpuSlot, GpuGeometry handles, cached colors/radii)
//     is NOT captured. Instead, data components are restored with GpuDirty=true
//     so lifecycle systems re-upload as needed.
//   - Data components (Mesh::Data, PointCloud::Data, Graph::Data) use shared_ptr
//     for geometry ownership, so snapshots are shallow copies (cheap).
//   - Hierarchy parent linkage is captured; child entities are NOT recursively
//     snapshotted. Deleting a parent with children destroys the subtree. Undo
//     only restores the single entity and re-attaches to its former parent.
//     Full subtree undo is left for future work.

namespace
{
    struct EntitySnapshot
    {
        // --- Always present (from SceneBootstrap::EmplaceDefaults) ---
        std::string Name;
        ECS::Components::Transform::Component Transform{};
        entt::entity HierarchyParent = entt::null;

        // --- Optional editor components ---
        bool HasSelectable = false;
        std::optional<ECS::Components::Selection::PickID> PickId;
        std::optional<ECS::Components::AssetSourceRef::Component> AssetSource;

        // --- Optional data authority ---
        bool HasMeshTag = false;
        bool HasGraphTag = false;
        bool HasPointCloudTag = false;

        // --- Optional data components ---
        std::optional<ECS::Mesh::Data> MeshData;
        std::optional<ECS::PointCloud::Data> PointCloudData;
        std::optional<ECS::Graph::Data> GraphData;

        // --- Optional CPU-only components ---
        std::optional<ECS::MeshCollider::Component> ColliderComp;
    };

    EntitySnapshot CaptureEntity(entt::registry& reg, entt::entity e)
    {
        EntitySnapshot snap;

        // Name
        if (auto* nt = reg.try_get<ECS::Components::NameTag::Component>(e))
            snap.Name = nt->Name;

        // Transform
        if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(e))
            snap.Transform = *xf;

        // Hierarchy parent
        if (auto* h = reg.try_get<ECS::Components::Hierarchy::Component>(e))
            snap.HierarchyParent = h->Parent;

        // Selection components
        snap.HasSelectable = reg.all_of<ECS::Components::Selection::SelectableTag>(e);
        if (auto* pid = reg.try_get<ECS::Components::Selection::PickID>(e))
            snap.PickId = *pid;

        // Asset source
        if (auto* src = reg.try_get<ECS::Components::AssetSourceRef::Component>(e))
            snap.AssetSource = *src;

        // Data authority tags
        snap.HasMeshTag = reg.all_of<ECS::DataAuthority::MeshTag>(e);
        snap.HasGraphTag = reg.all_of<ECS::DataAuthority::GraphTag>(e);
        snap.HasPointCloudTag = reg.all_of<ECS::DataAuthority::PointCloudTag>(e);

        // Data components (shallow copy — shared_ptrs to geometry are cheap)
        if (auto* md = reg.try_get<ECS::Mesh::Data>(e))
            snap.MeshData = *md;
        if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(e))
            snap.PointCloudData = *pcd;
        if (auto* gd = reg.try_get<ECS::Graph::Data>(e))
            snap.GraphData = *gd;

        // CPU-only components
        if (auto* mc = reg.try_get<ECS::MeshCollider::Component>(e))
            snap.ColliderComp = *mc;

        return snap;
    }

    void RestoreEntity(entt::registry& reg, entt::entity e, const EntitySnapshot& snap)
    {
        // EmplaceDefaults is already called by Scene::CreateEntity, so
        // NameTag, Transform, WorldMatrix, IsDirtyTag, and Hierarchy are present.

        // Update name
        if (auto* nt = reg.try_get<ECS::Components::NameTag::Component>(e))
            nt->Name = snap.Name;

        // Restore transform
        reg.emplace_or_replace<ECS::Components::Transform::Component>(e, snap.Transform);
        reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

        // Re-attach to parent if it still exists
        if (snap.HierarchyParent != entt::null && reg.valid(snap.HierarchyParent))
            ECS::Components::Hierarchy::Attach(reg, e, snap.HierarchyParent);

        // Selection components
        if (snap.HasSelectable)
            reg.emplace_or_replace<ECS::Components::Selection::SelectableTag>(e);
        if (snap.PickId)
            reg.emplace_or_replace<ECS::Components::Selection::PickID>(e, *snap.PickId);

        // Asset source
        if (snap.AssetSource)
            reg.emplace_or_replace<ECS::Components::AssetSourceRef::Component>(e, *snap.AssetSource);

        // Data authority tags
        if (snap.HasMeshTag)
            reg.emplace_or_replace<ECS::DataAuthority::MeshTag>(e);
        if (snap.HasGraphTag)
            reg.emplace_or_replace<ECS::DataAuthority::GraphTag>(e);
        if (snap.HasPointCloudTag)
            reg.emplace_or_replace<ECS::DataAuthority::PointCloudTag>(e);

        // Restore data components with GPU state reset so lifecycle systems
        // pick them up for re-upload on the next frame.
        if (snap.MeshData)
        {
            auto& md = reg.emplace_or_replace<ECS::Mesh::Data>(e, *snap.MeshData);
            md.AttributesDirty = true;
        }
        if (snap.PointCloudData)
        {
            auto& pcd = reg.emplace_or_replace<ECS::PointCloud::Data>(e, *snap.PointCloudData);
            // Reset GPU state — PointCloudLifecycleSystem re-uploads on GpuDirty
            pcd.GpuGeometry = {};
            pcd.GpuSlot = ECS::kInvalidGpuSlot;
            pcd.CachedColors.clear();
            pcd.CachedRadii.clear();
            pcd.GpuDirty = true;
            pcd.GpuPointCount = 0;
        }
        if (snap.GraphData)
        {
            auto& gd = reg.emplace_or_replace<ECS::Graph::Data>(e, *snap.GraphData);
            // Reset GPU state — GraphLifecycleSystem re-uploads on GpuDirty
            gd.GpuGeometry = {};
            gd.GpuEdgeGeometry = {};
            gd.GpuSlot = ECS::kInvalidGpuSlot;
            gd.CachedEdgePairs.clear();
            gd.CachedEdgeColors.clear();
            gd.CachedNodeColors.clear();
            gd.CachedNodeRadii.clear();
            gd.GpuDirty = true;
            gd.GpuVertexCount = 0;
            gd.GpuEdgeCount = 0;
        }

        // Collider data is CPU-only — safe to restore directly.
        if (snap.ColliderComp)
            reg.emplace_or_replace<ECS::MeshCollider::Component>(e, *snap.ColliderComp);

        // Note: Surface::Component, Line::Component, and Point::Component are
        // NOT restored. These carry GPU resource handles (geometry, slots) that
        // were freed on destroy. For PointCloud and Graph entities, their
        // lifecycle systems re-create Line/Point components from the restored
        // Data component's GpuDirty flag. For Mesh entities, the mesh geometry
        // must be re-uploaded via the asset pipeline (e.g. re-import from the
        // AssetSourceRef path). This is a known limitation matching the TODO.md
        // note that full geometry-operator undo may require deeper integration.
    }
} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================

namespace Runtime::EditorUI
{
    Core::EditorCommand MakeCreateEntityCommand(Runtime::Engine& engine,
                                                const std::string& name)
    {
        // Shared mutable handle between redo/undo lambdas.
        auto handle = std::make_shared<entt::entity>(entt::null);

        return Core::EditorCommand{
            .name = "Create " + name,
            .redo = [&engine, handle, name]()
            {
                auto& scene = engine.GetSceneManager().GetScene();
                *handle = scene.CreateEntity(name);

                // Make new entity selectable and give it a pick ID.
                auto& reg = scene.GetRegistry();
                reg.emplace_or_replace<ECS::Components::Selection::SelectableTag>(*handle);
                static uint32_t s_NextPickId = 50000u;
                reg.emplace_or_replace<ECS::Components::Selection::PickID>(*handle,
                    ECS::Components::Selection::PickID{s_NextPickId++});

                engine.GetSelection().SetSelectedEntity(scene, *handle);
            },
            .undo = [&engine, handle]()
            {
                auto& scene = engine.GetSceneManager().GetScene();
                auto& reg = scene.GetRegistry();
                if (*handle != entt::null && reg.valid(*handle))
                {
                    if (reg.all_of<ECS::Components::Hierarchy::Component>(*handle))
                        ECS::Components::Hierarchy::Detach(reg, *handle);
                    reg.destroy(*handle);
                }
                engine.GetSelection().ClearSelection(scene);
                *handle = entt::null;
            },
        };
    }

    Core::EditorCommand MakeDeleteEntityCommand(Runtime::Engine& engine,
                                                entt::entity target)
    {
        auto& scene = engine.GetSceneManager().GetScene();
        auto& reg = scene.GetRegistry();
        assert(reg.valid(target) && "MakeDeleteEntityCommand: target entity must be valid");

        // Capture entity state before deletion.
        auto snapshot = std::make_shared<EntitySnapshot>(CaptureEntity(reg, target));
        auto handle = std::make_shared<entt::entity>(target);

        std::string entityName = snapshot->Name.empty() ? "Entity" : snapshot->Name;

        return Core::EditorCommand{
            .name = "Delete " + entityName,
            .redo = [&engine, handle, snapshot]()
            {
                auto& sc = engine.GetSceneManager().GetScene();
                auto& r = sc.GetRegistry();
                if (*handle != entt::null && r.valid(*handle))
                {
                    // Re-capture in case state changed since command creation
                    // (only on subsequent redo after first undo).
                    *snapshot = CaptureEntity(r, *handle);

                    if (r.all_of<ECS::Components::Hierarchy::Component>(*handle))
                        ECS::Components::Hierarchy::Detach(r, *handle);
                    r.destroy(*handle);
                }
                engine.GetSelection().ClearSelection(sc);
            },
            .undo = [&engine, handle, snapshot]()
            {
                auto& sc = engine.GetSceneManager().GetScene();
                auto& r = sc.GetRegistry();

                // Create a fresh entity and restore snapshotted state.
                *handle = sc.CreateEntity(snapshot->Name);
                RestoreEntity(r, *handle, *snapshot);

                engine.GetSelection().SetSelectedEntity(sc, *handle);
            },
        };
    }
} // namespace Runtime::EditorUI
