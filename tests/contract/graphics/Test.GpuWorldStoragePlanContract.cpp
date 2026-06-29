#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Graphics.GpuWorld;

namespace
{
    using GpuWorld = Extrinsic::Graphics::GpuWorld;

    const std::array<glm::vec3, 3> kPositions{{
        {-0.5f, -0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    }};

    const std::array<glm::vec2, 3> kTexcoords{{
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.5f, 1.0f},
    }};

    const std::array<glm::vec3, 3> kNormals{{
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    }};

    const std::array<std::uint32_t, 3> kColors{{
        0xff0000ffu,
        0xff00ff00u,
        0xffff0000u,
    }};

    constexpr std::array<std::uint32_t, 3> kSurfaceIndices{{0u, 1u, 2u}};
    constexpr std::array<std::uint32_t, 2> kLineIndices{{0u, 1u}};

    [[nodiscard]] GpuWorld::GeometryUploadDesc StaticSoaSurfaceUpload()
    {
        return GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = {},
            .PositionBytes = std::as_bytes(std::span<const glm::vec3>{kPositions}),
            .TexcoordBytes = std::as_bytes(std::span<const glm::vec2>{kTexcoords}),
            .NormalBytes = std::as_bytes(std::span<const glm::vec3>{kNormals}),
            .SurfaceIndices = std::span<const std::uint32_t>{kSurfaceIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kPositions.size()),
            .LocalBounds = {},
            .DebugName = "storage-plan-static-surface",
            .PackedVertexColors = std::span<const std::uint32_t>{kColors},
        };
    }
} // namespace

TEST(GpuWorldStoragePlanContract, StaticSurfaceWithResolvedChannelsSelectsAosPlan)
{
    const auto plan = GpuWorld::PlanGeometryStorage(
        StaticSoaSurfaceUpload(), GpuWorld::GeometryStorageHint::StaticPreferInterleavedAoS);

    EXPECT_EQ(plan.Lane, GpuWorld::GeometryStorageLane::StaticInterleavedAoS);
    EXPECT_EQ(plan.Status, GpuWorld::GeometryStoragePlanStatus::SelectedStaticInterleavedAoS);
    EXPECT_TRUE(plan.EligibleForStaticAoS);
    EXPECT_TRUE(plan.RequiresPromotionOnStreamingEdit);
}

TEST(GpuWorldStoragePlanContract, DynamicHintKeepsUniformSoa)
{
    const auto plan = GpuWorld::PlanGeometryStorage(StaticSoaSurfaceUpload(),
                                                    GpuWorld::GeometryStorageHint::DynamicSoA);

    EXPECT_EQ(plan.Lane, GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_EQ(plan.Status, GpuWorld::GeometryStoragePlanStatus::DynamicHint);
    EXPECT_FALSE(plan.EligibleForStaticAoS);
    EXPECT_FALSE(plan.RequiresPromotionOnStreamingEdit);
}

TEST(GpuWorldStoragePlanContract, MissingSurfaceChannelsStayUniformSoa)
{
    auto upload = StaticSoaSurfaceUpload();
    upload.NormalBytes = {};
    const auto missingNormal = GpuWorld::PlanGeometryStorage(
        upload, GpuWorld::GeometryStorageHint::StaticPreferInterleavedAoS);

    EXPECT_EQ(missingNormal.Lane, GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_EQ(missingNormal.Status,
              GpuWorld::GeometryStoragePlanStatus::MissingStaticSurfaceChannels);

    auto lineOnly = StaticSoaSurfaceUpload();
    lineOnly.SurfaceIndices = {};
    lineOnly.LineIndices = std::span<const std::uint32_t>{kLineIndices};
    const auto linePlan = GpuWorld::PlanGeometryStorage(
        lineOnly, GpuWorld::GeometryStorageHint::StaticPreferInterleavedAoS);
    EXPECT_EQ(linePlan.Lane, GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_EQ(linePlan.Status, GpuWorld::GeometryStoragePlanStatus::MissingStaticSurfaceChannels);
}

TEST(GpuWorldStoragePlanContract, InvalidChannelSizesFailClosed)
{
    auto upload = StaticSoaSurfaceUpload();
    upload.NormalBytes = std::as_bytes(std::span<const glm::vec3>{kNormals.data(), 1u});

    const auto plan = GpuWorld::PlanGeometryStorage(
        upload, GpuWorld::GeometryStorageHint::StaticPreferInterleavedAoS);

    EXPECT_EQ(plan.Lane, GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_EQ(plan.Status, GpuWorld::GeometryStoragePlanStatus::InvalidInput);
    EXPECT_FALSE(plan.EligibleForStaticAoS);
}

TEST(GpuWorldStoragePlanContract, FirstStreamingEditPromotesStaticAosToSoa)
{
    const auto plan =
        GpuWorld::PlanGeometryStoragePromotion(GpuWorld::GeometryStorageLane::StaticInterleavedAoS,
                                               GpuWorld::GeometryChannelUpdateMask{.Normal = true});

    EXPECT_EQ(plan.Status, GpuWorld::GeometryStoragePromotionStatus::PromoteStreamingEditToSoA);
    EXPECT_EQ(plan.TargetLane, GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_TRUE(plan.StreamingChannels.Normal);
    EXPECT_TRUE(plan.RequiresPromotion);
    EXPECT_TRUE(plan.RequiresSoAConversion);
    EXPECT_TRUE(plan.RequiresFullReupload);
    EXPECT_TRUE(plan.RequiresInstanceRebind);
}

TEST(GpuWorldStoragePlanContract, UniformSoaOrNoStreamingEditDoesNotPromote)
{
    const auto alreadySoa =
        GpuWorld::PlanGeometryStoragePromotion(GpuWorld::GeometryStorageLane::UniformSoA,
                                               GpuWorld::GeometryChannelUpdateMask{.Normal = true});
    EXPECT_EQ(alreadySoa.Status, GpuWorld::GeometryStoragePromotionStatus::NotRequired);
    EXPECT_FALSE(alreadySoa.RequiresPromotion);

    const auto noEdit = GpuWorld::PlanGeometryStoragePromotion(
        GpuWorld::GeometryStorageLane::StaticInterleavedAoS, GpuWorld::GeometryChannelUpdateMask{});
    EXPECT_EQ(noEdit.Status, GpuWorld::GeometryStoragePromotionStatus::NotRequired);
    EXPECT_FALSE(noEdit.RequiresPromotion);
}
