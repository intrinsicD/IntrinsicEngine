module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Physics.World;

import Geometry.Capsule;
import Geometry.ContactManifold;
import Geometry.OBB;
import Geometry.Sphere;

namespace Extrinsic::Physics
{
    namespace
    {
        constexpr float kRotationLengthEpsilon = 1.0e-6f;
        constexpr float kScaleUniformEpsilon = 1.0e-4f;

        struct BuiltCollisionShape
        {
            ShapeReference Reference{};
            MotionType Motion{MotionType::Static};
            ShapeKind Kind{ShapeKind::Sphere};
            bool IsTrigger{false};
            Geometry::Sphere Sphere{};
            Geometry::Capsule Capsule{};
            Geometry::OBB Box{};
        };

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

        [[nodiscard]] bool IsUniformScale(const glm::vec3& scale) noexcept
        {
            return std::abs(scale.x - scale.y) <= kScaleUniformEpsilon &&
                   std::abs(scale.x - scale.z) <= kScaleUniformEpsilon;
        }

        [[nodiscard]] float MaxScaleComponent(const glm::vec3& scale) noexcept
        {
            const glm::vec3 absScale = glm::abs(scale);
            return std::max(absScale.x, std::max(absScale.y, absScale.z));
        }

        [[nodiscard]] glm::quat ComposeRotation(const Transform& bodyPose, const Transform& local)
        {
            return glm::normalize(bodyPose.Rotation * local.Rotation);
        }

        [[nodiscard]] glm::vec3 ComposePosition(const Transform& bodyPose, const Transform& local)
        {
            return bodyPose.Position + bodyPose.Rotation * (bodyPose.Scale * local.Position);
        }

        [[nodiscard]] std::optional<BuiltCollisionShape> BuildCollisionShape(BodyHandle body,
                                                                             MotionType motion,
                                                                             const Transform& bodyPose,
                                                                             const ShapeDescriptor& shape,
                                                                             std::uint32_t shapeIndex,
                                                                             CollisionDiagnostics& diagnostics)
        {
            ++diagnostics.ShapesVisited;
            if (!shape.Enabled)
            {
                diagnostics.LastRejectReason = CollisionRejectReason::DisabledShape;
                ++diagnostics.DisabledShapesSkipped;
                return std::nullopt;
            }

            if (Validate(shape) != ValidationStatus::Valid)
            {
                diagnostics.Status = ValidationStatus::InvalidShape;
                diagnostics.LastRejectReason = CollisionRejectReason::InvalidShape;
                ++diagnostics.InvalidShapesRejected;
                return std::nullopt;
            }

            const glm::vec3 scale = bodyPose.Scale * shape.Local.Scale;
            if (motion == MotionType::Dynamic && !IsUniformScale(scale))
            {
                diagnostics.LastRejectReason = CollisionRejectReason::NonUniformDynamicScale;
                ++diagnostics.DynamicNonUniformScaleRejects;
                return std::nullopt;
            }

            BuiltCollisionShape built{};
            built.Reference = ShapeReference{body, shapeIndex};
            built.Motion = motion;
            built.Kind = shape.Kind;
            built.IsTrigger = shape.IsTrigger;

            const glm::vec3 center = ComposePosition(bodyPose, shape.Local);
            const glm::quat rotation = ComposeRotation(bodyPose, shape.Local);

            switch (shape.Kind)
            {
            case ShapeKind::Sphere:
                built.Sphere.Center = center;
                built.Sphere.Radius = shape.Radius * MaxScaleComponent(scale);
                break;
            case ShapeKind::Capsule:
                {
                    const glm::vec3 axis = rotation * glm::vec3(0.0f, shape.CapsuleHalfHeight * std::abs(scale.y), 0.0f);
                    built.Capsule.PointA = center - axis;
                    built.Capsule.PointB = center + axis;
                    built.Capsule.Radius = shape.Radius * std::max(std::abs(scale.x), std::abs(scale.z));
                    break;
                }
            case ShapeKind::Box:
                built.Box.Center = center;
                built.Box.Extents = shape.HalfExtents * glm::abs(scale);
                built.Box.Rotation = rotation;
                break;
            }

            return built;
        }

        template <typename ShapeA, typename ShapeB>
        void AppendContactIfPresent(const ShapeReference& refA,
                                    const ShapeReference& refB,
                                    bool isTrigger,
                                    const ShapeA& a,
                                    const ShapeB& b,
                                    CollisionResult& result)
        {
            const std::optional<Geometry::ContactManifold> contact = Geometry::ComputeContact(a, b);
            if (!contact)
                return;

            ContactRecord record{};
            record.A = refA;
            record.B = refB;
            record.Normal = contact->Normal;
            record.PenetrationDepth = contact->PenetrationDepth;
            record.ContactPointA = contact->ContactPointA;
            record.ContactPointB = contact->ContactPointB;
            record.IsTrigger = isTrigger;
            result.Contacts.push_back(record);
            ++result.Diagnostics.ContactsGenerated;
            if (isTrigger)
                ++result.Diagnostics.TriggerContacts;
        }

        void DispatchContact(const BuiltCollisionShape& a, const BuiltCollisionShape& b, CollisionResult& result)
        {
            const bool trigger = a.IsTrigger || b.IsTrigger;
            switch (a.Kind)
            {
            case ShapeKind::Sphere:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Sphere, result);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Capsule, result);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Box, result);
                    return;
                }
                break;
            case ShapeKind::Capsule:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Sphere, result);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Capsule, result);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Box, result);
                    return;
                }
                break;
            case ShapeKind::Box:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Sphere, result);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Capsule, result);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Box, result);
                    return;
                }
                break;
            }

            ++result.Diagnostics.UnsupportedPairs;
            result.Diagnostics.LastRejectReason = CollisionRejectReason::UnsupportedPair;
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

    [[nodiscard]] CollisionResult World::ComputeCollisionContacts() const
    {
        CollisionResult result{};
        std::vector<BuiltCollisionShape> shapes{};
        shapes.reserve(m_Diagnostics.BodyCount);

        for (std::uint32_t bodyIndex = 0u; bodyIndex < m_Slots.size(); ++bodyIndex)
        {
            const Slot& slot = m_Slots[bodyIndex];
            if (!slot.Occupied)
                continue;

            ++result.Diagnostics.BodiesVisited;
            const BodyDescriptor& body = slot.Body;
            if (!body.Enabled)
            {
                result.Diagnostics.LastRejectReason = CollisionRejectReason::DisabledBody;
                ++result.Diagnostics.DisabledBodiesSkipped;
                continue;
            }

            if (!body.ParticipatesInContacts)
            {
                result.Diagnostics.LastRejectReason = CollisionRejectReason::FilteredBody;
                ++result.Diagnostics.FilteredBodiesSkipped;
                continue;
            }

            const ValidationStatus bodyStatus = Validate(body);
            if (bodyStatus != ValidationStatus::Valid)
            {
                result.Diagnostics.Status = bodyStatus;
                result.Diagnostics.LastRejectReason = CollisionRejectReason::InvalidBody;
                ++result.Diagnostics.InvalidBodiesRejected;
                continue;
            }

            const BodyHandle handle{bodyIndex, slot.Generation};
            for (std::uint32_t shapeIndex = 0u; shapeIndex < body.Shapes.size(); ++shapeIndex)
            {
                if (std::optional<BuiltCollisionShape> shape =
                        BuildCollisionShape(handle,
                                            body.Motion,
                                            body.Pose,
                                            body.Shapes[shapeIndex],
                                            shapeIndex,
                                            result.Diagnostics))
                {
                    shapes.push_back(*shape);
                }
            }
        }

        for (std::size_t i = 0u; i < shapes.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < shapes.size(); ++j)
            {
                const BuiltCollisionShape& a = shapes[i];
                const BuiltCollisionShape& b = shapes[j];
                if (a.Reference.Body.Index == b.Reference.Body.Index)
                    continue;

                result.Candidates.push_back(CollisionCandidatePair{a.Reference, b.Reference});
                ++result.Diagnostics.BroadphasePairs;
                DispatchContact(a, b, result);
            }
        }

        return result;
    }

    void World::Clear() noexcept
    {
        m_Slots.clear();
        m_FreeList.clear();
        m_Diagnostics.BodyCount = 0u;
    }
}
