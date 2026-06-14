#include <gtest/gtest.h>

#include <cstdint>
#include <array>
#include <cstddef>
#include <span>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
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
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.Types;
import Extrinsic.Core.Config.Render;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    Assets::AssetId MakeAssetId(std::uint32_t index, std::uint32_t generation = 1u)
    {
        return Assets::AssetId{index, generation};
    }

    RHI::TextureDesc AnyTextureDesc()
    {
        return RHI::TextureDesc{
            .Width = 4,
            .Height = 4,
            .MipLevels = 1,
            .Fmt = RHI::Format::RGBA8_UNORM,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
            .DebugName = "material-texture",
        };
    }

    RHI::SamplerDesc AnySamplerDesc()
    {
        return RHI::SamplerDesc{
            .MagFilter = RHI::FilterMode::Linear,
            .MinFilter = RHI::FilterMode::Linear,
            .MipFilter = RHI::MipmapMode::Nearest,
            .AddressU = RHI::AddressMode::ClampToEdge,
            .AddressV = RHI::AddressMode::ClampToEdge,
            .AddressW = RHI::AddressMode::ClampToEdge,
            .DebugName = "material-sampler",
        };
    }

    std::array<std::byte, 64> ZeroBytes64{};
}

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

TEST(GraphicsMaterialSystem, MaterialLayoutReloadCompatibilityIsDeterministic)
{
    const auto current = Graphics::GetCanonicalMaterialLayoutContract();
    EXPECT_TRUE(Graphics::IsMaterialLayoutReloadCompatible(current, current));
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, current),
              Graphics::MaterialLayoutReloadDecision::Compatible);

    auto reloaded = current;
    reloaded.Version += 1u;
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, reloaded),
              Graphics::MaterialLayoutReloadDecision::VersionMismatch);

    reloaded = current;
    reloaded.DefaultSlot += 1u;
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, reloaded),
              Graphics::MaterialLayoutReloadDecision::DefaultSlotMismatch);

    reloaded = current;
    reloaded.SlotSizeBytes += 16u;
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, reloaded),
              Graphics::MaterialLayoutReloadDecision::SlotSizeMismatch);

    reloaded = current;
    reloaded.CustomVec4SlotCount += 1u;
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, reloaded),
              Graphics::MaterialLayoutReloadDecision::CustomSlotCountMismatch);

    reloaded = current;
    reloaded.TextureBindingCount += 1u;
    EXPECT_EQ(Graphics::EvaluateMaterialLayoutReloadCompatibility(current, reloaded),
              Graphics::MaterialLayoutReloadDecision::TextureBindingCountMismatch);
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
    // StandardPBR + SciVis + DefaultDebugSurface + DefaultDebugUVs.
    EXPECT_EQ(diagnostics.RegisteredTypeCount, 4u);
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
    // StandardPBR + SciVis + DefaultDebugSurface + DefaultDebugUVs.
    EXPECT_EQ(diagnostics.RegisteredTypeCount, 4u);

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

TEST(GraphicsMaterialSystem, ResolvesReadyTextureAssetBindingsToBindlessMaterialParams)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::GpuAssetCache assets{buffers, textures, samplers, device.TransferQueue};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    const auto albedoId = MakeAssetId(101u);
    ASSERT_TRUE(assets.RequestUpload(Graphics::GpuTextureRequest{
        .Id = albedoId,
        .Bytes = std::span{ZeroBytes64},
        .Desc = AnyTextureDesc(),
        .SamplerDesc = AnySamplerDesc(),
    }).has_value());
    assets.Tick(0, 2);
    auto albedoView = assets.GetView(albedoId);
    ASSERT_TRUE(albedoView.has_value());

    const auto standard = materials.FindType("StandardPBR");
    auto material = materials.CreateInstance(standard, {});
    ASSERT_TRUE(material.IsValid());

    ASSERT_TRUE(materials.ResolveTextureAssetBindings(
        material.GetHandle(),
        Graphics::MaterialTextureAssetBindings{.Albedo = albedoId},
        assets).has_value());

    const Graphics::MaterialParams params = materials.GetParams(material.GetHandle());
    EXPECT_EQ(params.AlbedoID, albedoView->BindlessIdx);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveCount, 1u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetFallbackResolveCount, 0u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveFailureCount, 0u);

    material.Reset();
    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, GeneratedTextureAssetBindingsUseStandardMaterialSlots)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::GpuAssetCache assets{buffers, textures, samplers, device.TransferQueue};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    const auto generatedAlbedo = MakeAssetId(201u);
    const auto generatedNormal = MakeAssetId(202u);
    ASSERT_TRUE(assets.RequestUpload(Graphics::GpuTextureRequest{
        .Id = generatedAlbedo,
        .Bytes = std::span{ZeroBytes64},
        .Desc = AnyTextureDesc(),
        .SamplerDesc = AnySamplerDesc(),
    }).has_value());
    ASSERT_TRUE(assets.RequestUpload(Graphics::GpuTextureRequest{
        .Id = generatedNormal,
        .Bytes = std::span{ZeroBytes64},
        .Desc = AnyTextureDesc(),
        .SamplerDesc = AnySamplerDesc(),
    }).has_value());
    assets.Tick(0, 2);

    const auto albedoView = assets.GetView(generatedAlbedo);
    const auto normalView = assets.GetView(generatedNormal);
    ASSERT_TRUE(albedoView.has_value());
    ASSERT_TRUE(normalView.has_value());

    auto material = materials.CreateInstance(materials.FindType(Graphics::kMaterialTypeName_StandardPBR), {});
    ASSERT_TRUE(material.IsValid());

    ASSERT_TRUE(materials.ResolveTextureAssetBindings(
        material.GetHandle(),
        Graphics::MaterialTextureAssetBindings{
            .Albedo = generatedAlbedo,
            .Normal = generatedNormal,
        },
        assets).has_value());

    const Graphics::MaterialParams params = materials.GetParams(material.GetHandle());
    EXPECT_EQ(params.AlbedoID, albedoView->BindlessIdx);
    EXPECT_EQ(params.NormalID, normalView->BindlessIdx);
    EXPECT_EQ(params.MetallicRoughnessID, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(params.EmissiveID, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveCount, 2u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetFallbackResolveCount, 0u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveFailureCount, 0u);

    material.Reset();
    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, ResolvesMissingTextureAssetBindingsToFallback)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::GpuAssetCache assets{buffers, textures, samplers, device.TransferQueue};
    ASSERT_TRUE(assets.InitializeFallbackTexture(Graphics::GpuTextureFallbackDesc{
        .Bytes = std::span{ZeroBytes64},
        .Desc = AnyTextureDesc(),
        .SamplerDesc = AnySamplerDesc(),
    }).has_value());

    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);
    auto material = materials.CreateInstance(materials.FindType("StandardPBR"), {});
    ASSERT_TRUE(material.IsValid());

    const auto missingNormal = MakeAssetId(102u);
    ASSERT_TRUE(materials.ResolveTextureAssetBindings(
        material.GetHandle(),
        Graphics::MaterialTextureAssetBindings{.Normal = missingNormal},
        assets).has_value());

    const Graphics::MaterialParams params = materials.GetParams(material.GetHandle());
    EXPECT_NE(params.NormalID, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveCount, 1u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetFallbackResolveCount, 1u);
    EXPECT_EQ(assets.GetDiagnostics().FallbackHits, 1u);

    material.Reset();
    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, ReportsTextureAssetBindingFailuresWithoutFallback)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::GpuAssetCache assets{buffers, textures, samplers, device.TransferQueue};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);
    auto material = materials.CreateInstance(materials.FindType("StandardPBR"), {});
    ASSERT_TRUE(material.IsValid());

    const auto missingEmissive = MakeAssetId(103u);
    auto result = materials.ResolveTextureAssetBindings(
        material.GetHandle(),
        Graphics::MaterialTextureAssetBindings{.Emissive = missingEmissive},
        assets);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::ResourceNotFound);

    const Graphics::MaterialParams params = materials.GetParams(material.GetHandle());
    EXPECT_EQ(params.EmissiveID, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveCount, 1u);
    EXPECT_EQ(materials.GetDiagnostics().TextureAssetResolveFailureCount, 1u);
    EXPECT_EQ(assets.GetDiagnostics().FallbackMisses, 1u);

    result = materials.ResolveTextureAssetBindings(
        Graphics::MaterialHandle{},
        Graphics::MaterialTextureAssetBindings{.Albedo = missingEmissive},
        assets);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(materials.GetDiagnostics().InvalidTextureAssetBindingCount, 1u);

    material.Reset();
    materials.Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-031A — canonical missing-material fallback
// ---------------------------------------------------------------------------

TEST(GraphicsMaterialSystem, RegistersDefaultDebugSurfaceWithStableTypeId)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    const auto defaultDebugSurface =
        materials.FindType(Graphics::kMaterialTypeName_DefaultDebugSurface);
    ASSERT_TRUE(defaultDebugSurface.IsValid());
    EXPECT_EQ(defaultDebugSurface.Index, Graphics::kMaterialTypeID_DefaultDebugSurface);

    const auto standard = materials.FindType(Graphics::kMaterialTypeName_StandardPBR);
    ASSERT_TRUE(standard.IsValid());
    EXPECT_EQ(standard.Index, Graphics::kMaterialTypeID_StandardPBR);

    const auto sciVis = materials.FindType(Graphics::kMaterialTypeName_SciVis);
    ASSERT_TRUE(sciVis.IsValid());
    EXPECT_EQ(sciVis.Index, Graphics::kMaterialTypeID_SciVis);

    const auto debugUvs =
        materials.FindType(Graphics::kMaterialTypeName_DefaultDebugUVs);
    ASSERT_TRUE(debugUvs.IsValid());
    EXPECT_EQ(debugUvs.Index, Graphics::kMaterialTypeID_DefaultDebugUVs);

    materials.Shutdown();
}

TEST(GraphicsMaterialSystem, DefaultSlotCarriesDefaultDebugSurfaceParams)
{
    MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::MaterialSystem materials;
    materials.Initialize(device, buffers);

    const Graphics::MaterialHandle defaultHandle{Graphics::kDefaultMaterialSlotIndex, 0u};
    const Graphics::MaterialParams params = materials.GetParams(defaultHandle);
    EXPECT_FLOAT_EQ(params.BaseColorFactor.x, Graphics::kDefaultDebugSurfaceBaseColor[0]);
    EXPECT_FLOAT_EQ(params.BaseColorFactor.y, Graphics::kDefaultDebugSurfaceBaseColor[1]);
    EXPECT_FLOAT_EQ(params.BaseColorFactor.z, Graphics::kDefaultDebugSurfaceBaseColor[2]);
    EXPECT_FLOAT_EQ(params.BaseColorFactor.w, Graphics::kDefaultDebugSurfaceBaseColor[3]);
    EXPECT_TRUE(Graphics::HasFlag(params.Flags, Graphics::MaterialFlags::Unlit));

    materials.Shutdown();
}
