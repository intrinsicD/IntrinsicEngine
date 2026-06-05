module;

#include <cmath>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.ECS.Component.Collider;

namespace Extrinsic::ECS::Components::Collider
{
    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] bool IsFinite(const glm::quat& value) noexcept
    {
        return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] bool IsValidUnitRotation(const glm::quat& value) noexcept
    {
        const float lengthSq = value.w * value.w + value.x * value.x + value.y * value.y + value.z * value.z;
        return IsFinite(value) && lengthSq > 0.0f && std::abs(lengthSq - 1.0f) <= 1.0e-3f;
    }

    [[nodiscard]] ShapeKind GetShapeKind(const ShapeData& shape) noexcept
    {
        if (std::holds_alternative<SphereShape>(shape))
            return ShapeKind::Sphere;
        if (std::holds_alternative<CapsuleShape>(shape))
            return ShapeKind::Capsule;
        return ShapeKind::Box;
    }

    [[nodiscard]] ShapeKind GetShapeKind(const ShapeDescriptor& shape) noexcept
    {
        return GetShapeKind(shape.Shape);
    }

    [[nodiscard]] ShapeDescriptor MakeSphere(float radius, const LocalPose& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Shape = SphereShape{radius};
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] ShapeDescriptor MakeCapsule(float radius, float halfHeight, const LocalPose& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Shape = CapsuleShape{radius, halfHeight};
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] ShapeDescriptor MakeBox(const glm::vec3& halfExtents, const LocalPose& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Shape = BoxShape{halfExtents};
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] bool IsValid(const Material& material) noexcept
    {
        return std::isfinite(material.StaticFriction) && material.StaticFriction >= 0.0f &&
               std::isfinite(material.DynamicFriction) && material.DynamicFriction >= 0.0f &&
               std::isfinite(material.Restitution) && material.Restitution >= 0.0f && material.Restitution <= 1.0f;
    }

    [[nodiscard]] bool IsValid(const ContactOffsets& offsets) noexcept
    {
        return std::isfinite(offsets.ContactOffset) && std::isfinite(offsets.RestOffset) &&
               offsets.ContactOffset >= 0.0f && offsets.RestOffset >= 0.0f &&
               offsets.RestOffset <= offsets.ContactOffset;
    }

    [[nodiscard]] ValidationStatus Validate(const ShapeDescriptor& descriptor) noexcept
    {
        if (!IsFinite(descriptor.Local.Position) || !IsValidUnitRotation(descriptor.Local.Rotation))
            return ValidationStatus::InvalidLocalPose;
        if (!IsValid(descriptor.Surface))
            return ValidationStatus::InvalidMaterial;
        if (!IsValid(descriptor.Offsets))
            return ValidationStatus::InvalidContactOffsets;

        if (const auto* sphere = std::get_if<SphereShape>(&descriptor.Shape))
        {
            if (!std::isfinite(sphere->Radius) || sphere->Radius <= 0.0f)
                return ValidationStatus::InvalidSphereRadius;
            return ValidationStatus::Valid;
        }
        if (const auto* capsule = std::get_if<CapsuleShape>(&descriptor.Shape))
        {
            if (!std::isfinite(capsule->Radius) || capsule->Radius <= 0.0f)
                return ValidationStatus::InvalidCapsuleRadius;
            if (!std::isfinite(capsule->HalfHeight) || capsule->HalfHeight <= 0.0f)
                return ValidationStatus::InvalidCapsuleHalfHeight;
            return ValidationStatus::Valid;
        }

        const auto& box = std::get<BoxShape>(descriptor.Shape);
        if (!IsFinite(box.HalfExtents) || box.HalfExtents.x <= 0.0f || box.HalfExtents.y <= 0.0f ||
            box.HalfExtents.z <= 0.0f)
            return ValidationStatus::InvalidBoxExtents;
        return ValidationStatus::Valid;
    }

    [[nodiscard]] ValidationStatus Validate(const Component& component) noexcept
    {
        if (component.Shapes.empty())
            return ValidationStatus::EmptyShapeList;

        for (const ShapeDescriptor& shape : component.Shapes)
        {
            const ValidationStatus status = Validate(shape);
            if (status != ValidationStatus::Valid)
                return status;
        }
        return ValidationStatus::Valid;
    }
}
