module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <latch>
#include <entt/entity/registry.hpp>
#include <glm/gtc/matrix_inverse.hpp>

module Runtime.RenderExtraction;

import Graphics.Components;
import Geometry.HalfedgeMesh;
import Geometry.MeshUtils;
import ECS;
import Core.Tasks;

namespace Runtime
{
    namespace
    {
        [[nodiscard]] double SanitizeAlpha(double alpha)
        {
            if (!std::isfinite(alpha))
                return 0.0;

            return std::clamp(alpha, 0.0, 1.0);
        }

        [[nodiscard]] glm::mat4 ResolveWorldMatrix(const entt::registry& registry,
                                                   entt::entity entity,
                                                   const ECS::Components::Transform::Component& transform)
        {
            if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                return world->Matrix;
            return ECS::Components::Transform::GetMatrix(transform);
        }

        [[nodiscard]] uint32_t ResolvePickId(const entt::registry& registry, entt::entity entity)
        {
            if (const auto* pickId = registry.try_get<ECS::Components::Selection::PickID>(entity))
                return pickId->Value;
            return 0u;
        }

        [[nodiscard]] const Geometry::Halfedge::Mesh* ResolvePickingMesh(
            const entt::registry& registry, entt::entity entity)
        {
            if (const auto* meshData = registry.try_get<ECS::Mesh::Data>(entity))
            {
                if (meshData->MeshRef)
                    return meshData->MeshRef.get();
            }

            if (const auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity))
            {
                if (collider->CollisionRef && collider->CollisionRef->SourceMesh)
                    return collider->CollisionRef->SourceMesh.get();
            }

            return nullptr;
        }

        [[nodiscard]] glm::mat4 ResolveDrawWorldMatrix(const entt::registry& registry, entt::entity entity)
        {
            if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                return world->Matrix;
            return glm::mat4(1.0f);
        }

        void BuildTriangleFaceIds(const Geometry::Halfedge::Mesh& mesh, std::vector<uint32_t>& triangleFaceIds)
        {
            std::vector<glm::vec3> positions;
            std::vector<uint32_t> indices;
            Geometry::MeshUtils::ExtractIndexedTriangles(mesh, positions, indices, nullptr, &triangleFaceIds);
        }

        [[nodiscard]] std::vector<Graphics::PickingSurfacePacket> ExtractSurfacePickingPackets(
            const WorldSnapshot& world)
        {
            std::vector<Graphics::PickingSurfacePacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Components::Transform::Component, ECS::Surface::Component>();
            packets.reserve(view.size_hint());

            for (auto [entity, transform, surface] : view.each())
            {
                if (!surface.Geometry.IsValid())
                    continue;

                Graphics::PickingSurfacePacket packet{
                    .Geometry = surface.Geometry,
                    .WorldMatrix = ResolveWorldMatrix(registry, entity, transform),
                    .EntityId = ResolvePickId(registry, entity),
                };

                if (const Geometry::Halfedge::Mesh* mesh = ResolvePickingMesh(registry, entity))
                    BuildTriangleFaceIds(*mesh, packet.TriangleFaceIds);

                packets.push_back(std::move(packet));
            }

            return packets;
        }

        [[nodiscard]] std::vector<Graphics::PickingLinePacket> ExtractLinePickingPackets(const WorldSnapshot& world)
        {
            std::vector<Graphics::PickingLinePacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Components::Transform::Component, ECS::Line::Component>();
            packets.reserve(view.size_hint());

            for (auto [entity, transform, line] : view.each())
            {
                const bool isMeshEntity =
                    registry.all_of<ECS::MeshCollider::Component>(entity) || registry.all_of<ECS::Mesh::Data>(entity);
                const bool isGraphEntity = registry.all_of<ECS::Graph::Data>(entity);
                if (isMeshEntity || !isGraphEntity)
                    continue;
                if (!line.Geometry.IsValid() || !line.EdgeView.IsValid() || line.EdgeCount == 0u)
                    continue;

                packets.push_back(Graphics::PickingLinePacket{
                    .Geometry = line.Geometry,
                    .EdgeView = line.EdgeView,
                    .WorldMatrix = ResolveWorldMatrix(registry, entity, transform),
                    .EntityId = ResolvePickId(registry, entity),
                    .Width = line.Width,
                    .EdgeCount = line.EdgeCount,
                });
            }

            return packets;
        }

        [[nodiscard]] std::vector<Graphics::PickingPointPacket> ExtractPointPickingPackets(const WorldSnapshot& world)
        {
            std::vector<Graphics::PickingPointPacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Components::Transform::Component, ECS::Point::Component>();
            packets.reserve(view.size_hint());

            for (auto [entity, transform, point] : view.each())
            {
                const bool isMeshEntity =
                    registry.all_of<ECS::MeshCollider::Component>(entity) || registry.all_of<ECS::Mesh::Data>(entity);
                const bool isGraphEntity = registry.all_of<ECS::Graph::Data>(entity);
                const bool isPointCloudEntity = registry.all_of<ECS::PointCloud::Data>(entity);
                if (isMeshEntity || isGraphEntity || !isPointCloudEntity)
                    continue;
                if (!point.Geometry.IsValid())
                    continue;

                packets.push_back(Graphics::PickingPointPacket{
                    .Geometry = point.Geometry,
                    .WorldMatrix = ResolveWorldMatrix(registry, entity, transform),
                    .EntityId = ResolvePickId(registry, entity),
                    .Size = point.Size,
                });
            }

            return packets;
        }

        struct PickingPacketBundle
        {
            std::vector<Graphics::PickingSurfacePacket> Surface{};
            std::vector<Graphics::PickingLinePacket> Line{};
            std::vector<Graphics::PickingPointPacket> Point{};
        };

        [[nodiscard]] Graphics::SelectionOutlinePacket ExtractSelectionOutlinePacket(const WorldSnapshot& world)
        {
            Graphics::SelectionOutlinePacket packet{};
            if (!world.Registry)
                return packet;

            const entt::registry& registry = *world.Registry;
            auto canEmitPickId = [&](entt::entity entity)
            {
                return registry.valid(entity) &&
                    registry.all_of<ECS::Surface::Component, ECS::Components::Selection::PickID>(entity);
            };

            auto visitSubtree = [&](entt::entity root, auto&& visitor)
            {
                if (!registry.valid(root))
                    return;

                std::vector<entt::entity> stack;
                stack.push_back(root);
                while (!stack.empty())
                {
                    const entt::entity current = stack.back();
                    stack.pop_back();
                    visitor(current);

                    const auto* hierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(current);
                    if (!hierarchy)
                        continue;

                    for (entt::entity child = hierarchy->FirstChild; child != entt::null;)
                    {
                        entt::entity nextSibling = entt::null;
                        if (const auto* childHierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(child))
                            nextSibling = childHierarchy->NextSibling;
                        stack.push_back(child);
                        child = nextSibling;
                    }
                }
            };

            std::unordered_set<uint32_t> seenPickIds;
            registry.view<ECS::Components::Selection::SelectedTag>().each([&](entt::entity root)
            {
                visitSubtree(root, [&](entt::entity candidate)
                {
                    if (!canEmitPickId(candidate))
                        return;
                    const uint32_t pickId = registry.get<ECS::Components::Selection::PickID>(candidate).Value;
                    if (pickId == 0u)
                        return;
                    if (!seenPickIds.insert(pickId).second)
                        return;
                    packet.SelectedPickIds.push_back(pickId);
                });
            });

            auto hoveredRoots = registry.view<ECS::Components::Selection::HoveredTag, entt::entity>();
            for (const auto root : hoveredRoots)
            {
                visitSubtree(root, [&](entt::entity candidate)
                {
                    if (packet.HoveredPickId != 0u || !canEmitPickId(candidate))
                        return;
                    packet.HoveredPickId = registry.get<ECS::Components::Selection::PickID>(candidate).Value;
                });
                if (packet.HoveredPickId != 0u)
                    break;
            }

            return packet;
        }

        [[nodiscard]] bool ExtractSelectionWorkState(const WorldSnapshot& world)
        {
            if (!world.Registry)
                return false;

            const entt::registry& registry = *world.Registry;
            return !registry.view<ECS::Components::Selection::SelectedTag>().empty() ||
                !registry.view<ECS::Components::Selection::HoveredTag>().empty();
        }

        [[nodiscard]] std::vector<Graphics::SurfaceDrawPacket> ExtractSurfaceDrawPackets(const WorldSnapshot& world)
        {
            std::vector<Graphics::SurfaceDrawPacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Surface::Component>();
            packets.reserve(view.size());

            std::unordered_map<uint32_t, size_t> packetIndexByGeometry;
            packetIndexByGeometry.reserve(view.size());

            for (auto [entity, surface] : view.each())
            {
                (void)entity;
                if (!surface.Geometry.IsValid())
                    continue;

                const auto [it, inserted] =
                    packetIndexByGeometry.emplace(surface.Geometry.Index, packets.size());
                if (inserted)
                {
                    packets.push_back(Graphics::SurfaceDrawPacket{
                        .Geometry = surface.Geometry,
                    });
                }

                Graphics::SurfaceDrawPacket& packet = packets[it->second];

                if (packet.FaceColors.empty() && surface.ShowPerFaceColors && !surface.CachedFaceColors.empty())
                    packet.FaceColors = surface.CachedFaceColors;

                if (packet.VertexColors.empty() && surface.ShowPerVertexColors &&
                    !surface.CachedVertexColors.empty())
                {
                    packet.VertexColors = surface.CachedVertexColors;
                    packet.UseNearestVertexColors = surface.UseNearestVertexColors;
                    packet.VertexLabels = surface.CachedVertexLabels;
                    packet.Centroids.reserve(surface.CachedCentroids.size());
                    for (const auto& centroid : surface.CachedCentroids)
                    {
                        packet.Centroids.push_back(Graphics::SurfaceCentroidPacketEntry{
                            .Position = centroid.Position,
                            .PackedColor = centroid.PackedColor,
                        });
                    }
                }
            }

            return packets;
        }

        [[nodiscard]] std::vector<Graphics::LineDrawPacket> ExtractLineDrawPackets(const WorldSnapshot& world)
        {
            std::vector<Graphics::LineDrawPacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Line::Component>();
            packets.reserve(view.size());

            for (auto [entity, line] : view.each())
            {
                if (!line.Geometry.IsValid() || !line.EdgeView.IsValid() || line.EdgeCount == 0u)
                    continue;

                Graphics::LineDrawPacket packet{
                    .Geometry = line.Geometry,
                    .EdgeView = line.EdgeView,
                    .WorldMatrix = ResolveDrawWorldMatrix(registry, entity),
                    .Color = line.Color,
                    .Width = line.Width,
                    .Overlay = line.Overlay,
                    .EdgeCount = line.EdgeCount,
                    .EntityKey = static_cast<uint32_t>(entity),
                };

                if (line.HasPerEdgeColors && line.ShowPerEdgeColors)
                {
                    if (const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
                        graphData && graphData->CachedEdgeColors.size() == line.EdgeCount)
                    {
                        packet.EdgeColors = graphData->CachedEdgeColors;
                    }
                    else if (line.CachedEdgeColors.size() == line.EdgeCount)
                    {
                        packet.EdgeColors = line.CachedEdgeColors;
                    }
                }

                packets.push_back(std::move(packet));
            }

            return packets;
        }

        [[nodiscard]] std::vector<Graphics::PointDrawPacket> ExtractPointDrawPackets(const WorldSnapshot& world)
        {
            std::vector<Graphics::PointDrawPacket> packets;
            if (!world.Registry)
                return packets;

            const entt::registry& registry = *world.Registry;
            auto view = registry.view<ECS::Point::Component>();
            packets.reserve(view.size());

            for (auto [entity, point] : view.each())
            {
                if (!point.Geometry.IsValid())
                    continue;

                Graphics::PointDrawPacket packet{
                    .Geometry = point.Geometry,
                    .WorldMatrix = ResolveDrawWorldMatrix(registry, entity),
                    .Color = point.Color,
                    .Size = point.Size,
                    .SizeMultiplier = point.SizeMultiplier,
                    .Mode = point.Mode,
                    .HasPerPointNormals = point.HasPerPointNormals,
                    .EntityKey = static_cast<uint32_t>(entity),
                };

                if (point.HasPerPointColors)
                {
                    if (const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
                        graphData && !graphData->CachedNodeColors.empty())
                    {
                        packet.Colors = graphData->CachedNodeColors;
                    }
                    else if (const auto* pointCloud = registry.try_get<ECS::PointCloud::Data>(entity);
                        pointCloud && !pointCloud->CachedColors.empty())
                    {
                        packet.Colors = pointCloud->CachedColors;
                    }
                }

                if (point.HasPerPointRadii)
                {
                    if (const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
                        graphData && !graphData->CachedNodeRadii.empty())
                    {
                        packet.Radii = graphData->CachedNodeRadii;
                    }
                    else if (const auto* pointCloud = registry.try_get<ECS::PointCloud::Data>(entity);
                        pointCloud && !pointCloud->CachedRadii.empty())
                    {
                        packet.Radii = pointCloud->CachedRadii;
                    }
                }

                packets.push_back(std::move(packet));
            }

            return packets;
        }

        [[nodiscard]] bool IsInterestingHtexMeshEntity(const entt::registry& registry, entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.all_of<ECS::Mesh::Data>(entity))
                return false;
            const auto& data = registry.get<ECS::Mesh::Data>(entity);
            return static_cast<bool>(data.MeshRef) && data.MeshRef->EdgeCount() > 0;
        }

        [[nodiscard]] std::optional<entt::entity> FindHtexSourceMeshEntity(const entt::registry& registry)
        {
            std::optional<entt::entity> freshestKMeansMesh;
            uint64_t freshestRevision = 0;

            auto considerFreshest = [&](entt::entity entity)
            {
                if (!IsInterestingHtexMeshEntity(registry, entity))
                    return;

                const auto& data = registry.get<ECS::Mesh::Data>(entity);
                if (!data.MeshRef)
                    return;

                const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                if (!(labels || colors))
                    return;

                if (!freshestKMeansMesh || data.KMeansResultRevision >= freshestRevision)
                {
                    freshestKMeansMesh = entity;
                    freshestRevision = data.KMeansResultRevision;
                }
            };

            auto selected = registry.view<ECS::Components::Selection::SelectedTag, entt::entity>();

            for (auto entity : selected)
            {
                if (IsInterestingHtexMeshEntity(registry, entity))
                {
                    const auto& data = registry.get<ECS::Mesh::Data>(entity);
                    if (data.MeshRef)
                    {
                        const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                        const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                        if (labels || colors)
                            return entity;
                    }
                }
            }

            for (auto entity : selected)
            {
                if (IsInterestingHtexMeshEntity(registry, entity))
                    return entity;
            }

            auto meshes = registry.view<ECS::Mesh::Data>();

            for (auto entity : meshes)
                considerFreshest(entity);
            if (freshestKMeansMesh)
                return freshestKMeansMesh;

            for (auto entity : meshes)
            {
                if (IsInterestingHtexMeshEntity(registry, entity))
                {
                    const auto& data = registry.get<ECS::Mesh::Data>(entity);
                    if (data.MeshRef)
                    {
                        const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                        const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                        if (labels || colors)
                            return entity;
                    }
                }
            }

            for (auto entity : meshes)
            {
                if (IsInterestingHtexMeshEntity(registry, entity))
                    return entity;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<Graphics::HtexPatchPreviewPacket> ExtractHtexPatchPreviewPacket(
            const WorldSnapshot& world)
        {
            if (!world.Registry)
                return std::nullopt;

            const entt::registry& registry = *world.Registry;
            const auto source = FindHtexSourceMeshEntity(registry);
            if (!source)
                return std::nullopt;

            const auto* meshData = registry.try_get<ECS::Mesh::Data>(*source);
            if (!meshData || !meshData->MeshRef)
                return std::nullopt;

            return Graphics::HtexPatchPreviewPacket{
                .SourceEntityId = static_cast<uint32_t>(*source),
                .Mesh = Geometry::Halfedge::Mesh{*meshData->MeshRef},
                .KMeansResultRevision = meshData->KMeansResultRevision,
                .KMeansCentroids = meshData->KMeansCentroids,
            };
        }

        [[nodiscard]] PickingPacketBundle ExtractPickingPackets(const WorldSnapshot& world)
        {
            if (!Core::Tasks::Scheduler::IsInitialized())
            {
                return PickingPacketBundle{
                    .Surface = ExtractSurfacePickingPackets(world),
                    .Line = ExtractLinePickingPackets(world),
                    .Point = ExtractPointPickingPackets(world),
                };
            }

            PickingPacketBundle packets{};
            std::latch done(3);

            Core::Tasks::Scheduler::Dispatch([&]
            {
                packets.Surface = ExtractSurfacePickingPackets(world);
                done.count_down();
            });
            Core::Tasks::Scheduler::Dispatch([&]
            {
                packets.Line = ExtractLinePickingPackets(world);
                done.count_down();
            });
            Core::Tasks::Scheduler::Dispatch([&]
            {
                packets.Point = ExtractPointPickingPackets(world);
                done.count_down();
            });

            done.wait();
            return packets;
        }
    }

    uint32_t SanitizeFrameContextCount(uint32_t requestedCount)
    {
        return std::clamp(requestedCount, MinFrameContexts, MaxFrameContexts);
    }

    FrameContextRing::FrameContextRing(uint32_t framesInFlight)
        : m_FramesInFlight(SanitizeFrameContextCount(framesInFlight))
    {
        Configure(m_FramesInFlight);
    }

    void FrameContextRing::Configure(uint32_t framesInFlight)
    {
        m_FramesInFlight = SanitizeFrameContextCount(framesInFlight);
        m_Contexts.assign(m_FramesInFlight, FrameContext{});
        for (uint32_t slotIndex = 0; slotIndex < m_FramesInFlight; ++slotIndex)
        {
            m_Contexts[slotIndex].SlotIndex = slotIndex;
            m_Contexts[slotIndex].FramesInFlight = m_FramesInFlight;
        }
    }

    FrameContext& FrameContextRing::BeginFrame(uint64_t frameNumber, RenderViewport viewport) &
    {
        const uint32_t slotIndex = static_cast<uint32_t>(frameNumber % static_cast<uint64_t>(m_FramesInFlight));
        if (slotIndex >= m_Contexts.size())
            m_Contexts.resize(m_FramesInFlight);

        FrameContext& frame = m_Contexts[slotIndex];
        const uint64_t previousFrameNumber = frame.FrameNumber;
        const bool previouslySubmitted = frame.Submitted;
        const bool hasPreviousFrame = frame.Prepared || frame.Submitted || frame.Viewport.IsValid() ||
            frame.FrameNumber != 0u || frame.PreviousFrameNumber != InvalidFrameNumber;

        frame.ResetPreparedState();
        frame.FrameNumber = frameNumber;
        frame.PreviousFrameNumber = hasPreviousFrame ? previousFrameNumber : InvalidFrameNumber;
        frame.SlotIndex = slotIndex;
        frame.FramesInFlight = m_FramesInFlight;
        frame.Viewport = viewport;
        frame.Submitted = false;
        frame.ReusedSubmittedSlot = hasPreviousFrame && previouslySubmitted;
        return frame;
    }

    RenderFrameInput MakeRenderFrameInput(const Graphics::CameraComponent& camera,
                                          WorldSnapshot world,
                                          RenderViewport viewport,
                                          double alpha)
    {
        return RenderFrameInput{
            .Alpha = SanitizeAlpha(alpha),
            .View = MakeRenderViewPacket(camera, viewport),
            .World = world,
        };
    }

    RenderViewPacket MakeRenderViewPacket(const Graphics::CameraComponent& camera, RenderViewport viewport)
    {
        const glm::mat4 viewProjection = camera.ProjectionMatrix * camera.ViewMatrix;
        return RenderViewPacket{
            .Camera = camera,
            .ViewMatrix = camera.ViewMatrix,
            .ProjectionMatrix = camera.ProjectionMatrix,
            .ViewProjectionMatrix = viewProjection,
            .InverseViewMatrix = glm::inverse(camera.ViewMatrix),
            .InverseProjectionMatrix = glm::inverse(camera.ProjectionMatrix),
            .InverseViewProjectionMatrix = glm::inverse(viewProjection),
            .CameraPosition = camera.Position,
            .NearPlane = camera.Near,
            .CameraForward = camera.GetForward(),
            .FarPlane = camera.Far,
            .Viewport = viewport,
            .AspectRatio = camera.AspectRatio,
            .VerticalFieldOfViewDegrees = camera.Fov,
        };
    }

    RenderWorld ExtractRenderWorld(const RenderFrameInput& input)
    {
        PickingPacketBundle packets = ExtractPickingPackets(input.World);
        std::vector<Graphics::SurfaceDrawPacket> surfaceDraws = ExtractSurfaceDrawPackets(input.World);
        std::vector<Graphics::LineDrawPacket> lineDraws = ExtractLineDrawPackets(input.World);
        std::vector<Graphics::PointDrawPacket> pointDraws = ExtractPointDrawPackets(input.World);
        return RenderWorld{
            .Alpha = SanitizeAlpha(input.Alpha),
            .View = input.View,
            .World = input.World,
            .HasSelectionWork = ExtractSelectionWorkState(input.World),
            .SelectionOutline = ExtractSelectionOutlinePacket(input.World),
            .SurfacePicking = std::move(packets.Surface),
            .LinePicking = std::move(packets.Line),
            .PointPicking = std::move(packets.Point),
            .SurfaceDraws = std::move(surfaceDraws),
            .LineDraws = std::move(lineDraws),
            .PointDraws = std::move(pointDraws),
            .HtexPatchPreview = ExtractHtexPatchPreviewPacket(input.World),
        };
    }
}
