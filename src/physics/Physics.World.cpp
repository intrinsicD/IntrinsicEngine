module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Physics.World;

namespace Extrinsic::Physics
{
    namespace
    {
        constexpr float kRotationLengthEpsilon = 1.0e-6f;

        [[nodiscard]] float DampingFactor(float damping, float dt) noexcept
        {
            return 1.0f / (1.0f + damping * dt);
        }

        void IntegrateAngular(Transform& pose, const glm::vec3& angularVelocity, float dt)
        {
            const float angularSpeed = glm::length(angularVelocity);
            if (!std::isfinite(angularSpeed) || angularSpeed <= 1.0e-6f)
                return;

            const glm::quat delta = glm::angleAxis(angularSpeed * dt, angularVelocity / angularSpeed);
            pose.Rotation = glm::normalize(delta * pose.Rotation);
        }

        [[nodiscard]] std::uint32_t BumpGeneration(std::uint32_t generation) noexcept
        {
            ++generation;
            return generation == 0u ? 1u : generation;
        }
    }

    [[nodiscard]] ShapeDescriptor MakeSphere(float radius, const Transform& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Kind = ShapeKind::Sphere;
        descriptor.Radius = radius;
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] ShapeDescriptor MakeCapsule(float radius, float halfHeight, const Transform& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Kind = ShapeKind::Capsule;
        descriptor.Radius = radius;
        descriptor.CapsuleHalfHeight = halfHeight;
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] ShapeDescriptor MakeBox(const glm::vec3& halfExtents, const Transform& local)
    {
        ShapeDescriptor descriptor{};
        descriptor.Kind = ShapeKind::Box;
        descriptor.HalfExtents = halfExtents;
        descriptor.Local = local;
        return descriptor;
    }

    [[nodiscard]] BodyDescriptor MakeStaticBody(const Transform& pose)
    {
        BodyDescriptor descriptor{};
        descriptor.Motion = MotionType::Static;
        descriptor.Pose = pose;
        descriptor.Mass = 0.0f;
        descriptor.GravityScale = 0.0f;
        descriptor.Shapes.push_back(MakeSphere(0.5f));
        return descriptor;
    }

    [[nodiscard]] BodyDescriptor MakeKinematicBody(const Transform& pose)
    {
        BodyDescriptor descriptor{};
        descriptor.Motion = MotionType::Kinematic;
        descriptor.Pose = pose;
        descriptor.Mass = 0.0f;
        descriptor.GravityScale = 0.0f;
        descriptor.Shapes.push_back(MakeSphere(0.5f));
        return descriptor;
    }

    [[nodiscard]] BodyDescriptor MakeDynamicBody(float mass, const Transform& pose)
    {
        BodyDescriptor descriptor{};
        descriptor.Motion = MotionType::Dynamic;
        descriptor.Pose = pose;
        descriptor.Mass = mass;
        descriptor.Shapes.push_back(MakeSphere(0.5f));
        return descriptor;
    }

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
        if (!IsFinite(value))
            return false;
        const float lengthSq = value.w * value.w + value.x * value.x + value.y * value.y + value.z * value.z;
        return std::isfinite(lengthSq) && lengthSq > kRotationLengthEpsilon && std::abs(lengthSq - 1.0f) <= 1.0e-3f;
    }

    [[nodiscard]] ValidationStatus Validate(const Transform& transform) noexcept
    {
        if (!IsFinite(transform.Position) || !IsValidUnitRotation(transform.Rotation) || !IsFinite(transform.Scale) ||
            transform.Scale.x <= 0.0f || transform.Scale.y <= 0.0f || transform.Scale.z <= 0.0f)
            return ValidationStatus::InvalidPose;
        return ValidationStatus::Valid;
    }

    [[nodiscard]] ValidationStatus Validate(const ShapeDescriptor& descriptor) noexcept
    {
        if (!descriptor.Enabled)
            return ValidationStatus::Valid;
        if (Validate(descriptor.Local) != ValidationStatus::Valid)
            return ValidationStatus::InvalidPose;

        switch (descriptor.Kind)
        {
        case ShapeKind::Sphere:
            return std::isfinite(descriptor.Radius) && descriptor.Radius > 0.0f
                ? ValidationStatus::Valid
                : ValidationStatus::InvalidShape;
        case ShapeKind::Capsule:
            return std::isfinite(descriptor.Radius) && descriptor.Radius > 0.0f &&
                   std::isfinite(descriptor.CapsuleHalfHeight) && descriptor.CapsuleHalfHeight > 0.0f
                ? ValidationStatus::Valid
                : ValidationStatus::InvalidShape;
        case ShapeKind::Box:
            return IsFinite(descriptor.HalfExtents) && descriptor.HalfExtents.x > 0.0f &&
                   descriptor.HalfExtents.y > 0.0f && descriptor.HalfExtents.z > 0.0f
                ? ValidationStatus::Valid
                : ValidationStatus::InvalidShape;
        }

        return ValidationStatus::InvalidShape;
    }

    [[nodiscard]] ValidationStatus Validate(const BodyDescriptor& descriptor) noexcept
    {
        if (!descriptor.Enabled)
            return ValidationStatus::Valid;
        if (Validate(descriptor.Pose) != ValidationStatus::Valid)
            return ValidationStatus::InvalidPose;
        if (!IsFinite(descriptor.LinearVelocity) || !IsFinite(descriptor.AngularVelocity))
            return ValidationStatus::InvalidVelocity;
        if (!std::isfinite(descriptor.LinearDamping) || descriptor.LinearDamping < 0.0f ||
            !std::isfinite(descriptor.AngularDamping) || descriptor.AngularDamping < 0.0f)
            return ValidationStatus::InvalidDamping;
        if (!std::isfinite(descriptor.GravityScale))
            return ValidationStatus::InvalidGravityScale;
        if (descriptor.Motion == MotionType::Dynamic && (!std::isfinite(descriptor.Mass) || descriptor.Mass <= 0.0f))
            return ValidationStatus::InvalidMass;

        std::uint32_t enabledShapes = 0u;
        for (const ShapeDescriptor& shape : descriptor.Shapes)
        {
            const ValidationStatus status = Validate(shape);
            if (status != ValidationStatus::Valid)
                return status;
            if (shape.Enabled)
                ++enabledShapes;
        }

        if (descriptor.ParticipatesInContacts && enabledShapes == 0u)
            return ValidationStatus::EmptyShapeList;

        return ValidationStatus::Valid;
    }

    [[nodiscard]] ValidationStatus Validate(const StepInput& input) noexcept
    {
        if (!std::isfinite(input.DeltaSeconds) || input.DeltaSeconds <= 0.0f || !IsFinite(input.Gravity))
            return ValidationStatus::InvalidStep;
        return ValidationStatus::Valid;
    }

    [[nodiscard]] World::Slot* World::ResolveSlot(BodyHandle handle) noexcept
    {
        if (!handle.IsValid() || handle.Index >= m_Slots.size())
            return nullptr;
        Slot& slot = m_Slots[handle.Index];
        if (!slot.Occupied || slot.Generation != handle.Generation)
            return nullptr;
        return &slot;
    }

    [[nodiscard]] const World::Slot* World::ResolveSlot(BodyHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.Index >= m_Slots.size())
            return nullptr;
        const Slot& slot = m_Slots[handle.Index];
        if (!slot.Occupied || slot.Generation != handle.Generation)
            return nullptr;
        return &slot;
    }

    [[nodiscard]] BodyHandle World::AddBody(const BodyDescriptor& descriptor)
    {
        if (Validate(descriptor) != ValidationStatus::Valid)
        {
            ++m_Diagnostics.InvalidDescriptorsRejected;
            return {};
        }

        std::uint32_t index = 0u;
        if (!m_FreeList.empty())
        {
            index = m_FreeList.back();
            m_FreeList.pop_back();
            Slot& slot = m_Slots[index];
            slot.Occupied = true;
            slot.Body = descriptor;
        }
        else
        {
            index = static_cast<std::uint32_t>(m_Slots.size());
            Slot slot{};
            slot.Occupied = true;
            slot.Body = descriptor;
            m_Slots.push_back(slot);
        }

        ++m_Diagnostics.BodyCount;
        ++m_Diagnostics.BodiesCreated;
        return BodyHandle{index, m_Slots[index].Generation};
    }

    [[nodiscard]] bool World::DestroyBody(BodyHandle handle)
    {
        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr)
        {
            ++m_Diagnostics.StaleHandleRejects;
            return false;
        }

        slot->Occupied = false;
        slot->Body = {};
        slot->Generation = BumpGeneration(slot->Generation);
        m_FreeList.push_back(handle.Index);
        --m_Diagnostics.BodyCount;
        ++m_Diagnostics.BodiesDestroyed;
        return true;
    }

    [[nodiscard]] bool World::UpdateBody(BodyHandle handle, const BodyDescriptor& descriptor)
    {
        if (Validate(descriptor) != ValidationStatus::Valid)
        {
            ++m_Diagnostics.InvalidDescriptorsRejected;
            return false;
        }

        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr)
        {
            ++m_Diagnostics.StaleHandleRejects;
            return false;
        }

        slot->Body = descriptor;
        ++m_Diagnostics.DescriptorUpdates;
        return true;
    }

    [[nodiscard]] BodyDescriptor* World::GetBody(BodyHandle handle) noexcept
    {
        if (Slot* slot = ResolveSlot(handle))
            return &slot->Body;
        return nullptr;
    }

    [[nodiscard]] const BodyDescriptor* World::GetBody(BodyHandle handle) const noexcept
    {
        if (const Slot* slot = ResolveSlot(handle))
            return &slot->Body;
        return nullptr;
    }

    [[nodiscard]] bool World::Contains(BodyHandle handle) const noexcept
    {
        return ResolveSlot(handle) != nullptr;
    }

    [[nodiscard]] StepDiagnostics World::Step(const StepInput& input)
    {
        StepDiagnostics diagnostics{};
        diagnostics.StepIndex = m_Diagnostics.StepsExecuted + 1u;
        diagnostics.Status = Validate(input);
        if (diagnostics.Status != ValidationStatus::Valid)
        {
            m_Diagnostics.LastStep = diagnostics;
            return diagnostics;
        }

        const float dt = input.DeltaSeconds;
        for (Slot& slot : m_Slots)
        {
            if (!slot.Occupied)
                continue;

            BodyDescriptor& body = slot.Body;
            ++diagnostics.BodiesVisited;
            if (!body.Enabled)
            {
                ++diagnostics.DisabledBodiesSkipped;
                continue;
            }

            switch (body.Motion)
            {
            case MotionType::Static:
                ++diagnostics.StaticBodiesSkipped;
                break;
            case MotionType::Kinematic:
                body.Pose.Position += body.LinearVelocity * dt;
                IntegrateAngular(body.Pose, body.AngularVelocity, dt);
                ++diagnostics.KinematicBodiesIntegrated;
                break;
            case MotionType::Dynamic:
                body.LinearVelocity += input.Gravity * body.GravityScale * dt;
                body.LinearVelocity *= DampingFactor(body.LinearDamping, dt);
                body.AngularVelocity *= DampingFactor(body.AngularDamping, dt);
                body.Pose.Position += body.LinearVelocity * dt;
                IntegrateAngular(body.Pose, body.AngularVelocity, dt);
                ++diagnostics.DynamicBodiesIntegrated;
                break;
            }
        }

        ++m_Diagnostics.StepsExecuted;
        diagnostics.StepIndex = m_Diagnostics.StepsExecuted;
        m_Diagnostics.LastStep = diagnostics;
        return diagnostics;
    }

    void World::Clear() noexcept
    {
        m_Slots.clear();
        m_FreeList.clear();
        m_Diagnostics.BodyCount = 0u;
    }
}
