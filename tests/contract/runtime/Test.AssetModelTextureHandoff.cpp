#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.AssetModelTextureHandoff;

#include "MockRHI.hpp"

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace Graphics = Extrinsic::Graphics;
namespace RHI = Extrinsic::RHI;
namespace Runtime = Extrinsic::Runtime;
using Core::ErrorCode;
using Core::Expected;

namespace
{
    struct HandoffFixture
    {
        Extrinsic::Tests::MockDevice Device{};
        RHI::BufferManager BufferMgr;
        RHI::TextureManager TextureMgr;
        RHI::SamplerManager SamplerMgr;
        Extrinsic::Tests::MockTransferQueue Transfer{};
        Graphics::GpuAssetCache Cache;
        Assets::AssetService Service{};

        HandoffFixture()
            : BufferMgr(Device)
            , TextureMgr(Device, Device.Bindless)
            , SamplerMgr(Device)
            , Cache(BufferMgr, TextureMgr, SamplerMgr, Transfer)
        {
        }
    };

    struct TmpFile
    {
        std::filesystem::path Path;

        explicit TmpFile(std::string_view name)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << "asset-model-texture-handoff";
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    [[nodiscard]] Assets::AssetTexture2DPayload MakeTexturePayload(
        const Assets::AssetTexturePixelFormat pixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm,
        const Assets::AssetTextureColorSpace colorSpace = Assets::AssetTextureColorSpace::SRGB)
    {
        Assets::AssetTexture2DPayload payload{};
        payload.Metadata.Width = 2u;
        payload.Metadata.Height = 1u;
        payload.Metadata.Components = Assets::ComponentCountFor(pixelFormat);
        payload.Metadata.PixelFormat = pixelFormat;
        payload.Metadata.ColorSpace = colorSpace;
        payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::ExternalFile;
        payload.Metadata.SourceFormat = Assets::AssetFileFormat::PNG;
        payload.Metadata.SourcePath = "/textures/albedo.png";
        payload.Metadata.DebugName = "albedo";
        payload.PixelBytes.assign(
            static_cast<std::size_t>(payload.Metadata.Width)
                * static_cast<std::size_t>(payload.Metadata.Height)
                * static_cast<std::size_t>(Assets::BytesPerPixel(pixelFormat)),
            std::byte{0x7F});
        return payload;
    }

    [[nodiscard]] Assets::AssetModelScenePayload MakeModelScenePayload()
    {
        Assets::AssetModelScenePayload payload{};
        payload.SourcePath = "/models/scene.gltf";
        return payload;
    }

    [[nodiscard]] Expected<Assets::AssetId> LoadTexture(
        Assets::AssetService& service,
        std::string_view path,
        Assets::AssetTexture2DPayload payload)
    {
        return service.Load<Assets::AssetTexture2DPayload>(
            path,
            [payload = std::move(payload)](
                std::string_view,
                Assets::AssetId) -> Expected<Assets::AssetTexture2DPayload>
            {
                return payload;
            });
    }

    [[nodiscard]] Expected<Assets::AssetId> LoadModel(
        Assets::AssetService& service,
        std::string_view path,
        Assets::AssetModelScenePayload payload)
    {
        return service.Load<Assets::AssetModelScenePayload>(
            path,
            [payload = std::move(payload)](
                std::string_view,
                Assets::AssetId) -> Expected<Assets::AssetModelScenePayload>
            {
                return payload;
            });
    }

    void FlushAssetEvents(Assets::AssetService& service)
    {
        if (Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Tasks::Scheduler::WaitForAll();
        }
        service.Tick();
    }
}

TEST(RuntimeAssetModelTextureHandoff, BuildsGpuTextureDescFromPromotedTexturePayload)
{
    const Assets::AssetTexture2DPayload payload = MakeTexturePayload();

    auto desc = Runtime::BuildGpuTextureDesc(payload);
    ASSERT_TRUE(desc.has_value()) << static_cast<int>(desc.error());
    EXPECT_EQ(desc->Width, 2u);
    EXPECT_EQ(desc->Height, 1u);
    EXPECT_EQ(desc->DepthOrArrayLayers, 1u);
    EXPECT_EQ(desc->MipLevels, 1u);
    EXPECT_EQ(desc->Fmt, RHI::Format::RGBA8_SRGB);
    EXPECT_EQ(desc->Dimension, RHI::TextureDimension::Tex2D);
    EXPECT_EQ(desc->Usage, RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst);
    EXPECT_EQ(desc->InitialLayout, RHI::TextureLayout::Undefined);
}

TEST(RuntimeAssetModelTextureHandoff, ReadyTextureEventRequestsGpuUpload)
{
    HandoffFixture fx;
    Runtime::AssetModelTextureHandoff handoff(fx.Service, fx.Cache);
    TmpFile file("asset_texture_handoff_ready.png");
    const Assets::AssetTexture2DPayload payload = MakeTexturePayload();

    auto id = LoadTexture(fx.Service, file.Path.string(), payload);
    ASSERT_TRUE(id.has_value()) << static_cast<int>(id.error());
    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::NotRequested);

    FlushAssetEvents(fx.Service);

    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::GpuUploading);
    ASSERT_EQ(fx.Transfer.TextureUploads.size(), 1u);
    EXPECT_EQ(fx.Transfer.TextureUploads[0].SizeBytes, payload.PixelBytes.size());

    const auto diagnostics = handoff.GetDiagnostics();
    EXPECT_EQ(diagnostics.ReadyEventsObserved, 1u);
    EXPECT_EQ(diagnostics.TextureReadyEvents, 1u);
    EXPECT_EQ(diagnostics.TextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.TextureUploadFailures, 0u);

    fx.Cache.Tick(0u, 2u);
    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::Ready);
    auto view = fx.Cache.GetView(*id);
    ASSERT_TRUE(view.has_value()) << static_cast<int>(view.error());
    EXPECT_EQ(view->Kind, Graphics::GpuAssetKind::Texture);
    EXPECT_TRUE(view->Texture.IsValid());
}

TEST(RuntimeAssetModelTextureHandoff, UnsupportedTextureFormatMarksCacheFailed)
{
    HandoffFixture fx;
    Runtime::AssetModelTextureHandoff handoff(fx.Service, fx.Cache);
    TmpFile file("asset_texture_handoff_rgb8.png");

    auto id = LoadTexture(
        fx.Service,
        file.Path.string(),
        MakeTexturePayload(Assets::AssetTexturePixelFormat::Rgb8Unorm));
    ASSERT_TRUE(id.has_value()) << static_cast<int>(id.error());

    FlushAssetEvents(fx.Service);

    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::Failed);
    EXPECT_TRUE(fx.Transfer.TextureUploads.empty());

    const auto diagnostics = handoff.GetDiagnostics();
    EXPECT_EQ(diagnostics.ReadyEventsObserved, 1u);
    EXPECT_EQ(diagnostics.TextureReadyEvents, 1u);
    EXPECT_EQ(diagnostics.TextureUploadRequests, 0u);
    EXPECT_EQ(diagnostics.TextureUploadFailures, 1u);
    EXPECT_EQ(diagnostics.TextureUnsupportedFormat, 1u);
    EXPECT_EQ(diagnostics.LastFailedAsset, *id);
    EXPECT_EQ(diagnostics.LastError, ErrorCode::AssetUnsupportedFormat);
}

TEST(RuntimeAssetModelTextureHandoff, ReadyModelSceneEventIsDeferredNotUploaded)
{
    HandoffFixture fx;
    Runtime::AssetModelTextureHandoff handoff(fx.Service, fx.Cache);
    TmpFile file("asset_model_handoff_deferred.gltf");

    auto id = LoadModel(fx.Service, file.Path.string(), MakeModelScenePayload());
    ASSERT_TRUE(id.has_value()) << static_cast<int>(id.error());

    FlushAssetEvents(fx.Service);

    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::NotRequested);
    EXPECT_TRUE(fx.Transfer.TextureUploads.empty());

    const auto diagnostics = handoff.GetDiagnostics();
    EXPECT_EQ(diagnostics.ReadyEventsObserved, 1u);
    EXPECT_EQ(diagnostics.TextureReadyEvents, 0u);
    EXPECT_EQ(diagnostics.ModelSceneReadyEvents, 1u);
    EXPECT_EQ(diagnostics.ModelSceneDeferredEvents, 1u);
    EXPECT_EQ(diagnostics.NonModelTextureReadyEvents, 0u);
}

TEST(RuntimeAssetModelTextureHandoff, DestructorUnsubscribesFromAssetEvents)
{
    HandoffFixture fx;
    TmpFile file("asset_texture_handoff_unsubscribed.png");
    {
        Runtime::AssetModelTextureHandoff handoff(fx.Service, fx.Cache);
        EXPECT_TRUE(handoff.IsSubscribed());
    }

    auto id = LoadTexture(fx.Service, file.Path.string(), MakeTexturePayload());
    ASSERT_TRUE(id.has_value()) << static_cast<int>(id.error());

    FlushAssetEvents(fx.Service);

    EXPECT_EQ(fx.Cache.GetState(*id), Graphics::GpuAssetState::NotRequested);
    EXPECT_TRUE(fx.Transfer.TextureUploads.empty());
}
