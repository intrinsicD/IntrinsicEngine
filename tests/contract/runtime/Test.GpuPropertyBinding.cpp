// GRAPHICS-155 / ADR 0030: runtime binds live entity properties to the ECS-blind residency.
// A property resolves to one canonical slot per CPU revision in its own type (a double
// property stays double), and an output ring is acquired in the same layout.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
#include "MockRHI.hpp"

namespace R = Extrinsic::Runtime;
namespace G = Extrinsic::Graphics;
namespace GS = Extrinsic::ECS::Components::GeometrySources;

namespace
{
    R::GeometryPropertyRef Ref(const char* name, Geometry::PropertyValueKind kind)
    {
        return {.Domain = R::GeometryElementDomain::PointCloudPoint, .Name = name, .ValueKind = kind};
    }
}

TEST(GpuPropertyBinding, ResolvesOnePropertyRevisionToOneTypedCanonicalSlot)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("binding");
    auto& scene = *worlds.Get(world);
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(3);
    props.GetOrAdd<glm::vec3>("v:position").Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    props.GetOrAdd<float>("weight").Vector() = {0.5f, 1.5f, 2.5f};
    props.GetOrAdd<double>("height").Vector() = {1.0, 2.0, 3.0};
    props.GetOrAdd<bool>("mask").Vector() = {true, false, true};

    Extrinsic::Tests::MockDevice device;
    device.TransferQueue.AcceptBufferUploads = true;
    G::GpuPropertyResidency residency(device);

    const auto weight = Ref("weight", Geometry::PropertyValueKind::Float);
    const auto first = R::ResolveGpuPropertyInput(residency, worlds, world, entity, weight);
    ASSERT_TRUE(first && first->Valid());
    EXPECT_EQ(first->Layout, (G::GpuPropertyLayout{.Scalar = G::GpuScalarType::Float32, .Channels = 1u, .Count = 3u}));
    EXPECT_EQ(first->Bytes, 12u);
    const auto again = R::ResolveGpuPropertyInput(residency, worlds, world, entity, weight);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->Buffer, first->Buffer);
    EXPECT_EQ(residency.Stats().Uploads, 1u) << "resolving twice uploads once";

    const auto height = Ref("height", Geometry::PropertyValueKind::Double);
    const auto asDouble = R::ResolveGpuPropertyInput(residency, worlds, world, entity, height);
    ASSERT_TRUE(asDouble);
    EXPECT_EQ(asDouble->Layout.Scalar, G::GpuScalarType::Float64) << "a double property stays double";
    EXPECT_EQ(asDouble->Bytes, 24u);
    ASSERT_EQ(device.TransferQueue.BufferUploads.size(), 2u);
    std::vector<double> uploaded(3);
    std::memcpy(uploaded.data(), device.TransferQueue.BufferUploads.back().Data.data(), 24u);
    EXPECT_EQ(uploaded, (std::vector<double>{1.0, 2.0, 3.0}));

    const auto positions = R::ResolveGpuPropertyInput(residency, worlds, world, entity, Ref("v:position", Geometry::PropertyValueKind::Vec3));
    ASSERT_TRUE(positions);
    EXPECT_EQ(positions->Layout.Channels, 3u);
    EXPECT_EQ(positions->Layout.ElementBytes(), 12u);
    EXPECT_EQ(positions->Bytes, 36u);
    // The spatial index requests the same positions through the shared layout constructor;
    // an explicit stride-12 float3 request is the same identity, so both share one upload.
    const auto positionsRef = Ref("v:position", Geometry::PropertyValueKind::Vec3);
    const auto shared = residency.AcquireInput(
        R::MakeGpuPropertyKey(world, entity, positionsRef), positions->Revision,
        G::GpuPropertyLayout{.Scalar = G::GpuScalarType::Float32, .Channels = 3u, .Stride = 12u, .Count = 3u},
        [](std::span<std::byte>) { ADD_FAILURE() << "a hit must not fill"; });
    ASSERT_TRUE(shared);
    EXPECT_EQ(shared->Buffer, positions->Buffer);
    EXPECT_EQ(shared->Layout, *R::MakeGpuPropertyLayout(Geometry::PropertyValueKind::Vec3, 3u));
    EXPECT_EQ(residency.Stats().Uploads, 3u);

    // A revision bump uploads once more; the same key serves the new revision.
    props.GetOrAdd<float>("weight")[1] = 9.0f;
    const auto bumped = R::ResolveGpuPropertyInput(residency, worlds, world, entity, weight);
    ASSERT_TRUE(bumped);
    EXPECT_NE(bumped->Revision, first->Revision);
    EXPECT_EQ(residency.Stats().Uploads, 4u);

    EXPECT_FALSE(R::ResolveGpuPropertyInput(residency, worlds, world, entity, Ref("mask", Geometry::PropertyValueKind::Bool)));
    EXPECT_FALSE(R::ResolveGpuPropertyInput(residency, worlds, world, entity, Ref("missing", Geometry::PropertyValueKind::Float)));
    EXPECT_FALSE(R::ResolveGpuPropertyInput(residency, worlds, world, entity, Ref("weight", Geometry::PropertyValueKind::Double))) << "the declared kind must match";
    EXPECT_FALSE(R::ResolveGpuPropertyInput(residency, worlds, world, entt::entity{12345u}, weight));
    EXPECT_FALSE(R::MakeGpuPropertyLayout(Geometry::PropertyValueKind::Bool, 3u));
    EXPECT_FALSE(R::MakeGpuPropertyLayout(Geometry::PropertyValueKind::Float, 0u));
    EXPECT_EQ(R::MakeGpuPropertyKey(world, entity, weight), R::MakeGpuPropertyKey(world, entity, weight));
    EXPECT_NE(R::MakeGpuPropertyKey(world, entity, weight), R::MakeGpuPropertyKey(world, entity, height));
}

TEST(GpuPropertyBinding, AcquiresAnOutputRingInThePropertysOwnLayout)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("binding");
    auto& scene = *worlds.Get(world);
    const auto entity = scene.Create();
    Extrinsic::Tests::MockDevice device;
    device.TransferQueue.AcceptBufferUploads = true;
    G::GpuPropertyResidency residency(device);
    const auto height = Ref("height", Geometry::PropertyValueKind::Double);
    const auto back = R::AcquireGpuPropertyOutput(residency, world, entity, height, 4u, 2u);
    ASSERT_TRUE(back && back->Valid());
    EXPECT_EQ(back->Layout.Scalar, G::GpuScalarType::Float64);
    EXPECT_EQ(back->Bytes, 32u);
    const auto key = R::MakeGpuPropertyKey(world, entity, height);
    EXPECT_TRUE(residency.HasRing(key));
    EXPECT_TRUE(residency.Publish(key));
    EXPECT_EQ(residency.Front(key)->Buffer, back->Buffer);
    EXPECT_FALSE(R::AcquireGpuPropertyOutput(residency, world, entity, Ref("mask", Geometry::PropertyValueKind::Bool), 4u, 2u));
}
