#include <gtest/gtest.h>

#include <cstdint>

import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.Types;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

TEST(GraphicsMaterialSystem, CanonicalLayoutContractMatchesRhiSlot)
{
    constexpr auto layout = Graphics::GetCanonicalMaterialLayoutContract();

    EXPECT_EQ(layout.Version, Graphics::kMaterialLayoutVersion);
    EXPECT_EQ(layout.DefaultSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_EQ(layout.SlotSizeBytes, sizeof(RHI::GpuMaterialSlot));
    EXPECT_EQ(layout.CustomVec4SlotCount, Graphics::kMaterialCustomDataSlotCount);
    EXPECT_EQ(layout.TextureBindingCount, Graphics::kMaterialTextureBindingCount);
    EXPECT_EQ(static_cast<std::uint32_t>(Graphics::MaterialTextureSemantic::Albedo), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(Graphics::MaterialTextureSemantic::Emissive), 3u);
}

TEST(GraphicsMaterialSystem, DefaultAndStaleMaterialSlotsResolveToFallbackWithDiagnostics)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    EXPECT_EQ(materials.GetLayoutContract().DefaultSlot, Graphics::kDefaultMaterialSlotIndex);

    const auto standard = materials.FindType("StandardPBR");
    ASSERT_TRUE(standard.IsValid());

    Graphics::MaterialParams params{};
    params.BaseColorFactor = {0.25f, 0.5f, 0.75f, 1.0f};
    auto lease = materials.CreateInstance(standard, params);
    ASSERT_TRUE(lease.IsValid());

    const auto validSlot = materials.GetMaterialSlot(lease.GetHandle());
    EXPECT_NE(validSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_EQ(materials.GetMaterialSlot(Graphics::MaterialHandle{}), Graphics::kDefaultMaterialSlotIndex);

    const auto diagnostics = materials.GetDiagnostics();
    EXPECT_EQ(diagnostics.FallbackSlotResolveCount, 1u);
    EXPECT_EQ(diagnostics.LiveInstanceCount, 2u); // slot 0 default + one instance
    EXPECT_EQ(diagnostics.RegisteredTypeCount, 1u);
    EXPECT_GE(diagnostics.Capacity, 2u);

    lease.Reset();
    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, RejectsIncompatibleMaterialTypeLayoutsDeterministically)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    Graphics::MaterialTypeDesc tooManyParams{};
    tooManyParams.Name = "TooWide";
    tooManyParams.CustomParams.resize(Graphics::kMaterialCustomDataSlotCount + 1u);
    EXPECT_FALSE(materials.RegisterType(tooManyParams).IsValid());

    Graphics::MaterialTypeDesc duplicate{};
    duplicate.Name = "StandardPBR";
    EXPECT_FALSE(materials.RegisterType(duplicate).IsValid());

    EXPECT_FALSE(materials.CreateInstance(Graphics::MaterialTypeHandle{}).IsValid());

    const auto diagnostics = materials.GetDiagnostics();
    EXPECT_EQ(diagnostics.IncompatibleLayoutCount, 1u);
    EXPECT_EQ(diagnostics.DuplicateTypeNameCount, 1u);
    EXPECT_EQ(diagnostics.InvalidCreateTypeCount, 1u);
    EXPECT_EQ(diagnostics.RegisteredTypeCount, 1u);

    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, DirtyMaterialUpdatesAreCoalescedAndReported)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    const auto standard = materials.FindType("StandardPBR");
    ASSERT_TRUE(standard.IsValid());
    auto first = materials.CreateInstance(standard, {});
    auto second = materials.CreateInstance(standard, {});
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());

    EXPECT_EQ(materials.GetDiagnostics().DirtySlotCount, 3u); // default + two new slots

    materials.SyncGpuBuffer();

    auto diagnostics = materials.GetDiagnostics();
    EXPECT_EQ(diagnostics.DirtySlotCount, 0u);
    EXPECT_EQ(diagnostics.LastUploadRangeCount, 1u);
    EXPECT_EQ(diagnostics.LastUploadedSlotCount, 3u);
    ASSERT_FALSE(device.BufferWrites.empty());

    Graphics::MaterialParams changed{};
    changed.MetallicFactor = 0.9f;
    materials.SetParams(second.GetHandle(), changed);
    EXPECT_EQ(materials.GetDiagnostics().DirtySlotCount, 1u);

    materials.SyncGpuBuffer();
    diagnostics = materials.GetDiagnostics();
    EXPECT_EQ(diagnostics.LastUploadRangeCount, 1u);
    EXPECT_EQ(diagnostics.LastUploadedSlotCount, 1u);

    second.Reset();
    first.Reset();
    materials.Shutdown();
}


