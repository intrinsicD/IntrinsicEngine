module;

#include <cstdint>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.ECS.Component.Collider;

export namespace Extrinsic::ECS::Components::Collider
{
    enum class ShapeKind : std::uint8_t
    {
        Sphere,
        Capsule,
        Box,
    };

    enum class ValidationStatus : std::uint8_t
    {
        Valid,
        EmptyShapeList,
        InvalidLocalPose,
        InvalidMaterial,
        InvalidContactOffsets,
        InvalidSphereRadius,
        InvalidCapsuleRadius,
        InvalidCapsuleHalfHeight,
        InvalidBoxExtents,
    };

    struct LocalPose
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct Material
    {
        std::uint32_t Id{0u};
        float StaticFriction{0.5f};
        float DynamicFriction{0.5f};
        float Restitution{0.0f};
    };

    struct CollisionFilter
    {
        std::uint32_t CategoryBits{1u};
        std::uint32_t CollidesWithBits{0xFFFF'FFFFu};
        std::int32_t GroupIndex{0};
    };

    struct ContactOffsets
    {
        float ContactOffset{0.02f};
        float RestOffset{0.0f};
    };

    struct SphereShape
    {
        float Radius{0.5f};
    };

    struct CapsuleShape
    {
        float Radius{0.5f};
        // Half length of the capsule segment, excluding hemispherical caps.
        // The segment is aligned with the child shape's local Y axis.
        float HalfHeight{0.5f};
    };

    struct BoxShape
    {
        // Half-extents in the child shape's local frame. LocalPose supplies OBB orientation.
        glm::vec3 HalfExtents{0.5f};
    };

    using ShapeData = std::variant<SphereShape, CapsuleShape, BoxShape>;

    struct ShapeDescriptor
    {
        ShapeData Shape{SphereShape{}};
        LocalPose Local{};
        Material Surface{};
        CollisionFilter Filter{};
        ContactOffsets Offsets{};
        bool IsTrigger{false};
        bool Enabled{true};
    };

    struct Component
    {
        std::vector<ShapeDescriptor> Shapes{};
        bool Enabled{true};
    };

    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept;
    [[nodiscard]] bool IsFinite(const glm::quat& value) noexcept;
    [[nodiscard]] bool IsValidUnitRotation(const glm::quat& value) noexcept;
    [[nodiscard]] ShapeKind GetShapeKind(const ShapeData& shape) noexcept;
    [[nodiscard]] ShapeKind GetShapeKind(const ShapeDescriptor& shape) noexcept;
    [[nodiscard]] ShapeDescriptor MakeSphere(float radius, const LocalPose& local = {});
    [[nodiscard]] ShapeDescriptor MakeCapsule(float radius, float halfHeight, const LocalPose& local = {});
    [[nodiscard]] ShapeDescriptor MakeBox(const glm::vec3& halfExtents, const LocalPose& local = {});
    [[nodiscard]] bool IsValid(const Material& material) noexcept;
    [[nodiscard]] bool IsValid(const ContactOffsets& offsets) noexcept;
    [[nodiscard]] ValidationStatus Validate(const ShapeDescriptor& descriptor) noexcept;
    [[nodiscard]] ValidationStatus Validate(const Component& component) noexcept;
}
