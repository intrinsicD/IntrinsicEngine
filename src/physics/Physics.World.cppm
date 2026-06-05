module;

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Physics.World;

import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Physics
{
    struct BodyTag
    {
    };

    using BodyHandle = Extrinsic::Core::StrongHandle<BodyTag>;

    enum class MotionType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class ShapeKind : std::uint8_t
    {
        Sphere,
        Capsule,
        Box,
    };

    enum class ValidationStatus : std::uint8_t
    {
        Valid,
        InvalidPose,
        EmptyShapeList,
        InvalidShape,
        InvalidMass,
        InvalidVelocity,
        InvalidDamping,
        InvalidGravityScale,
        InvalidStep,
    };

    struct Transform
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f};
    };

    struct ShapeDescriptor
    {
        ShapeKind Kind{ShapeKind::Sphere};
        Transform Local{};
        glm::vec3 HalfExtents{0.5f};
        float Radius{0.5f};
        float CapsuleHalfHeight{0.5f};
        bool IsTrigger{false};
        bool Enabled{true};
    };

    struct BodyDescriptor
    {
        MotionType Motion{MotionType::Static};
        Transform Pose{};
        glm::vec3 LinearVelocity{0.0f};
        glm::vec3 AngularVelocity{0.0f};
        float Mass{1.0f};
        float LinearDamping{0.0f};
        float AngularDamping{0.0f};
        float GravityScale{1.0f};
        bool Enabled{true};
        bool ParticipatesInContacts{true};
        std::vector<ShapeDescriptor> Shapes{};
    };

    struct StepInput
    {
        float DeltaSeconds{1.0f / 60.0f};
        glm::vec3 Gravity{0.0f, -9.80665f, 0.0f};
    };

    struct StepDiagnostics
    {
        ValidationStatus Status{ValidationStatus::Valid};
        std::uint32_t StepIndex{0u};
        std::uint32_t BodiesVisited{0u};
        std::uint32_t DynamicBodiesIntegrated{0u};
        std::uint32_t KinematicBodiesIntegrated{0u};
        std::uint32_t StaticBodiesSkipped{0u};
        std::uint32_t DisabledBodiesSkipped{0u};
    };

    struct WorldDiagnostics
    {
        std::uint32_t BodyCount{0u};
        std::uint32_t BodiesCreated{0u};
        std::uint32_t BodiesDestroyed{0u};
        std::uint32_t DescriptorUpdates{0u};
        std::uint32_t InvalidDescriptorsRejected{0u};
        std::uint32_t StaleHandleRejects{0u};
        std::uint32_t StepsExecuted{0u};
        StepDiagnostics LastStep{};
    };

    [[nodiscard]] ShapeDescriptor MakeSphere(float radius, const Transform& local = {});
    [[nodiscard]] ShapeDescriptor MakeCapsule(float radius, float halfHeight, const Transform& local = {});
    [[nodiscard]] ShapeDescriptor MakeBox(const glm::vec3& halfExtents, const Transform& local = {});
    [[nodiscard]] BodyDescriptor MakeStaticBody(const Transform& pose = {});
    [[nodiscard]] BodyDescriptor MakeKinematicBody(const Transform& pose = {});
    [[nodiscard]] BodyDescriptor MakeDynamicBody(float mass = 1.0f, const Transform& pose = {});

    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept;
    [[nodiscard]] bool IsFinite(const glm::quat& value) noexcept;
    [[nodiscard]] bool IsValidUnitRotation(const glm::quat& value) noexcept;
    [[nodiscard]] ValidationStatus Validate(const Transform& transform) noexcept;
    [[nodiscard]] ValidationStatus Validate(const ShapeDescriptor& descriptor) noexcept;
    [[nodiscard]] ValidationStatus Validate(const BodyDescriptor& descriptor) noexcept;
    [[nodiscard]] ValidationStatus Validate(const StepInput& input) noexcept;

    class World
    {
    public:
        World() = default;

        [[nodiscard]] BodyHandle AddBody(const BodyDescriptor& descriptor);
        [[nodiscard]] bool DestroyBody(BodyHandle handle);
        [[nodiscard]] bool UpdateBody(BodyHandle handle, const BodyDescriptor& descriptor);
        [[nodiscard]] BodyDescriptor* GetBody(BodyHandle handle) noexcept;
        [[nodiscard]] const BodyDescriptor* GetBody(BodyHandle handle) const noexcept;
        [[nodiscard]] bool Contains(BodyHandle handle) const noexcept;

        [[nodiscard]] StepDiagnostics Step(const StepInput& input = {});
        void Clear() noexcept;

        [[nodiscard]] std::size_t BodyCount() const noexcept { return m_Diagnostics.BodyCount; }
        [[nodiscard]] const WorldDiagnostics& GetDiagnostics() const noexcept { return m_Diagnostics; }

    private:
        struct Slot
        {
            std::uint32_t Generation{1u};
            bool Occupied{false};
            BodyDescriptor Body{};
        };

        [[nodiscard]] Slot* ResolveSlot(BodyHandle handle) noexcept;
        [[nodiscard]] const Slot* ResolveSlot(BodyHandle handle) const noexcept;

        std::vector<Slot> m_Slots{};
        std::vector<std::uint32_t> m_FreeList{};
        WorldDiagnostics m_Diagnostics{};
    };
}
