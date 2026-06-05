#include <limits>
#include <type_traits>
#include <variant>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.RigidBody;

namespace Collider = Extrinsic::ECS::Components::Collider;
namespace RigidBody = Extrinsic::ECS::Components::RigidBody;

TEST(ECSColliderAuthoring, DescriptorsAreCpuOnlyValueTypes)
{
    static_assert(std::is_standard_layout_v<Collider::SphereShape>);
    static_assert(std::is_standard_layout_v<Collider::CapsuleShape>);
    static_assert(std::is_standard_layout_v<Collider::BoxShape>);
    static_assert(std::is_standard_layout_v<RigidBody::Component>);
}

TEST(ECSColliderAuthoring, DefaultColliderIsEmptyButEnabled)
{
    const Collider::Component collider{};

    EXPECT_TRUE(collider.Enabled);
    EXPECT_TRUE(collider.Shapes.empty());
    EXPECT_EQ(Collider::Validate(collider), Collider::ValidationStatus::EmptyShapeList);
}

TEST(ECSColliderAuthoring, SphereDescriptorStoresLocalPoseMaterialFilterAndTriggerDefaults)
{
    const Collider::LocalPose pose{
        .Position = glm::vec3{1.0f, 2.0f, 3.0f},
        .Rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
    };

    const Collider::ShapeDescriptor sphere = Collider::MakeSphere(2.0f, pose);

    ASSERT_TRUE(std::holds_alternative<Collider::SphereShape>(sphere.Shape));
    EXPECT_EQ(Collider::GetShapeKind(sphere), Collider::ShapeKind::Sphere);
    EXPECT_FLOAT_EQ(std::get<Collider::SphereShape>(sphere.Shape).Radius, 2.0f);
    EXPECT_FLOAT_EQ(sphere.Local.Position.x, 1.0f);
    EXPECT_FALSE(sphere.IsTrigger);
    EXPECT_TRUE(sphere.Enabled);
    EXPECT_EQ(sphere.Surface.Id, 0u);
    EXPECT_FLOAT_EQ(sphere.Surface.StaticFriction, 0.5f);
    EXPECT_FLOAT_EQ(sphere.Surface.DynamicFriction, 0.5f);
    EXPECT_FLOAT_EQ(sphere.Surface.Restitution, 0.0f);
    EXPECT_EQ(sphere.Filter.CategoryBits, 1u);
    EXPECT_EQ(sphere.Filter.CollidesWithBits, 0xFFFF'FFFFu);
    EXPECT_FLOAT_EQ(sphere.Offsets.ContactOffset, 0.02f);
    EXPECT_FLOAT_EQ(sphere.Offsets.RestOffset, 0.0f);
    EXPECT_EQ(Collider::Validate(sphere), Collider::ValidationStatus::Valid);
}

TEST(ECSColliderAuthoring, CapsuleAndBoxDescriptorsRepresentFirstPhasePrimitives)
{
    Collider::ShapeDescriptor capsule = Collider::MakeCapsule(0.25f, 1.5f);
    capsule.IsTrigger = true;
    ASSERT_TRUE(std::holds_alternative<Collider::CapsuleShape>(capsule.Shape));
    EXPECT_EQ(Collider::GetShapeKind(capsule), Collider::ShapeKind::Capsule);
    EXPECT_FLOAT_EQ(std::get<Collider::CapsuleShape>(capsule.Shape).Radius, 0.25f);
    EXPECT_FLOAT_EQ(std::get<Collider::CapsuleShape>(capsule.Shape).HalfHeight, 1.5f);
    EXPECT_TRUE(capsule.IsTrigger);
    EXPECT_EQ(Collider::Validate(capsule), Collider::ValidationStatus::Valid);

    const Collider::LocalPose boxPose{
        .Position = glm::vec3{-1.0f, 0.5f, 2.0f},
        .Rotation = glm::angleAxis(0.25f, glm::vec3{0.0f, 1.0f, 0.0f}),
    };
    const Collider::ShapeDescriptor box = Collider::MakeBox(glm::vec3{1.0f, 2.0f, 3.0f}, boxPose);
    ASSERT_TRUE(std::holds_alternative<Collider::BoxShape>(box.Shape));
    EXPECT_EQ(Collider::GetShapeKind(box), Collider::ShapeKind::Box);
    EXPECT_FLOAT_EQ(std::get<Collider::BoxShape>(box.Shape).HalfExtents.y, 2.0f);
    EXPECT_FLOAT_EQ(box.Local.Position.z, 2.0f);
    EXPECT_EQ(Collider::Validate(box), Collider::ValidationStatus::Valid);
}

TEST(ECSColliderAuthoring, CompoundColliderUsesExplicitChildShapeLocalPoses)
{
    Collider::Component collider{};
    collider.Shapes.push_back(Collider::MakeSphere(0.5f, Collider::LocalPose{.Position = glm::vec3{-1.0f, 0.0f, 0.0f}}));
    collider.Shapes.push_back(Collider::MakeBox(glm::vec3{0.25f, 0.5f, 0.75f},
                                                Collider::LocalPose{.Position = glm::vec3{1.0f, 0.0f, 0.0f}}));

    ASSERT_EQ(collider.Shapes.size(), 2u);
    EXPECT_FLOAT_EQ(collider.Shapes[0].Local.Position.x, -1.0f);
    EXPECT_FLOAT_EQ(collider.Shapes[1].Local.Position.x, 1.0f);
    EXPECT_EQ(Collider::Validate(collider), Collider::ValidationStatus::Valid);
}

TEST(ECSColliderAuthoring, InvalidShapeDiagnosticsRejectBadDescriptors)
{
    EXPECT_EQ(Collider::Validate(Collider::MakeSphere(0.0f)), Collider::ValidationStatus::InvalidSphereRadius);
    EXPECT_EQ(Collider::Validate(Collider::MakeCapsule(-1.0f, 1.0f)), Collider::ValidationStatus::InvalidCapsuleRadius);
    EXPECT_EQ(Collider::Validate(Collider::MakeCapsule(0.5f, 0.0f)), Collider::ValidationStatus::InvalidCapsuleHalfHeight);
    EXPECT_EQ(Collider::Validate(Collider::MakeBox(glm::vec3{1.0f, 0.0f, 1.0f})),
              Collider::ValidationStatus::InvalidBoxExtents);

    Collider::ShapeDescriptor invalidPose = Collider::MakeSphere(1.0f);
    invalidPose.Local.Position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(Collider::Validate(invalidPose), Collider::ValidationStatus::InvalidLocalPose);

    Collider::ShapeDescriptor invalidOffsets = Collider::MakeSphere(1.0f);
    invalidOffsets.Offsets.RestOffset = 0.05f;
    invalidOffsets.Offsets.ContactOffset = 0.01f;
    EXPECT_EQ(Collider::Validate(invalidOffsets), Collider::ValidationStatus::InvalidContactOffsets);

    Collider::ShapeDescriptor invalidMaterial = Collider::MakeSphere(1.0f);
    invalidMaterial.Surface.Restitution = 2.0f;
    EXPECT_EQ(Collider::Validate(invalidMaterial), Collider::ValidationStatus::InvalidMaterial);
}

TEST(ECSRigidBodyAuthoring, DefaultsDescribeExplicitStaticBodyIntent)
{
    const RigidBody::Component body{};

    EXPECT_EQ(body.Motion, RigidBody::MotionType::Static);
    EXPECT_EQ(body.Mass.Policy, RigidBody::MassPolicy::ExplicitMass);
    EXPECT_TRUE(body.ParticipatesInContacts);
    EXPECT_TRUE(body.Enabled);
    EXPECT_EQ(RigidBody::Validate(body), RigidBody::ValidationStatus::Valid);
}

TEST(ECSRigidBodyAuthoring, StaticKinematicAndDynamicHelpersSetMotionPolicies)
{
    const RigidBody::Component staticBody = RigidBody::MakeStatic();
    EXPECT_EQ(staticBody.Motion, RigidBody::MotionType::Static);
    EXPECT_EQ(staticBody.Mass.Policy, RigidBody::MassPolicy::Immovable);
    EXPECT_FLOAT_EQ(staticBody.GravityScale, 0.0f);
    EXPECT_EQ(RigidBody::Validate(staticBody), RigidBody::ValidationStatus::Valid);

    const RigidBody::Component kinematicBody = RigidBody::MakeKinematic();
    EXPECT_EQ(kinematicBody.Motion, RigidBody::MotionType::Kinematic);
    EXPECT_EQ(kinematicBody.Mass.Policy, RigidBody::MassPolicy::Immovable);
    EXPECT_FLOAT_EQ(kinematicBody.GravityScale, 0.0f);
    EXPECT_EQ(RigidBody::Validate(kinematicBody), RigidBody::ValidationStatus::Valid);

    const RigidBody::Component dynamicBody = RigidBody::MakeDynamic(4.0f);
    EXPECT_EQ(dynamicBody.Motion, RigidBody::MotionType::Dynamic);
    EXPECT_EQ(dynamicBody.Mass.Policy, RigidBody::MassPolicy::ExplicitMass);
    EXPECT_FLOAT_EQ(dynamicBody.Mass.Mass, 4.0f);
    EXPECT_EQ(RigidBody::Validate(dynamicBody), RigidBody::ValidationStatus::Valid);
}

TEST(ECSRigidBodyAuthoring, InvalidBodyDiagnosticsRejectBadMassAndState)
{
    RigidBody::Component dynamicBody = RigidBody::MakeDynamic();
    dynamicBody.Mass.Policy = RigidBody::MassPolicy::Immovable;
    EXPECT_EQ(RigidBody::Validate(dynamicBody), RigidBody::ValidationStatus::DynamicBodyIsImmovable);

    RigidBody::Component invalidMass = RigidBody::MakeDynamic(0.0f);
    EXPECT_EQ(RigidBody::Validate(invalidMass), RigidBody::ValidationStatus::InvalidMass);

    RigidBody::Component invalidDensity = RigidBody::MakeDynamic();
    invalidDensity.Mass.Policy = RigidBody::MassPolicy::Density;
    invalidDensity.Mass.Density = -1.0f;
    EXPECT_EQ(RigidBody::Validate(invalidDensity), RigidBody::ValidationStatus::InvalidDensity);

    RigidBody::Component invalidVelocity = RigidBody::MakeDynamic();
    invalidVelocity.LinearVelocity.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(RigidBody::Validate(invalidVelocity), RigidBody::ValidationStatus::InvalidVelocity);

    RigidBody::Component invalidDamping = RigidBody::MakeDynamic();
    invalidDamping.AngularDamping = -0.01f;
    EXPECT_EQ(RigidBody::Validate(invalidDamping), RigidBody::ValidationStatus::InvalidDamping);
}

TEST(ECSRigidBodyAuthoring, ComponentCombinationsAreClassifiedWithoutRuntimeState)
{
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(false, nullptr),
              RigidBody::AuthoringCombination::NoPhysicsAuthoring);
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(true, nullptr),
              RigidBody::AuthoringCombination::ColliderOnlyStatic);

    RigidBody::Component staticBody = RigidBody::MakeStatic();
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(true, &staticBody),
              RigidBody::AuthoringCombination::ExplicitStatic);

    RigidBody::Component kinematicBody = RigidBody::MakeKinematic();
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(true, &kinematicBody),
              RigidBody::AuthoringCombination::Kinematic);

    RigidBody::Component dynamicBody = RigidBody::MakeDynamic();
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(true, &dynamicBody),
              RigidBody::AuthoringCombination::Dynamic);

    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(false, &dynamicBody),
              RigidBody::AuthoringCombination::MissingColliderForContactingBody);

    dynamicBody.ParticipatesInContacts = false;
    EXPECT_EQ(RigidBody::ClassifyAuthoringCombination(false, &dynamicBody),
              RigidBody::AuthoringCombination::NonContactingBody);
}
