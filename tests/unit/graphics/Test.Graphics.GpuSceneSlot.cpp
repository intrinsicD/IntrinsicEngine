#include <gtest/gtest.h>

#include <cstdint>
#include <string>

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.RHI.Handles;

using namespace Extrinsic;

namespace
{
    Assets::AssetId MakeAssetId(std::uint32_t index, std::uint32_t generation = 1u)
    {
        return Assets::AssetId{index, generation};
    }

    Graphics::GpuInstanceHandle MakeInstanceHandle(std::uint32_t index, std::uint32_t generation = 1u)
    {
        return Graphics::GpuInstanceHandle{index, generation};
    }

    Graphics::GpuGeometryHandle MakeGeometryHandle(std::uint32_t index, std::uint32_t generation = 1u)
    {
        return Graphics::GpuGeometryHandle{index, generation};
    }

    RHI::BufferHandle MakeBufferHandle(std::uint32_t index, std::uint32_t generation = 1u)
    {
        return RHI::BufferHandle{index, generation};
    }
}

TEST(GraphicsGpuSceneSlot, DefaultStateHasNoAssetOrResidency)
{
    Graphics::Components::GpuSceneSlot slot{};

    EXPECT_FALSE(slot.HasInstance());
    EXPECT_FALSE(slot.HasGeometry());
    EXPECT_FALSE(slot.IsRegistered());
    EXPECT_FALSE(slot.HasSourceAsset());
    EXPECT_FALSE(slot.SourceAsset.IsValid());
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);
    EXPECT_FALSE(slot.Find("positions").IsValid());
    EXPECT_EQ(slot.FindEntry("positions"), nullptr);
}

TEST(GraphicsGpuSceneSlot, TracksHandlesAndSourceAssetGeneration)
{
    Graphics::Components::GpuSceneSlot slot{};
    const auto instance = MakeInstanceHandle(7u, 3u);
    const auto geometry = MakeGeometryHandle(11u, 5u);
    const auto asset = MakeAssetId(19u, 2u);

    slot.SetInstanceHandle(instance);
    slot.SetGeometryHandle(geometry);
    slot.SetSourceAsset(asset, 41u);

    EXPECT_TRUE(slot.HasInstance());
    EXPECT_TRUE(slot.HasGeometry());
    EXPECT_TRUE(slot.IsRegistered());
    EXPECT_TRUE(slot.HasSourceAsset());
    EXPECT_EQ(slot.ToInstanceHandle(), instance);
    EXPECT_EQ(slot.ToGeometryHandle(), geometry);
    EXPECT_EQ(slot.SourceAsset, asset);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 41u);

    slot.UpdateLastSeenAssetGeneration(42u);
    EXPECT_EQ(slot.SourceAsset, asset);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 42u);
}

TEST(GraphicsGpuSceneSlot, ClearingSourceAssetPreservesResidencyAndBuffers)
{
    Graphics::Components::GpuSceneSlot slot{};
    const auto instance = MakeInstanceHandle(3u, 1u);
    const auto geometry = MakeGeometryHandle(4u, 2u);
    const auto asset = MakeAssetId(5u, 6u);
    const auto positions = MakeBufferHandle(9u, 10u);

    slot.SetInstanceHandle(instance);
    slot.SetGeometryHandle(geometry);
    slot.SetSourceAsset(asset, 77u);
    slot.Upsert("positions", positions, 123u, 12u);

    ASSERT_TRUE(slot.HasSourceAsset());
    ASSERT_EQ(slot.Find("positions"), positions);
    const auto* entry = slot.FindEntry("positions");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Name, std::string("positions"));
    EXPECT_EQ(entry->Handle, positions);
    EXPECT_EQ(entry->ElementCount, 123u);
    EXPECT_EQ(entry->Stride, 12u);

    slot.ClearSourceAsset();

    EXPECT_FALSE(slot.HasSourceAsset());
    EXPECT_FALSE(slot.SourceAsset.IsValid());
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);
    EXPECT_EQ(slot.ToInstanceHandle(), instance);
    EXPECT_EQ(slot.ToGeometryHandle(), geometry);
    EXPECT_EQ(slot.Find("positions"), positions);
}
