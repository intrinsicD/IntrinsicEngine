module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <latch>
#include <entt/entity/fwd.hpp>
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

        [[nodiscard]] const Geometry::Halfedge::Mesh* ResolvePickingMesh(const entt::registry& registry, entt::entity entity)
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

        void BuildTriangleFaceIds(const Geometry::Halfedge::Mesh& mesh, std::vector<uint32_t>& triangleFaceIds)
        {
            std::vector<glm::vec3> positions;
            std::vector<uint32_t> indices;
            Geometry::MeshUtils::ExtractIndexedTriangles(mesh, positions, indices, nullptr, &triangleFaceIds);
        }

        [[nodiscard]] std::vector<Graphics::PickingSurfacePacket> ExtractSurfacePickingPackets(const WorldSnapshot& world)
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

        [[nodiscard]] bool ExtractSelectionWorkState(const WorldSnapshot& world)
        {
            if (!world.Registry)
                return false;

            const entt::registry& registry = *world.Registry;
            return !registry.view<ECS::Components::Selection::SelectedTag>().empty() ||
                   !registry.view<ECS::Components::Selection::HoveredTag>().empty();
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
        for (uint32_t slotIndex = 0; slotIndex < MaxFrameContexts; ++slotIndex)
        {
            FrameContext& frame = m_Contexts[slotIndex];
            frame = FrameContext{};
            frame.SlotIndex = slotIndex;
            frame.FramesInFlight = m_FramesInFlight;
        }
    }

    FrameContext& FrameContextRing::BeginFrame(uint64_t frameNumber, RenderViewport viewport) &
    {
        const uint32_t slotIndex = static_cast<uint32_t>(frameNumber % static_cast<uint64_t>(m_FramesInFlight));
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
        return RenderWorld{
            .Alpha = SanitizeAlpha(input.Alpha),
            .View = input.View,
            .World = input.World,
            .HasSelectionWork = ExtractSelectionWorkState(input.World),
            .SurfacePicking = std::move(packets.Surface),
            .LinePicking = std::move(packets.Line),
            .PointPicking = std::move(packets.Point),
        };
    }
}
