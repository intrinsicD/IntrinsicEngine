module;

#include <cstddef>
#include <memory>
#include <span>

module Extrinsic.Runtime.AssetModelTextureHandoff;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool IsTypeMismatch(const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::TypeMismatch
                || error == Core::ErrorCode::AssetTypeMismatch;
        }

        [[nodiscard]] Core::Expected<RHI::Format> ToGpuFormat(
            const Assets::AssetTexture2DMetadata& metadata)
        {
            using Assets::AssetTextureColorSpace;
            using Assets::AssetTexturePixelFormat;
            switch (metadata.PixelFormat)
            {
            case AssetTexturePixelFormat::R8Unorm:
                return RHI::Format::R8_UNORM;
            case AssetTexturePixelFormat::Rg8Unorm:
                return RHI::Format::RG8_UNORM;
            case AssetTexturePixelFormat::Rgba8Unorm:
                return metadata.ColorSpace == AssetTextureColorSpace::SRGB
                    ? RHI::Format::RGBA8_SRGB
                    : RHI::Format::RGBA8_UNORM;
            case AssetTexturePixelFormat::Rgb32Float:
                return RHI::Format::RGB32_FLOAT;
            case AssetTexturePixelFormat::Rgba32Float:
                return RHI::Format::RGBA32_FLOAT;
            case AssetTexturePixelFormat::Rgb8Unorm:
            case AssetTexturePixelFormat::Unknown:
                return Core::Err<RHI::Format>(Core::ErrorCode::AssetUnsupportedFormat);
            }
            return Core::Err<RHI::Format>(Core::ErrorCode::AssetUnsupportedFormat);
        }

        void RecordFailure(
            AssetModelTextureHandoffDiagnostics& diagnostics,
            const Assets::AssetId id,
            const Core::ErrorCode error)
        {
            diagnostics.LastFailedAsset = id;
            diagnostics.LastError = error;
            ++diagnostics.TextureUploadFailures;
            if (error == Core::ErrorCode::AssetUnsupportedFormat)
            {
                ++diagnostics.TextureUnsupportedFormat;
            }
            if (error == Core::ErrorCode::AssetInvalidData)
            {
                ++diagnostics.TextureInvalidPayloads;
            }
        }
    }

    Core::Expected<RHI::TextureDesc> BuildGpuTextureDesc(
        const Assets::AssetTexture2DPayload& payload)
    {
        if (auto valid = Assets::ValidateAssetTexture2DPayload(payload); !valid.has_value())
        {
            return Core::Err<RHI::TextureDesc>(valid.error());
        }

        auto format = ToGpuFormat(payload.Metadata);
        if (!format.has_value())
        {
            return Core::Err<RHI::TextureDesc>(format.error());
        }

        return RHI::TextureDesc{
            .Width = payload.Metadata.Width,
            .Height = payload.Metadata.Height,
            .DepthOrArrayLayers = 1u,
            .MipLevels = 1u,
            .Fmt = *format,
            .Dimension = RHI::TextureDimension::Tex2D,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .SampleCount = 1u,
            .DebugName = "Runtime.AssetTexture2D",
        };
    }

    Core::Result RequestTextureAssetUpload(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        const Assets::AssetId id,
        const AssetModelTextureHandoffOptions& options)
    {
        auto fail = [&cache, id, &options](const Core::ErrorCode error) -> Core::Result
        {
            if (options.NotifyCacheFailedOnUploadError && !IsTypeMismatch(error))
            {
                cache.NotifyFailed(id);
            }
            return Core::Err(error);
        };

        auto payloadSpan = service.Read<Assets::AssetTexture2DPayload>(id);
        if (!payloadSpan.has_value())
        {
            return fail(payloadSpan.error());
        }
        if (payloadSpan->size() != 1u)
        {
            return fail(Core::ErrorCode::AssetInvalidData);
        }

        const Assets::AssetTexture2DPayload& payload = (*payloadSpan)[0];
        auto desc = BuildGpuTextureDesc(payload);
        if (!desc.has_value())
        {
            return fail(desc.error());
        }

        auto upload = cache.RequestUpload(Graphics::GpuTextureRequest{
            .Id = id,
            .Bytes = std::span<const std::byte>(payload.PixelBytes.data(), payload.PixelBytes.size()),
            .Desc = *desc,
            .SamplerDesc = options.TextureSamplerDesc,
            .Sampler = {},
        });
        if (!upload.has_value())
        {
            return fail(upload.error());
        }

        return Core::Ok();
    }

    struct AssetModelTextureHandoff::Impl
    {
        Assets::AssetService& Service;
        Graphics::GpuAssetCache& Cache;
        AssetModelTextureHandoffOptions Options{};
        AssetModelTextureHandoffDiagnostics Diagnostics{};
        Assets::AssetEventBus::ListenerToken Token{Assets::AssetEventBus::InvalidToken};

        Impl(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            AssetModelTextureHandoffOptions options)
            : Service(service)
            , Cache(cache)
            , Options(options)
        {
            Token = Service.SubscribeAll(
                [this](const Assets::AssetId id, const Assets::AssetEvent event)
                {
                    Handle(id, event);
                });
        }

        ~Impl()
        {
            if (Token != Assets::AssetEventBus::InvalidToken)
            {
                Service.UnsubscribeAll(Token);
                Token = Assets::AssetEventBus::InvalidToken;
            }
        }

        [[nodiscard]] Core::Result UploadReadyTexture(const Assets::AssetId id)
        {
            auto result = RequestTextureAssetUpload(Service, Cache, id, Options);
            if (!result.has_value())
            {
                RecordFailure(Diagnostics, id, result.error());
                return result;
            }

            ++Diagnostics.TextureUploadRequests;
            Diagnostics.LastError = Core::ErrorCode::Success;
            return Core::Ok();
        }

        void HandleReady(const Assets::AssetId id)
        {
            ++Diagnostics.ReadyEventsObserved;

            auto texture = Service.Read<Assets::AssetTexture2DPayload>(id);
            if (texture.has_value())
            {
                ++Diagnostics.TextureReadyEvents;
                (void)UploadReadyTexture(id);
                return;
            }
            if (!IsTypeMismatch(texture.error()))
            {
                RecordFailure(Diagnostics, id, texture.error());
                if (Options.NotifyCacheFailedOnUploadError)
                {
                    Cache.NotifyFailed(id);
                }
                return;
            }

            auto model = Service.Read<Assets::AssetModelScenePayload>(id);
            if (model.has_value())
            {
                ++Diagnostics.ModelSceneReadyEvents;
                ++Diagnostics.ModelSceneDeferredEvents;
                return;
            }

            ++Diagnostics.NonModelTextureReadyEvents;
        }

        void Handle(const Assets::AssetId id, const Assets::AssetEvent event)
        {
            switch (event)
            {
            case Assets::AssetEvent::Ready:
                HandleReady(id);
                break;
            case Assets::AssetEvent::Failed:
                ++Diagnostics.FailedEventsObserved;
                break;
            case Assets::AssetEvent::Reloaded:
                ++Diagnostics.ReloadedEventsObserved;
                break;
            case Assets::AssetEvent::Destroyed:
                ++Diagnostics.DestroyedEventsObserved;
                break;
            }
        }
    };

    AssetModelTextureHandoff::AssetModelTextureHandoff(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        AssetModelTextureHandoffOptions options)
        : m_Impl(std::make_unique<Impl>(service, cache, options))
    {
    }

    AssetModelTextureHandoff::~AssetModelTextureHandoff() = default;

    bool AssetModelTextureHandoff::IsSubscribed() const noexcept
    {
        return m_Impl != nullptr
            && m_Impl->Token != Assets::AssetEventBus::InvalidToken;
    }

    AssetModelTextureHandoffDiagnostics
    AssetModelTextureHandoff::GetDiagnostics() const noexcept
    {
        return m_Impl != nullptr
            ? m_Impl->Diagnostics
            : AssetModelTextureHandoffDiagnostics{};
    }

    Core::Result AssetModelTextureHandoff::UploadReadyTexture(const Assets::AssetId id)
    {
        if (m_Impl == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return m_Impl->UploadReadyTexture(id);
    }
}
