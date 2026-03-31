module;

#include <cstdint>
#include <memory>
#include <string>
#include <entt/entity/registry.hpp>

module Graphics.OverlayEntityFactory;

import Graphics.Components;
import ECS;

import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloudUtils;

namespace
{
    // Monotonic pick ID counter for overlay entities.  Uses the upper half of
    // the uint32_t range to avoid collision with SceneManager (starts at 1),
    // SceneSerializer, and SelectionModule counters.
    static uint32_t s_OverlayPickId = 0x80000000u;
}

namespace Graphics::OverlayEntityFactory
{
    entt::entity CreateMeshOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::Halfedge::Mesh> mesh,
        const std::string& name)
    {
        entt::entity child = registry.create();

        registry.emplace<ECS::Components::NameTag::Component>(child, ECS::Components::NameTag::Component{name});
        registry.emplace<ECS::Components::Transform::Component>(child);
        registry.emplace<ECS::Components::Transform::WorldMatrix>(child);
        registry.emplace<ECS::Components::Transform::IsDirtyTag>(child);

        ECS::Components::Hierarchy::Attach(registry, child, parent);

        registry.emplace<ECS::DataAuthority::MeshTag>(child);

        auto& md = registry.emplace<ECS::Mesh::Data>(child);
        md.MeshRef = std::move(mesh);
        md.AttributesDirty = true;

        // Surface::Component is required for the mesh to enter the rendering
        // pipeline via MeshRendererLifecycle.  The Geometry handle starts
        // invalid; it will be populated once the mesh is uploaded to the GPU.
        registry.emplace<ECS::Surface::Component>(child);

        registry.emplace<ECS::Components::Selection::SelectableTag>(child);
        registry.emplace<ECS::Components::Selection::PickID>(child, s_OverlayPickId++);

        return child;
    }

    entt::entity CreatePointCloudOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::PointCloud::Cloud> cloud,
        const std::string& name)
    {
        entt::entity child = registry.create();

        registry.emplace<ECS::Components::NameTag::Component>(child, ECS::Components::NameTag::Component{name});
        registry.emplace<ECS::Components::Transform::Component>(child);
        registry.emplace<ECS::Components::Transform::WorldMatrix>(child);
        registry.emplace<ECS::Components::Transform::IsDirtyTag>(child);

        ECS::Components::Hierarchy::Attach(registry, child, parent);

        registry.emplace<ECS::DataAuthority::PointCloudTag>(child);

        auto& pcd = registry.emplace<ECS::PointCloud::Data>(child);
        pcd.CloudRef = std::move(cloud);
        pcd.GpuDirty = true;

        registry.emplace<ECS::Components::Selection::SelectableTag>(child);
        registry.emplace<ECS::Components::Selection::PickID>(child, s_OverlayPickId++);

        return child;
    }

    entt::entity CreateGraphOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::Graph::Graph> graph,
        const std::string& name)
    {
        entt::entity child = registry.create();

        registry.emplace<ECS::Components::NameTag::Component>(child, ECS::Components::NameTag::Component{name});
        registry.emplace<ECS::Components::Transform::Component>(child);
        registry.emplace<ECS::Components::Transform::WorldMatrix>(child);
        registry.emplace<ECS::Components::Transform::IsDirtyTag>(child);

        ECS::Components::Hierarchy::Attach(registry, child, parent);

        registry.emplace<ECS::DataAuthority::GraphTag>(child);

        auto& gd = registry.emplace<ECS::Graph::Data>(child);
        gd.GraphRef = std::move(graph);
        gd.GpuDirty = true;

        registry.emplace<ECS::Components::Selection::SelectableTag>(child);
        registry.emplace<ECS::Components::Selection::PickID>(child, s_OverlayPickId++);

        return child;
    }

    void DestroyOverlay(entt::registry& registry, entt::entity overlayEntity)
    {
        if (overlayEntity == entt::null || !registry.valid(overlayEntity))
            return;

        ECS::Components::Hierarchy::Detach(registry, overlayEntity);
        registry.destroy(overlayEntity);
    }
}
