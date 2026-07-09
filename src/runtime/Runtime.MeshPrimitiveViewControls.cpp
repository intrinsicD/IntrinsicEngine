module;

#include <cstdint>
#include <variant>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.MeshPrimitiveViewControls;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.StableEntityLookup;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] Graphics::Components::RenderPoints::RenderType ToRenderPointType(
            const MeshVertexViewRenderMode mode) noexcept
        {
            namespace G = Graphics::Components;
            switch (mode)
            {
            case MeshVertexViewRenderMode::FlatCircle:
                return G::RenderPoints::RenderType::Flat;
            case MeshVertexViewRenderMode::SurfaceAlignedCircle:
                return G::RenderPoints::RenderType::Surfel;
            case MeshVertexViewRenderMode::ImpostorSphere:
                return G::RenderPoints::RenderType::Sphere;
            }
            return G::RenderPoints::RenderType::Sphere;
        }

        [[nodiscard]] MeshVertexViewRenderMode ToMeshVertexViewRenderMode(
            const Graphics::Components::RenderPoints::RenderType type) noexcept
        {
            namespace G = Graphics::Components;
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat:
                return MeshVertexViewRenderMode::FlatCircle;
            case G::RenderPoints::RenderType::Surfel:
                return MeshVertexViewRenderMode::SurfaceAlignedCircle;
            case G::RenderPoints::RenderType::Sphere:
                return MeshVertexViewRenderMode::ImpostorSphere;
            }
            return MeshVertexViewRenderMode::ImpostorSphere;
        }

        [[nodiscard]] ECS::EntityHandle ResolveLiveEntity(
            const ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                StableEntityLookup::ToEntityHandle(stableEntityId);
            return entity != ECS::InvalidEntityHandle && scene.IsValid(entity)
                ? entity
                : ECS::InvalidEntityHandle;
        }
    }

    void ApplyMeshPrimitiveViewSettings(
        ECS::Scene::Registry& scene,
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        const ECS::EntityHandle entity = ResolveLiveEntity(scene, stableEntityId);
        if (entity == ECS::InvalidEntityHandle)
        {
            return;
        }

        namespace G = Graphics::Components;
        entt::registry& raw = scene.Raw();
        if (settings.EnableEdgeView)
        {
            raw.emplace_or_replace<G::RenderEdges>(entity);
        }
        else if (raw.all_of<G::RenderEdges>(entity))
        {
            raw.remove<G::RenderEdges>(entity);
        }

        if (settings.EnableVertexView)
        {
            G::RenderPoints points =
                raw.all_of<G::RenderPoints>(entity)
                    ? raw.get<G::RenderPoints>(entity)
                    : G::RenderPoints{};
            points.Type = ToRenderPointType(settings.VertexRenderMode);
            points.SizeSource = settings.VertexPointRadiusPx;
            raw.emplace_or_replace<G::RenderPoints>(entity, points);
        }
        else if (raw.all_of<G::RenderPoints>(entity))
        {
            raw.remove<G::RenderPoints>(entity);
        }
    }

    void ClearMeshPrimitiveViewSettings(
        ECS::Scene::Registry& scene,
        const std::uint32_t stableEntityId) noexcept
    {
        const ECS::EntityHandle entity = ResolveLiveEntity(scene, stableEntityId);
        if (entity == ECS::InvalidEntityHandle)
        {
            return;
        }

        namespace G = Graphics::Components;
        scene.Raw().remove<G::RenderEdges, G::RenderPoints>(entity);
    }

    MeshPrimitiveViewSettings ReadMeshPrimitiveViewSettings(
        const ECS::Scene::Registry& scene,
        const std::uint32_t stableEntityId) noexcept
    {
        MeshPrimitiveViewSettings settings{};
        const ECS::EntityHandle entity = ResolveLiveEntity(scene, stableEntityId);
        if (entity == ECS::InvalidEntityHandle)
        {
            return settings;
        }

        namespace G = Graphics::Components;
        const entt::registry& raw = scene.Raw();
        settings.EnableEdgeView = raw.all_of<G::RenderEdges>(entity);
        if (const auto* points = raw.try_get<G::RenderPoints>(entity))
        {
            settings.EnableVertexView = true;
            settings.VertexRenderMode = ToMeshVertexViewRenderMode(points->Type);
            if (const auto* uniform = std::get_if<float>(&points->SizeSource))
            {
                settings.VertexPointRadiusPx = *uniform;
            }
        }
        return settings;
    }
}
