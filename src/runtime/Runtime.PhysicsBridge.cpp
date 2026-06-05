module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.PhysicsBridge;

import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Physics.World;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Collider = Extrinsic::ECS::Components::Collider;
        namespace RigidBody = Extrinsic::ECS::Components::RigidBody;
        namespace Transform = Extrinsic::ECS::Components::Transform;
        namespace Physics = Extrinsic::Physics;
        namespace EcsComponents = Extrinsic::ECS::Components;

        [[nodiscard]] Physics::MotionType ConvertMotion(RigidBody::MotionType motion) noexcept
        {
            switch (motion)
            {
            case RigidBody::MotionType::Static:
                return Physics::MotionType::Static;
            case RigidBody::MotionType::Kinematic:
                return Physics::MotionType::Kinematic;
            case RigidBody::MotionType::Dynamic:
                return Physics::MotionType::Dynamic;
            }
            return Physics::MotionType::Static;
        }

        [[nodiscard]] Physics::Transform ConvertTransform(const Transform::Component& transform) noexcept
        {
            Physics::Transform result{};
            result.Position = transform.Position;
            result.Rotation = transform.Rotation;
            result.Scale = transform.Scale;
            return result;
        }

        [[nodiscard]] Physics::Transform ConvertLocalPose(const Collider::LocalPose& pose) noexcept
        {
            Physics::Transform result{};
            result.Position = pose.Position;
            result.Rotation = pose.Rotation;
            return result;
        }

        [[nodiscard]] Physics::ShapeDescriptor ConvertShape(const Collider::ShapeDescriptor& shape)
        {
            Physics::ShapeDescriptor result{};
            result.Enabled = shape.Enabled;
            result.IsTrigger = shape.IsTrigger;
            result.Local = ConvertLocalPose(shape.Local);

            if (const auto* sphere = std::get_if<Collider::SphereShape>(&shape.Shape))
            {
                result.Kind = Physics::ShapeKind::Sphere;
                result.Radius = sphere->Radius;
                return result;
            }
            if (const auto* capsule = std::get_if<Collider::CapsuleShape>(&shape.Shape))
            {
                result.Kind = Physics::ShapeKind::Capsule;
                result.Radius = capsule->Radius;
                result.CapsuleHalfHeight = capsule->HalfHeight;
                return result;
            }

            const auto& box = std::get<Collider::BoxShape>(shape.Shape);
            result.Kind = Physics::ShapeKind::Box;
            result.HalfExtents = box.HalfExtents;
            return result;
        }

        [[nodiscard]] bool IsValidConfig(const PhysicsBridgeFixedStepConfig& config) noexcept
        {
            return std::isfinite(config.FixedDeltaSeconds) && config.FixedDeltaSeconds > 0.0f &&
                   std::isfinite(config.MaxAccumulatedSeconds) && config.MaxAccumulatedSeconds >= 0.0f &&
                   Physics::IsFinite(config.Gravity);
        }
    }

    std::optional<PhysicsBridge::BodyHandle> PhysicsBridge::ResolveBody(StableId id) const
    {
        const auto it = m_Bindings.find(id);
        if (it == m_Bindings.end())
            return std::nullopt;
        return it->second.Handle;
    }

    void PhysicsBridge::RemoveBinding(StableId id, Binding& binding)
    {
        if (binding.Handle.IsValid())
        {
            if (!m_World.DestroyBody(binding.Handle))
                ++m_Diagnostics.StaleHandles;
        }
        m_Bindings.erase(id);
        ++m_Diagnostics.BodiesRemoved;
        m_Diagnostics.SidecarCount = static_cast<std::uint32_t>(m_Bindings.size());
    }

    void PhysicsBridge::RemoveUntouchedBindings()
    {
        for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
        {
            if (it->second.LastSeenGeneration == m_SyncGeneration)
            {
                ++it;
                continue;
            }

            Binding binding = it->second;
            const StableId id = it->first;
            ++it;
            RemoveBinding(id, binding);
        }
    }

    const PhysicsBridgeDiagnostics& PhysicsBridge::SyncAuthoring(Registry& registry)
    {
        ++m_Diagnostics.SyncPasses;
        m_Diagnostics.LastSyncOrder = NextOrder();
        ++m_SyncGeneration;

        auto& raw = registry.Raw();
        std::vector<Extrinsic::ECS::EntityHandle> entities;
        std::unordered_set<Extrinsic::ECS::EntityHandle> unique;

        for (const auto entity : raw.view<Collider::Component>())
        {
            if (unique.insert(entity).second)
                entities.push_back(entity);
        }
        for (const auto entity : raw.view<RigidBody::Component>())
        {
            if (unique.insert(entity).second)
                entities.push_back(entity);
        }

        std::sort(entities.begin(), entities.end(), [](auto lhs, auto rhs)
        {
            return static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs);
        });

        for (const auto entity : entities)
        {
            const auto* stableId = raw.try_get<EcsComponents::StableId>(entity);
            if (stableId == nullptr || !EcsComponents::IsValid(*stableId))
            {
                ++m_Diagnostics.MissingStableIds;
                continue;
            }

            const auto* collider = raw.try_get<Collider::Component>(entity);
            const auto* rigidBody = raw.try_get<RigidBody::Component>(entity);
            const RigidBody::AuthoringCombination authoring =
                RigidBody::ClassifyAuthoringCombination(collider != nullptr, rigidBody);

            if (authoring == RigidBody::AuthoringCombination::NoPhysicsAuthoring)
                continue;
            if (authoring == RigidBody::AuthoringCombination::MissingColliderForContactingBody)
            {
                ++m_Diagnostics.MissingAuthoringComponents;
                if (auto it = m_Bindings.find(*stableId); it != m_Bindings.end())
                    RemoveBinding(*stableId, it->second);
                continue;
            }

            if (rigidBody != nullptr && (!rigidBody->Enabled || RigidBody::Validate(*rigidBody) != RigidBody::ValidationStatus::Valid))
            {
                ++m_Diagnostics.InvalidDescriptors;
                if (auto it = m_Bindings.find(*stableId); it != m_Bindings.end())
                    RemoveBinding(*stableId, it->second);
                continue;
            }
            if (collider != nullptr && (!collider->Enabled || Collider::Validate(*collider) != Collider::ValidationStatus::Valid))
            {
                ++m_Diagnostics.InvalidDescriptors;
                if (auto it = m_Bindings.find(*stableId); it != m_Bindings.end())
                    RemoveBinding(*stableId, it->second);
                continue;
            }

            Physics::BodyDescriptor descriptor{};
            if (const auto* transform = raw.try_get<Transform::Component>(entity))
            {
                descriptor.Pose = ConvertTransform(*transform);
            }
            else
            {
                ++m_Diagnostics.MissingTransforms;
            }

            if (rigidBody != nullptr)
            {
                descriptor.Motion = ConvertMotion(rigidBody->Motion);
                descriptor.LinearVelocity = rigidBody->LinearVelocity;
                descriptor.AngularVelocity = rigidBody->AngularVelocity;
                descriptor.Mass = rigidBody->Mass.Mass;
                descriptor.LinearDamping = rigidBody->LinearDamping;
                descriptor.AngularDamping = rigidBody->AngularDamping;
                descriptor.GravityScale = rigidBody->GravityScale;
                descriptor.ParticipatesInContacts = rigidBody->ParticipatesInContacts;
            }
            else
            {
                descriptor.Motion = Physics::MotionType::Static;
                descriptor.Mass = 0.0f;
                descriptor.GravityScale = 0.0f;
                descriptor.ParticipatesInContacts = true;
            }

            if (collider != nullptr)
            {
                descriptor.Shapes.reserve(collider->Shapes.size());
                for (const Collider::ShapeDescriptor& shape : collider->Shapes)
                {
                    if (shape.Enabled)
                        descriptor.Shapes.push_back(ConvertShape(shape));
                }
            }

            if (Physics::Validate(descriptor) != Physics::ValidationStatus::Valid)
            {
                ++m_Diagnostics.InvalidDescriptors;
                if (auto it = m_Bindings.find(*stableId); it != m_Bindings.end())
                    RemoveBinding(*stableId, it->second);
                continue;
            }

            auto [it, inserted] = m_Bindings.try_emplace(*stableId);
            Binding& binding = it->second;
            binding.Entity = entity;
            binding.LastSeenGeneration = m_SyncGeneration;
            binding.Motion = descriptor.Motion;

            if (inserted || !m_World.Contains(binding.Handle))
            {
                binding.Handle = m_World.AddBody(descriptor);
                if (!binding.Handle.IsValid())
                {
                    ++m_Diagnostics.InvalidDescriptors;
                    m_Bindings.erase(it);
                    continue;
                }
                ++m_Diagnostics.BodiesCreated;
            }
            else if (m_World.UpdateBody(binding.Handle, descriptor))
            {
                ++m_Diagnostics.BodiesUpdated;
            }
            else
            {
                ++m_Diagnostics.StaleHandles;
                binding.Handle = m_World.AddBody(descriptor);
                if (binding.Handle.IsValid())
                    ++m_Diagnostics.BodiesCreated;
                else
                    ++m_Diagnostics.InvalidDescriptors;
            }
        }

        RemoveUntouchedBindings();
        m_Diagnostics.SidecarCount = static_cast<std::uint32_t>(m_Bindings.size());
        return m_Diagnostics;
    }

    void PhysicsBridge::WritebackDynamicTransforms(Registry& registry)
    {
        m_Diagnostics.LastWritebackOrder = NextOrder();
        auto& raw = registry.Raw();

        std::vector<const Binding*> bindings;
        bindings.reserve(m_Bindings.size());
        for (const auto& [id, binding] : m_Bindings)
            bindings.push_back(&binding);
        std::sort(bindings.begin(), bindings.end(), [](const Binding* lhs, const Binding* rhs)
        {
            return static_cast<std::uint32_t>(lhs->Entity) < static_cast<std::uint32_t>(rhs->Entity);
        });

        for (const Binding* binding : bindings)
        {
            const Physics::BodyDescriptor* body = m_World.GetBody(binding->Handle);
            if (body == nullptr || !raw.valid(binding->Entity) || !raw.all_of<Transform::Component>(binding->Entity))
                continue;

            if (body->Motion == Physics::MotionType::Static)
            {
                ++m_Diagnostics.StaticWritebacksSkipped;
                continue;
            }
            if (body->Motion == Physics::MotionType::Kinematic)
            {
                ++m_Diagnostics.KinematicWritebacksSkipped;
                continue;
            }

            auto& transform = raw.get<Transform::Component>(binding->Entity);
            transform.Position = body->Pose.Position;
            transform.Rotation = body->Pose.Rotation;
            raw.emplace_or_replace<Transform::IsDirtyTag>(binding->Entity);
            raw.emplace_or_replace<Transform::WorldUpdatedTag>(binding->Entity);
            ++m_Diagnostics.DynamicWritebacks;
        }
    }

    const PhysicsBridgeDiagnostics& PhysicsBridge::TickFixedStep(Registry& registry,
                                                                 float frameDeltaSeconds,
                                                                 const PhysicsBridgeFixedStepConfig& config)
    {
        SyncAuthoring(registry);

        if (!IsValidConfig(config) || !std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0f)
        {
            ++m_Diagnostics.InvalidFixedStepConfigs;
            m_Diagnostics.AccumulatorSeconds = m_AccumulatorSeconds;
            return m_Diagnostics;
        }

        const float clampedDelta = std::min(frameDeltaSeconds, config.MaxAccumulatedSeconds);
        m_AccumulatorSeconds = std::min(m_AccumulatorSeconds + clampedDelta, config.MaxAccumulatedSeconds);

        while (m_AccumulatorSeconds + 1.0e-7f >= config.FixedDeltaSeconds)
        {
            m_Diagnostics.LastStepOrder = NextOrder();
            const Physics::StepDiagnostics step = m_World.Step({config.FixedDeltaSeconds, config.Gravity});
            if (step.Status != Physics::ValidationStatus::Valid)
            {
                ++m_Diagnostics.InvalidFixedStepConfigs;
                break;
            }
            ++m_Diagnostics.FixedSteps;
            m_AccumulatorSeconds -= config.FixedDeltaSeconds;
        }

        WritebackDynamicTransforms(registry);
        m_Diagnostics.AccumulatorSeconds = m_AccumulatorSeconds;
        return m_Diagnostics;
    }

    void PhysicsBridge::Clear() noexcept
    {
        m_World.Clear();
        m_Bindings.clear();
        m_Diagnostics.SidecarCount = 0u;
        m_AccumulatorSeconds = 0.0f;
    }
}
