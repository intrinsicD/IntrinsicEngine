module;

#include <cmath>

#include <glm/glm.hpp>

module Extrinsic.ECS.Component.RigidBody;

namespace Extrinsic::ECS::Components::RigidBody
{
    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] Component MakeStatic() noexcept
    {
        Component component{};
        component.Motion = MotionType::Static;
        component.Mass.Policy = MassPolicy::Immovable;
        component.LinearVelocity = glm::vec3{0.0f};
        component.AngularVelocity = glm::vec3{0.0f};
        component.GravityScale = 0.0f;
        return component;
    }

    [[nodiscard]] Component MakeKinematic() noexcept
    {
        Component component{};
        component.Motion = MotionType::Kinematic;
        component.Mass.Policy = MassPolicy::Immovable;
        component.GravityScale = 0.0f;
        return component;
    }

    [[nodiscard]] Component MakeDynamic(float mass) noexcept
    {
        Component component{};
        component.Motion = MotionType::Dynamic;
        component.Mass.Policy = MassPolicy::ExplicitMass;
        component.Mass.Mass = mass;
        return component;
    }

    [[nodiscard]] ValidationStatus Validate(const Component& component) noexcept
    {
        if (!IsFinite(component.Mass.LocalCenterOfMass))
            return ValidationStatus::InvalidCenterOfMass;
        if (!IsFinite(component.LinearVelocity) || !IsFinite(component.AngularVelocity))
            return ValidationStatus::InvalidVelocity;
        if (!std::isfinite(component.LinearDamping) || component.LinearDamping < 0.0f ||
            !std::isfinite(component.AngularDamping) || component.AngularDamping < 0.0f)
            return ValidationStatus::InvalidDamping;
        if (!std::isfinite(component.GravityScale))
            return ValidationStatus::InvalidGravityScale;

        if (component.Motion == MotionType::Dynamic && component.Mass.Policy == MassPolicy::Immovable)
            return ValidationStatus::DynamicBodyIsImmovable;

        if (component.Mass.Policy == MassPolicy::ExplicitMass &&
            (!std::isfinite(component.Mass.Mass) || component.Mass.Mass <= 0.0f))
            return ValidationStatus::InvalidMass;
        if (component.Mass.Policy == MassPolicy::Density &&
            (!std::isfinite(component.Mass.Density) || component.Mass.Density <= 0.0f))
            return ValidationStatus::InvalidDensity;

        return ValidationStatus::Valid;
    }

    [[nodiscard]] AuthoringCombination ClassifyAuthoringCombination(bool hasCollider,
                                                                    const Component* rigidBody) noexcept
    {
        if (rigidBody == nullptr)
            return hasCollider ? AuthoringCombination::ColliderOnlyStatic : AuthoringCombination::NoPhysicsAuthoring;

        if (!hasCollider)
        {
            return rigidBody->ParticipatesInContacts
                ? AuthoringCombination::MissingColliderForContactingBody
                : AuthoringCombination::NonContactingBody;
        }

        switch (rigidBody->Motion)
        {
        case MotionType::Static:
            return AuthoringCombination::ExplicitStatic;
        case MotionType::Kinematic:
            return AuthoringCombination::Kinematic;
        case MotionType::Dynamic:
            return AuthoringCombination::Dynamic;
        }

        return AuthoringCombination::MissingColliderForContactingBody;
    }
}
