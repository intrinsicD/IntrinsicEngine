module;

#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.AssetModelTextureHandoff;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Descriptors;

export namespace Extrinsic::Runtime
{
    struct AssetModelTextureHandoffOptions
    {
        RHI::SamplerDesc TextureSamplerDesc{};
        bool NotifyCacheFailedOnUploadError{true};
    };

    struct AssetModelTextureHandoffDiagnostics
    {
        std::uint64_t ReadyEventsObserved{0};
        std::uint64_t TextureReadyEvents{0};
        std::uint64_t TextureUploadRequests{0};
        std::uint64_t TextureUploadFailures{0};
        std::uint64_t TextureUnsupportedFormat{0};
        std::uint64_t TextureInvalidPayloads{0};
        std::uint64_t ModelSceneReadyEvents{0};
        std::uint64_t ModelSceneDeferredEvents{0};
        std::uint64_t NonModelTextureReadyEvents{0};
        std::uint64_t FailedEventsObserved{0};
        std::uint64_t ReloadedEventsObserved{0};
        std::uint64_t DestroyedEventsObserved{0};
        Assets::AssetId LastFailedAsset{};
        Core::ErrorCode LastError{Core::ErrorCode::Success};
    };

    [[nodiscard]] Core::Expected<RHI::TextureDesc> BuildGpuTextureDesc(
        const Assets::AssetTexture2DPayload& payload);

    [[nodiscard]] Core::Result RequestTextureAssetUpload(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        Assets::AssetId id,
        const AssetModelTextureHandoffOptions& options = {});

    class AssetModelTextureHandoff
    {
    public:
        AssetModelTextureHandoff(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            AssetModelTextureHandoffOptions options = {});
        ~AssetModelTextureHandoff();

        AssetModelTextureHandoff(const AssetModelTextureHandoff&) = delete;
        AssetModelTextureHandoff& operator=(const AssetModelTextureHandoff&) = delete;
        AssetModelTextureHandoff(AssetModelTextureHandoff&&) = delete;
        AssetModelTextureHandoff& operator=(AssetModelTextureHandoff&&) = delete;

        [[nodiscard]] bool IsSubscribed() const noexcept;
        [[nodiscard]] AssetModelTextureHandoffDiagnostics GetDiagnostics() const noexcept;

        [[nodiscard]] Core::Result UploadReadyTexture(Assets::AssetId id);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
