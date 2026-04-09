module;

#include <memory>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Graphics.OverlayEntityFactory;

import Graphics.Components;
import Graphics.Geometry;
import ECS;

import Core.Logging;

import Geometry.MeshUtils;
import Geometry.Graph;
import Geometry.Handle;
import Geometry.HalfedgeMesh;
import Geometry.PointCloudUtils;
import RHI.Buffer;
import RHI.Device;
import RHI.Transfer;

namespace
{
    // Monotonic pick ID counter for overlay entities.  Uses the upper half of
    // the uint32_t range to avoid collision with SceneManager (starts at 1),
    // SceneSerializer, and SelectionModule counters.
    uint32_t s_OverlayPickId = 0x80000000u;

    [[nodiscard]] bool UploadMeshOverlayGeometry(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager,
        RHI::BufferManager& bufferManager,
        Graphics::GeometryPool& geometryStorage,
        ECS::Surface::Component& surface,
        const Geometry::Halfedge::Mesh& mesh)
    {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        std::vector<glm::vec4> aux;
        Geometry::MeshUtils::ExtractIndexedTriangles(mesh, positions, indices, &aux);

        if (positions.empty() || indices.empty())
            return false;

        std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
        Geometry::MeshUtils::CalculateNormals(positions, indices, normals);

        Graphics::GeometryUploadRequest upload{};
        upload.Positions = positions;
        upload.Normals = normals;
        upload.Aux = aux;
        upload.Indices = indices;
        upload.Topology = Graphics::PrimitiveTopology::Triangles;
        upload.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            std::move(device), transferManager, bufferManager, upload, &geometryStorage);
        (void)token;

        if (!gpuData || !gpuData->GetVertexBuffer() || !gpuData->GetIndexBuffer())
            return false;

        surface.Geometry = geometryStorage.Add(std::move(*gpuData));
        return surface.Geometry.IsValid();
    }
}

namespace Graphics::OverlayEntityFactory
{
    entt::entity CreateMeshOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::Halfedge::Mesh> mesh,
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager,
        RHI::BufferManager& bufferManager,
        Graphics::GeometryPool& geometryStorage,
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

        registry.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(child);
        registry.emplace_or_replace<ECS::DirtyTag::FaceAttributes>(child);

        // Surface::Component is required for the mesh to enter the rendering
        // pipeline via MeshRendererLifecycle.  Upload now so the geometry
        // handle is valid immediately for rendering and GPU picking.
        auto& surface = registry.emplace<ECS::Surface::Component>(child);
        if (md.MeshRef)
        {
            if (!UploadMeshOverlayGeometry(std::move(device), transferManager, bufferManager, geometryStorage, surface, *md.MeshRef))
            {
                Core::Log::Warn("CreateMeshOverlay: mesh overlay '{}' has no renderable triangle geometry; Surface::Geometry remains invalid.",
                                name);
            }
        }

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
