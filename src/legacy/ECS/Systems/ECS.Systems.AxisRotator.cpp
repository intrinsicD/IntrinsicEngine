module;
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <span>

module ECS:Systems.AxisRotator.Impl;
import :Systems.AxisRotator;
import :Components.Transform;
import :Components.AxisRotator;
import Core.FrameGraph;

namespace ECS::Systems::AxisRotator
{
    namespace
    {
        [[nodiscard]] glm::quat IntegrateRotation(const glm::quat& currentRotation,
                                                  const AxisAngularVelocity& angularVelocity,
                                                  float dt)
        {
            const glm::vec3 safeAxis = glm::normalize(
                glm::dot(angularVelocity.Axis, angularVelocity.Axis) > 1e-12f ? angularVelocity.Axis : glm::vec3{0.0f, 1.0f, 0.0f});
            const glm::quat deltaRotation =
                glm::angleAxis(glm::radians(angularVelocity.DegreesPerSecond * dt), safeAxis);
            return glm::normalize(deltaRotation * currentRotation);
        }
    }

    void RotateBatch(std::span<glm::quat> rotations,
                     std::span<const AxisAngularVelocity> angularVelocities,
                     float dt)
    {
        const std::size_t count = std::min(rotations.size(), angularVelocities.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            rotations[i] = IntegrateRotation(rotations[i], angularVelocities[i], dt);
        }
    }

    void OnUpdate(entt::registry& registry, float dt)
    {
        auto view = registry.view<Components::Transform::Component, Components::AxisRotator::Component>();
        constexpr std::size_t kBatchSize = 256;
        std::array<entt::entity, kBatchSize> entities{};
        std::array<glm::quat, kBatchSize> rotations{};
        std::array<AxisAngularVelocity, kBatchSize> angularVelocities{};
        std::size_t count = 0;

        const auto flushBatch = [&]()
        {
            RotateBatch(std::span<glm::quat>(rotations.data(), count),
                        std::span<const AxisAngularVelocity>(angularVelocities.data(), count),
                        dt);
            for (std::size_t i = 0; i < count; ++i)
            {
                auto& transform = registry.get<Components::Transform::Component>(entities[i]);
                transform.Rotation = rotations[i];
                registry.emplace_or_replace<Components::Transform::IsDirtyTag>(entities[i]);
            }
            count = 0;
        };

        for (auto [entity, transform, rotator] : view.each())
        {
            entities[count] = entity;
            rotations[count] = transform.Rotation;
            angularVelocities[count] = AxisAngularVelocity{
                .Axis = rotator.axis,
                .DegreesPerSecond = rotator.Speed,
            };
            ++count;
            if (count == kBatchSize) flushBatch();
        }
        if (count > 0) flushBatch();
    }

    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry, float dt)
    {
        graph.AddPass("AxisRotator",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<Components::AxisRotator::Component>();
                builder.Write<Components::Transform::Component>();
                builder.Write<Components::Transform::IsDirtyTag>();
            },
            [&registry, dt]()
            {
                OnUpdate(registry, dt);
            });
    }
}
