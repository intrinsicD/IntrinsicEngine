module;

#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.ECS.Component.RigidBody;

export namespace Extrinsic::ECS::Components::RigidBody
{
    enum class MotionType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class MassPolicy : std::uint8_t
    {
        Immovable,
        ExplicitMass,
        Density,
    };

    enum class ValidationStatus : std::uint8_t
    {
        Valid,
        DynamicBodyIsImmovable,
        InvalidMass,
        InvalidDensity,
        InvalidCenterOfMass,
        InvalidVelocity,
        InvalidDamping,
        InvalidGravityScale,
    };

    enum class AuthoringCombination : std::uint8_t
    {
        NoPhysicsAuthoring,
        ColliderOnlyStatic,
        ExplicitStatic,
        Kinematic,
        Dynamic,
        NonContactingBody,
        MissingColliderForContactingBody,
    };

    struct MassDescriptor
    {
        MassPolicy Policy{MassPolicy::ExplicitMass};
        float Mass{1.0f};
        float Density{1000.0f};
        glm::vec3 LocalCenterOfMass{0.0f};
    };

    struct Component
    {
        MotionType Motion{MotionType::Static};
        MassDescriptor Mass{};
        glm::vec3 LinearVelocity{0.0f};
        glm::vec3 AngularVelocity{0.0f};
        float LinearDamping{0.0f};
        float AngularDamping{0.0f};
        float GravityScale{1.0f};
        bool AllowSleep{true};
        bool StartsAwake{true};
        bool ContinuousCollisionDetection{false};
        bool ParticipatesInContacts{true};
        bool Enabled{true};
    };

    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept;
    [[nodiscard]] Component MakeStatic() noexcept;
    [[nodiscard]] Component MakeKinematic() noexcept;
    [[nodiscard]] Component MakeDynamic(float mass = 1.0f) noexcept;
    [[nodiscard]] ValidationStatus Validate(const Component& component) noexcept;
    [[nodiscard]] AuthoringCombination ClassifyAuthoringCombination(bool hasCollider,
                                                                    const Component* rigidBody) noexcept;
}
