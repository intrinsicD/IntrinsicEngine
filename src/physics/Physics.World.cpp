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

import Extrinsic.Core.Memory;

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


        struct BuiltCollisionShape
        {
            ShapeReference Reference{};
            ShapeKind Kind{ShapeKind::Sphere};
            bool IsTrigger{false};
            Geometry::Sphere Sphere{};
            Geometry::Capsule Capsule{};
            Geometry::OBB Box{};
        };

        [[nodiscard]] float DampingFactor(float damping, float dt) noexcept
        {
            // Matches the canonical METHOD-001 reference
            // (methods/physics/rigid_body_reference): explicit linear decay
            // clamped at zero. PHYSICS-003 aligned this from the previous
            // 1/(1+c*dt) variant so solver parity fixtures track the
            // reference exactly; the two agree to first order in c*dt.
            if (!std::isfinite(damping) || damping <= 0.0f)
                return 1.0f;
            return std::max(0.0f, 1.0f - damping * dt);
        }

        [[nodiscard]] bool IntegrateAngular(Transform& pose, const glm::vec3& angularVelocity, float dt)
        {
            const float angularSpeed = glm::length(angularVelocity);
            if (!std::isfinite(angularSpeed))
                return false;
            if (angularSpeed <= 1.0e-6f)
                return true;
            const float angle = angularSpeed * dt;
            if (!std::isfinite(angle))
                return false;
            const glm::quat delta = glm::angleAxis(angle, angularVelocity / angularSpeed);
            pose.Rotation = glm::normalize(delta * pose.Rotation);
            return IsValidUnitRotation(pose.Rotation);
        }

        [[nodiscard]] bool IsSupportedShapeTransform(
            const Transform& pose, MotionType motion, const ShapeDescriptor& shape);

        void IntegrateBody(BodyDescriptor& body, const StepInput& input, StepDiagnostics& diagnostics)
        {
            ++diagnostics.BodiesVisited;
            if (!body.Enabled)
            {
                ++diagnostics.DisabledBodiesSkipped;
                return;
            }
            if (body.Motion == MotionType::Static)
            {
                ++diagnostics.StaticBodiesSkipped;
                return;
            }

            if (body.Motion == MotionType::Kinematic && !input.IntegrateKinematicPoses)
                return;
            Transform pose = body.Pose;
            glm::vec3 linearVelocity = body.LinearVelocity;
            glm::vec3 angularVelocity = body.AngularVelocity;
            const float dt = input.DeltaSeconds;
            if (body.Motion == MotionType::Dynamic)
            {
                linearVelocity += input.Gravity * body.GravityScale * dt;
                linearVelocity *= DampingFactor(body.LinearDamping, dt);
                angularVelocity *= DampingFactor(body.AngularDamping, dt);
            }
            pose.Position += linearVelocity * dt;
            if (!Extrinsic::Physics::IsFinite(pose.Position) ||
                !Extrinsic::Physics::IsFinite(linearVelocity) ||
                !Extrinsic::Physics::IsFinite(angularVelocity) ||
                !IntegrateAngular(pose, angularVelocity, dt))
            {
                diagnostics.Status = ValidationStatus::NumericalFailure;
                return;
            }
            if (body.ParticipatesInContacts)
            {
                for (const ShapeDescriptor& shape : body.Shapes)
                {
                    if (shape.Enabled && !IsSupportedShapeTransform(pose, body.Motion, shape))
                    {
                        diagnostics.Status = ValidationStatus::NumericalFailure;
                        return;
                    }
                }
            }
            body.Pose = pose;
            body.LinearVelocity = linearVelocity;
            body.AngularVelocity = angularVelocity;
            if (body.Motion == MotionType::Dynamic)
                ++diagnostics.DynamicBodiesIntegrated;
            else
                ++diagnostics.KinematicBodiesIntegrated;
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

        [[nodiscard]] glm::dmat3 ScaledChildAxes(const Transform& bodyPose, const Transform& local)
        {
            // Left multiplication by the body rotation preserves lengths and
            // angles, so only the scaled child frame is needed here.
            glm::dmat3 axes = glm::mat3_cast(glm::normalize(glm::dquat(local.Rotation)));
            for (glm::length_t column = 0; column < 3; ++column)
                axes[column] *= glm::dvec3(bodyPose.Scale) * static_cast<double>(local.Scale[column]);
            return axes;
        }

        [[nodiscard]] glm::vec3 AxisLengths(const glm::dmat3& axes)
        {
            return glm::vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
        }

        [[nodiscard]] bool IsSupportedShapeTransform(
            const Transform& pose, MotionType motion, const ShapeDescriptor& shape)
        {
            const glm::dmat3 axes = ScaledChildAxes(pose, shape.Local);
            const glm::vec3 scale = AxisLengths(axes);
            if (!Extrinsic::Physics::IsFinite(scale) || scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
                return false;
            for (glm::length_t a = 0; a < 3; ++a)
                for (glm::length_t b = a + 1; b < 3; ++b)
                    if (std::abs(glm::dot(glm::normalize(axes[a]), glm::normalize(axes[b]))) > kScaleUniformEpsilon)
                        return false;
            if (motion == MotionType::Dynamic && !IsUniformScale(scale))
                return false;
            if (!Extrinsic::Physics::IsFinite(ComposePosition(pose, shape.Local)))
                return false;
            switch (shape.Kind)
            {
            case ShapeKind::Sphere:
                return std::isfinite(shape.Radius * MaxScaleComponent(scale));
            case ShapeKind::Capsule:
                return std::isfinite(shape.CapsuleHalfHeight * scale.y) &&
                    std::isfinite(shape.Radius * std::max(scale.x, scale.z));
            case ShapeKind::Box:
                return Extrinsic::Physics::IsFinite(shape.HalfExtents * scale);
            }
            return false;
        }

        [[nodiscard]] std::optional<BuiltCollisionShape> BuildCollisionShape(BodyHandle body,
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

            const glm::vec3 scale = AxisLengths(ScaledChildAxes(bodyPose, shape.Local));

            BuiltCollisionShape built{};
            built.Reference = ShapeReference{body, shapeIndex};
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

        [[nodiscard]] glm::vec3 ShapeWorldCenter(const BuiltCollisionShape& shape) noexcept
        {
            switch (shape.Kind)
            {
            case ShapeKind::Sphere:
                return shape.Sphere.Center;
            case ShapeKind::Capsule:
                return (shape.Capsule.PointA + shape.Capsule.PointB) * 0.5f;
            case ShapeKind::Box:
                return shape.Box.Center;
            }
            return glm::vec3{0.0f};
        }

        template <typename ShapeA, typename ShapeB>
        void AppendContactIfPresent(const ShapeReference& refA,
                                    const ShapeReference& refB,
                                    bool isTrigger,
                                    const ShapeA& a,
                                    const ShapeB& b,
                                    const glm::vec3& centerA,
                                    const glm::vec3& centerB,
                                    CollisionResult& result,
                                    Extrinsic::Core::Memory::LinearArena& scratch)
        {
            const std::optional<Geometry::ContactManifold> contact = Geometry::ComputeContact(a, b, scratch);
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

            // PHYSICS-003: enforce the documented A->B normal convention on
            // the physics-owned record. The geometry kernel honors this
            // convention since BUG-025; the shape-center orientation check
            // stays as defense in depth so a future kernel regression
            // degrades to a re-orientation instead of a tunneling solver.
            // A coincident-center manifold is left unchanged.
            const glm::vec3 centerDelta = centerB - centerA;
            if (glm::dot(record.Normal, centerDelta) < 0.0f)
            {
                record.Normal = -record.Normal;
                std::swap(record.ContactPointA, record.ContactPointB);
            }

            result.Contacts.push_back(record);
            ++result.Diagnostics.ContactsGenerated;
            if (isTrigger)
                ++result.Diagnostics.TriggerContacts;
        }

        void DispatchContact(const BuiltCollisionShape& a, const BuiltCollisionShape& b,
                             CollisionResult& result, Extrinsic::Core::Memory::LinearArena& scratch)
        {
            const bool trigger = a.IsTrigger || b.IsTrigger;
            const glm::vec3 centerA = ShapeWorldCenter(a);
            const glm::vec3 centerB = ShapeWorldCenter(b);
            switch (a.Kind)
            {
            case ShapeKind::Sphere:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Sphere, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Capsule, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Sphere, b.Box, centerA, centerB, result, scratch);
                    return;
                }
                break;
            case ShapeKind::Capsule:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Sphere, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Capsule, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Capsule, b.Box, centerA, centerB, result, scratch);
                    return;
                }
                break;
            case ShapeKind::Box:
                switch (b.Kind)
                {
                case ShapeKind::Sphere:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Sphere, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Capsule:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Capsule, centerA, centerB, result, scratch);
                    return;
                case ShapeKind::Box:
                    AppendContactIfPresent(a.Reference, b.Reference, trigger, a.Box, b.Box, centerA, centerB, result, scratch);
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
            {
                ++enabledShapes;
                if (descriptor.ParticipatesInContacts && !IsSupportedShapeTransform(descriptor.Pose, descriptor.Motion, shape))
                    return ValidationStatus::UnsupportedShapeTransform;
            }
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
            slot.Sleep = {};
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
        if (descriptor.Motion != MotionType::Dynamic)
            WakeDynamicBodies();
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
        slot->Sleep = {};
        slot->Generation = BumpGeneration(slot->Generation);
        m_FreeList.push_back(handle.Index);
        --m_Diagnostics.BodyCount;
        ++m_Diagnostics.BodiesDestroyed;
        WakeDynamicBodies();
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
        // A moved or changed support may no longer have a contact through
        // which wake propagation could reach its former neighbours.
        (void)WakeBody(handle);
        WakeDynamicBodies();
        ++m_Diagnostics.DescriptorUpdates;
        return true;
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

        for (Slot& slot : m_Slots)
        {
            if (slot.Occupied)
                IntegrateBody(slot.Body, input, diagnostics);
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
                                            body.Pose,
                                            body.Shapes[shapeIndex],
                                            shapeIndex,
                                            result.Diagnostics))
                {
                    shapes.push_back(*shape);
                }
            }
        }

        Extrinsic::Core::Memory::LinearArena scratch(64 * 1024);
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
                // Contact records own values; no EPA allocation escapes a pair.
                // Avoid a debug-fill reset after allocation-free analytic queries.
                if (scratch.Used() > 0)
                    scratch.Reset();
                DispatchContact(a, b, result, scratch);
            }
        }

        return result;
    }

    [[nodiscard]] ValidationStatus Validate(const SolverSettings& settings) noexcept
    {
        if (settings.MaxIterations == 0u ||
            !std::isfinite(settings.Restitution) || settings.Restitution < 0.0f || settings.Restitution > 1.0f ||
            !std::isfinite(settings.PenetrationSlop) || settings.PenetrationSlop < 0.0f ||
            !std::isfinite(settings.PositionCorrectionPercent) ||
            settings.PositionCorrectionPercent < 0.0f || settings.PositionCorrectionPercent > 1.0f ||
            !std::isfinite(settings.SleepLinearVelocityThreshold) || settings.SleepLinearVelocityThreshold < 0.0f ||
            !std::isfinite(settings.SleepAngularVelocityThreshold) || settings.SleepAngularVelocityThreshold < 0.0f ||
            !std::isfinite(settings.TimeToSleepSeconds) || settings.TimeToSleepSeconds < 0.0f)
        {
            return ValidationStatus::InvalidSolverSettings;
        }
        return ValidationStatus::Valid;
    }

    [[nodiscard]] IslandBuildResult World::BuildIslands(const CollisionResult& collision) const
    {
        IslandBuildResult result{};

        // Union-find over slot indices, restricted to occupied dynamic
        // bodies. Static/kinematic endpoints anchor contacts but never join
        // (or merge) islands.
        std::vector<std::uint32_t> parent(m_Slots.size());
        for (std::uint32_t i = 0u; i < parent.size(); ++i)
            parent[i] = i;

        const auto findRoot = [&parent](std::uint32_t index) noexcept
        {
            while (parent[index] != index)
            {
                parent[index] = parent[parent[index]];
                index = parent[index];
            }
            return index;
        };

        const auto isLiveDynamic = [this](const BodyHandle handle) noexcept
        {
            const Slot* slot = ResolveSlot(handle);
            return slot != nullptr && slot->Body.Enabled && slot->Body.Motion == MotionType::Dynamic;
        };

        for (std::uint32_t contactIndex = 0u;
             contactIndex < collision.Contacts.size();
             ++contactIndex)
        {
            const ContactRecord& contact = collision.Contacts[contactIndex];
            ++result.Diagnostics.ContactsConsidered;
            if (contact.IsTrigger)
            {
                ++result.Diagnostics.TriggerContactsExcluded;
                continue;
            }

            const bool dynamicA = isLiveDynamic(contact.A.Body);
            const bool dynamicB = isLiveDynamic(contact.B.Body);
            if (!dynamicA && !dynamicB)
            {
                ++result.Diagnostics.NonDynamicContactsIgnored;
                continue;
            }
            if (dynamicA != dynamicB)
            {
                ++result.Diagnostics.StaticAnchoredContacts;
            }
            if (dynamicA && dynamicB)
            {
                const std::uint32_t rootA = findRoot(contact.A.Body.Index);
                const std::uint32_t rootB = findRoot(contact.B.Body.Index);
                if (rootA != rootB)
                    parent[rootB < rootA ? rootA : rootB] = rootB < rootA ? rootB : rootA;
            }
        }

        // Bucket dynamic bodies and qualifying contacts by island root. The
        // root choice above (smaller index wins) plus the index-ordered scans
        // below give the deterministic ordering contract.
        const std::uint32_t kNoIsland = static_cast<std::uint32_t>(m_Slots.size());
        std::vector<std::uint32_t> rootToIsland(m_Slots.size(), kNoIsland);
        for (std::uint32_t index = 0u; index < m_Slots.size(); ++index)
        {
            const Slot& slot = m_Slots[index];
            if (!slot.Occupied || !slot.Body.Enabled || slot.Body.Motion != MotionType::Dynamic)
                continue;

            const std::uint32_t root = findRoot(index);
            if (rootToIsland[root] == kNoIsland)
            {
                rootToIsland[root] = static_cast<std::uint32_t>(result.Islands.size());
                result.Islands.emplace_back();
            }
            result.Islands[rootToIsland[root]].Bodies.push_back(
                BodyHandle{index, slot.Generation});
            ++result.Diagnostics.IslandedDynamicBodies;
        }

        for (std::uint32_t contactIndex = 0u;
             contactIndex < collision.Contacts.size();
             ++contactIndex)
        {
            const ContactRecord& contact = collision.Contacts[contactIndex];
            if (contact.IsTrigger)
                continue;
            const bool dynamicA = isLiveDynamic(contact.A.Body);
            const bool dynamicB = isLiveDynamic(contact.B.Body);
            if (!dynamicA && !dynamicB)
                continue;
            const std::uint32_t anchorIndex =
                dynamicA ? contact.A.Body.Index : contact.B.Body.Index;
            const std::uint32_t island = rootToIsland[findRoot(anchorIndex)];
            if (island < result.Islands.size())
                result.Islands[island].ContactIndices.push_back(contactIndex);
        }

        result.Diagnostics.IslandCount = static_cast<std::uint32_t>(result.Islands.size());
        return result;
    }

    [[nodiscard]] bool World::IsBodyAsleep(BodyHandle handle) const noexcept
    {
        const Slot* slot = ResolveSlot(handle);
        return slot != nullptr && slot->Sleep.Asleep;
    }

    bool World::WakeBody(BodyHandle handle) noexcept
    {
        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr)
            return false;
        slot->Sleep = {};
        return true;
    }

    void World::WakeDynamicBodies() noexcept
    {
        for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
        {
            const Slot& slot = m_Slots[index];
            if (slot.Occupied && slot.Body.Motion == MotionType::Dynamic)
                (void)WakeBody(BodyHandle{index, slot.Generation});
        }
    }

    [[nodiscard]] SolveStepDiagnostics World::SolveStep(const StepInput& input,
                                                        const SolverSettings& settings)
    {
        SolveStepDiagnostics diagnostics{};
        diagnostics.Status = Validate(input);
        if (diagnostics.Status == ValidationStatus::Valid)
            diagnostics.Status = Validate(settings);
        if (diagnostics.Status != ValidationStatus::Valid)
        {
            m_Diagnostics.LastSolveStep = diagnostics;
            return diagnostics;
        }

        const float dt = input.DeltaSeconds;
        const bool gravityChanged = m_LastSolveGravity && *m_LastSolveGravity != input.Gravity;
        m_LastSolveGravity = input.Gravity;
        if (!settings.EnableSleep || gravityChanged)
        {
            for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
            {
                const Slot& slot = m_Slots[index];
                if (slot.Occupied && slot.Body.Motion == MotionType::Dynamic && slot.Sleep.Asleep)
                {
                    (void)WakeBody(BodyHandle{index, slot.Generation});
                    ++diagnostics.Sleep.WakeTransitions;
                }
            }
        }


        // Linear kinetic energy of awake dynamic bodies. The world models no
        // angular inertia, so angular energy is intentionally excluded.
        const auto kineticEnergy = [this]() noexcept
        {
            float energy = 0.0f;
            for (const Slot& slot : m_Slots)
            {
                if (!slot.Occupied || slot.Sleep.Asleep)
                    continue;
                const BodyDescriptor& body = slot.Body;
                if (!body.Enabled || body.Motion != MotionType::Dynamic || body.Mass <= 0.0f)
                    continue;
                energy += 0.5f * body.Mass * glm::dot(body.LinearVelocity, body.LinearVelocity);
            }
            return energy;
        };
        diagnostics.KineticEnergyBefore = kineticEnergy();

        // Integration publishes each body only after all derived state is finite.
        for (Slot& slot : m_Slots)
        {
            if (!slot.Occupied)
                continue;
            if (slot.Sleep.Asleep && slot.Body.Enabled)
                ++diagnostics.Integration.BodiesVisited;
            else
                IntegrateBody(slot.Body, input, diagnostics.Integration);
        }

        const CollisionResult collision = ComputeCollisionContacts();
        const IslandBuildResult islands = BuildIslands(collision);
        diagnostics.Islands = islands.Diagnostics;
        std::vector<bool> attemptedBodies(m_Slots.size(), false);
        // Integration is complete. The solver translates bodies but holds each
        // manifold normal fixed, so its remaining depth follows this displacement.
        std::vector<glm::vec3> initialPositions(m_Slots.size());
        for (std::size_t i = 0; i < m_Slots.size(); ++i)
            initialPositions[i] = m_Slots[i].Body.Pose.Position;

        // ── Wake propagation: any awake member wakes the whole island ─────
        for (const IslandRecord& island : islands.Islands)
        {
            bool anyAwake = false;
            for (const BodyHandle handle : island.Bodies)
            {
                const Slot* slot = ResolveSlot(handle);
                if (slot != nullptr && !slot->Sleep.Asleep)
                {
                    anyAwake = true;
                    break;
                }
            }
            if (!anyAwake)
            {
                for (const std::uint32_t contactIndex : island.ContactIndices)
                {
                    const ContactRecord& contact = collision.Contacts[contactIndex];
                    for (const BodyHandle handle : {contact.A.Body, contact.B.Body})
                    {
                        const Slot* anchor = ResolveSlot(handle);
                        if (anchor != nullptr && anchor->Body.Motion != MotionType::Dynamic &&
                            (glm::length(anchor->Body.LinearVelocity) > settings.SleepLinearVelocityThreshold ||
                             glm::length(anchor->Body.AngularVelocity) > settings.SleepAngularVelocityThreshold))
                            anyAwake = true;
                    }
                    if (anyAwake)
                        break;
                }
            }
            if (!anyAwake)
                continue;
            for (const BodyHandle handle : island.Bodies)
            {
                Slot* slot = ResolveSlot(handle);
                if (slot != nullptr && slot->Sleep.Asleep)
                {
                    (void)WakeBody(handle);
                    ++diagnostics.Sleep.WakeTransitions;
                }
            }
        }

        // ── Iterative linear contact solve per awake island ───────────────
        const auto inverseMass = [](const BodyDescriptor& body) noexcept
        {
            if (body.Motion != MotionType::Dynamic || body.Mass <= 0.0f)
                return 0.0f;
            return 1.0f / body.Mass;
        };

        bool degraded = diagnostics.Integration.Status != ValidationStatus::Valid;
        for (const IslandRecord& island : islands.Islands)
        {
            if (island.ContactIndices.empty())
                continue;

            bool islandAsleep = true;
            for (const BodyHandle handle : island.Bodies)
            {
                const Slot* slot = ResolveSlot(handle);
                if (slot != nullptr && !slot->Sleep.Asleep)
                {
                    islandAsleep = false;
                    break;
                }
            }
            if (islandAsleep)
                continue;
            for (const BodyHandle handle : island.Bodies)
                attemptedBodies[handle.Index] = true;
            for (const std::uint32_t contactIndex : island.ContactIndices)
                diagnostics.MaxPenetrationBefore = std::max(
                    diagnostics.MaxPenetrationBefore, collision.Contacts[contactIndex].PenetrationDepth);

            bool islandFailed = false;
            std::uint32_t iterationsUsed = 0u;
            for (std::uint32_t iteration = 0u; iteration < settings.MaxIterations; ++iteration)
            {
                bool anyApplied = false;
                ++iterationsUsed;
                for (const std::uint32_t contactIndex : island.ContactIndices)
                {
                    const ContactRecord& contact = collision.Contacts[contactIndex];
                    Slot* slotA = ResolveSlot(contact.A.Body);
                    Slot* slotB = ResolveSlot(contact.B.Body);
                    if (slotA == nullptr || slotB == nullptr)
                        continue;
                    BodyDescriptor& a = slotA->Body;
                    BodyDescriptor& b = slotB->Body;
                    const float invA = inverseMass(a);
                    const float invB = inverseMass(b);
                    const float invSum = invA + invB;
                    const float normalSpeed = glm::dot(b.LinearVelocity - a.LinearVelocity, contact.Normal);
                    const float remainingDepth = contact.PenetrationDepth - glm::dot(
                        (b.Pose.Position - initialPositions[contact.B.Body.Index]) -
                        (a.Pose.Position - initialPositions[contact.A.Body.Index]), contact.Normal);
                    if (!std::isfinite(invA) || !std::isfinite(invB) || !std::isfinite(invSum) ||
                        !std::isfinite(normalSpeed) || !std::isfinite(remainingDepth))
                    {
                        islandFailed = true;
                        break;
                    }
                    if (invSum <= 0.0f)
                        continue;

                    glm::vec3 velocityA = a.LinearVelocity;
                    glm::vec3 velocityB = b.LinearVelocity;
                    glm::vec3 positionA = a.Pose.Position;
                    glm::vec3 positionB = b.Pose.Position;
                    if (normalSpeed < 0.0f)
                    {
                        const float impulse = (-(1.0f + settings.Restitution) * normalSpeed) / invSum;
                        velocityA -= contact.Normal * (impulse * invA);
                        velocityB += contact.Normal * (impulse * invB);
                        anyApplied = true;
                    }
                    const float correctionDepth = std::max(remainingDepth - settings.PenetrationSlop, 0.0f);
                    if (correctionDepth > 0.0f)
                    {
                        const glm::vec3 correction = contact.Normal *
                            ((settings.PositionCorrectionPercent * correctionDepth) / invSum);
                        positionA -= correction * invA;
                        positionB += correction * invB;
                        anyApplied = true;
                    }
                    if (!IsFinite(positionA) || !IsFinite(positionB) ||
                        !IsFinite(velocityA) || !IsFinite(velocityB))
                    {
                        islandFailed = true;
                        break;
                    }
                    // A failed trial never publishes half of a contact update.
                    a.Pose.Position = positionA;
                    b.Pose.Position = positionB;
                    a.LinearVelocity = velocityA;
                    b.LinearVelocity = velocityB;
                    ++diagnostics.ContactsSolved;
                }
                if (!anyApplied || islandFailed)
                    break;
            }
            degraded = degraded || islandFailed;
            diagnostics.IterationsUsed = std::max(diagnostics.IterationsUsed, iterationsUsed);
        }

        // Residuals cover the same solvable contacts as island construction.
        const CollisionResult residual = ComputeCollisionContacts();
        const IslandBuildResult residualIslands = BuildIslands(residual);
        const float convergenceTolerance = settings.PenetrationSlop + std::max(settings.PenetrationSlop, 1.0e-6f);
        for (const IslandRecord& island : residualIslands.Islands)
        {
            bool nonConverged = false;
            for (const std::uint32_t contactIndex : island.ContactIndices)
            {
                const ContactRecord& contact = residual.Contacts[contactIndex];
                if (!attemptedBodies[contact.A.Body.Index] && !attemptedBodies[contact.B.Body.Index])
                    continue;
                diagnostics.MaxPenetrationAfter = std::max(diagnostics.MaxPenetrationAfter, contact.PenetrationDepth);
                nonConverged = nonConverged || contact.PenetrationDepth > convergenceTolerance;
                const Slot* slotA = ResolveSlot(contact.A.Body);
                const Slot* slotB = ResolveSlot(contact.B.Body);
                if (slotA == nullptr || slotB == nullptr)
                    continue;
                const float normalSpeed = glm::dot(
                    slotB->Body.LinearVelocity - slotA->Body.LinearVelocity, contact.Normal);
                if (!std::isfinite(normalSpeed))
                    degraded = true;
                else
                    diagnostics.MaxNormalVelocityResidual = std::max(
                        diagnostics.MaxNormalVelocityResidual, std::max(0.0f, -normalSpeed));
            }
            if (nonConverged)
                ++diagnostics.NonConvergedIslands;
        }
        if (degraded)
            diagnostics.Solve = SolveStatus::Degraded;
        else if (diagnostics.NonConvergedIslands > 0u)
            diagnostics.Solve = SolveStatus::MaxIterationsReached;

        // ── Sleep policy ──────────────────────────────────────────────────
        for (Slot& slot : m_Slots)
        {
            if (!slot.Occupied)
                continue;
            const BodyDescriptor& body = slot.Body;
            if (!body.Enabled || body.Motion != MotionType::Dynamic)
                continue;

            if (slot.Sleep.Asleep)
            {
                ++diagnostics.Sleep.SleepingDynamicBodies;
                continue;
            }

            if (settings.EnableSleep &&
                glm::length(body.LinearVelocity) <= settings.SleepLinearVelocityThreshold &&
                glm::length(body.AngularVelocity) <= settings.SleepAngularVelocityThreshold)
            {
                slot.Sleep.LowMotionSeconds += dt;
                if (slot.Sleep.LowMotionSeconds >= settings.TimeToSleepSeconds)
                {
                    slot.Sleep.Asleep = true;
                    slot.Body.LinearVelocity = glm::vec3{0.0f};
                    slot.Body.AngularVelocity = glm::vec3{0.0f};
                    ++diagnostics.Sleep.SleepTransitions;
                    ++diagnostics.Sleep.SleepingDynamicBodies;
                    continue;
                }
            }
            else
            {
                slot.Sleep.LowMotionSeconds = 0.0f;
            }
            ++diagnostics.Sleep.AwakeDynamicBodies;
        }

        diagnostics.KineticEnergyAfter = kineticEnergy();
        diagnostics.EnergyDrift =
            diagnostics.KineticEnergyAfter - diagnostics.KineticEnergyBefore;

        ++m_Diagnostics.SolveStepsExecuted;
        m_Diagnostics.LastSolveStep = diagnostics;
        return diagnostics;
    }

    void World::Clear()
    {
        m_FreeList.resize(m_Slots.size());
        for (std::size_t i = 0; i < m_Slots.size(); ++i)
        {
            Slot& slot = m_Slots[i];
            if (slot.Occupied)
                slot.Generation = BumpGeneration(slot.Generation);
            slot.Occupied = false;
            slot.Body = {};
            slot.Sleep = {};
            // AddBody pops the back; recycle the lowest index first.
            m_FreeList[m_Slots.size() - 1 - i] = static_cast<std::uint32_t>(i);
        }
        m_Diagnostics.BodyCount = 0u;
        m_LastSolveGravity.reset();
    }
}
