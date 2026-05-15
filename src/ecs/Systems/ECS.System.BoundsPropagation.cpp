module;

#include <algorithm>
#include <cmath>

#include <entt/entity/registry.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.ECS.System.BoundsPropagation;

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.System.TransformHierarchy;
import Geometry.AABB;
import Geometry.OBB;
import Geometry.Sphere;

namespace Extrinsic::ECS::Systems::BoundsPropagation
{
    namespace Components = ::Extrinsic::ECS::Components;

    namespace
    {
        bool IsFinite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool IsFinite(const glm::quat& q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
        }

        Components::Culling::World::Bounds ComputeWorldBounds(
            const Components::Culling::Local::Bounds& local,
            const glm::mat4& worldMatrix)
        {
            const glm::vec3 col0{worldMatrix[0][0], worldMatrix[0][1], worldMatrix[0][2]};
            const glm::vec3 col1{worldMatrix[1][0], worldMatrix[1][1], worldMatrix[1][2]};
            const glm::vec3 col2{worldMatrix[2][0], worldMatrix[2][1], worldMatrix[2][2]};

            const float lenX = glm::length(col0);
            const float lenY = glm::length(col1);
            const float lenZ = glm::length(col2);

            const glm::vec3 localCenter = local.LocalBoundingAABB.IsValid()
                ? local.LocalBoundingAABB.GetCenter()
                : glm::vec3{0.0f};
            const glm::vec3 localExtents = local.LocalBoundingAABB.IsValid()
                ? local.LocalBoundingAABB.GetExtents()
                : glm::vec3{0.0f};

            const glm::vec3 worldCenter = glm::vec3{worldMatrix * glm::vec4{localCenter, 1.0f}};

            constexpr float kAxisEpsilon = 1.0e-8f;
            glm::mat3 rotationBasis{1.0f};
            if (lenX > kAxisEpsilon && lenY > kAxisEpsilon && lenZ > kAxisEpsilon)
            {
                rotationBasis = glm::mat3{col0 / lenX, col1 / lenY, col2 / lenZ};
            }

            Components::Culling::World::Bounds world{};
            world.WorldBoundingOBB.Center = worldCenter;
            world.WorldBoundingOBB.Extents = glm::vec3{
                lenX * localExtents.x,
                lenY * localExtents.y,
                lenZ * localExtents.z,
            };
            world.WorldBoundingOBB.Rotation = glm::normalize(glm::quat_cast(rotationBasis));

            const float maxScale = std::max({lenX, lenY, lenZ});
            const glm::vec3 worldSphereCenter = glm::vec3{worldMatrix * glm::vec4{local.LocalBoundingSphere.Center, 1.0f}};
            world.WorldBoundingSphere.Center = worldSphereCenter;
            world.WorldBoundingSphere.Radius = local.LocalBoundingSphere.Radius * maxScale;

            return world;
        }

        bool IsFinite(const Components::Culling::World::Bounds& world)
        {
            return IsFinite(world.WorldBoundingOBB.Center)
                && IsFinite(world.WorldBoundingOBB.Extents)
                && IsFinite(world.WorldBoundingOBB.Rotation)
                && IsFinite(world.WorldBoundingSphere.Center)
                && std::isfinite(world.WorldBoundingSphere.Radius);
        }
    }

    void OnUpdate(entt::registry& registry)
    {
        Stats discard{};
        OnUpdate(registry, discard);
    }

    void OnUpdate(entt::registry& registry, Stats& stats)
    {
        const auto view = registry.view<Components::Transform::WorldUpdatedTag>();
        for (const auto entity : view)
        {
            const auto* local = registry.try_get<Components::Culling::Local::Bounds>(entity);
            if (local == nullptr)
            {
                ++stats.SkippedMissingLocalBounds;
                continue;
            }

            const auto* worldMatrix = registry.try_get<Components::Transform::WorldMatrix>(entity);
            if (worldMatrix == nullptr)
            {
                ++stats.SkippedMissingWorldMatrix;
                continue;
            }

            const Components::Culling::World::Bounds computed = ComputeWorldBounds(*local, worldMatrix->Matrix);
            if (!IsFinite(computed))
            {
                ++stats.NonFiniteResults;
                continue;
            }

            registry.emplace_or_replace<Components::Culling::World::Bounds>(entity, computed);
            ++stats.Recomputed;
        }
    }

    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry)
    {
        graph.AddPass(PassName,
            [](Extrinsic::Core::FrameGraphBuilder& builder)
            {
                builder.WaitFor(Extrinsic::Core::Hash::StringID{TransformHierarchy::PassName});
                builder.Read<Components::Culling::Local::Bounds>();
                builder.Read<Components::Transform::WorldMatrix>();
                builder.Read<Components::Transform::WorldUpdatedTag>();
                builder.Write<Components::Culling::World::Bounds>();
                builder.Signal(Extrinsic::Core::Hash::StringID{PassName});
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}
