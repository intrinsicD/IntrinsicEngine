module;

#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.AssetWorkflowTextureResidency;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Descriptors;

export namespace Extrinsic::Runtime
{
    struct AssetWorkflowTextureResidencyOptions
    {
        RHI::SamplerDesc TextureSamplerDesc{};
        bool NotifyCacheFailedOnUploadError{true};
    };

    struct AssetWorkflowTextureResidencyDiagnostics
    {
        std::uint64_t ReadyEventsObserved{0};
        std::uint64_t TextureReadyEvents{0};
        std::uint64_t TextureUploadRequests{0};
        std::uint64_t TextureUploadDeferrals{0};
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
        const AssetWorkflowTextureResidencyOptions& options = {});

    class AssetWorkflowTextureResidency
    {
    public:
        AssetWorkflowTextureResidency(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            AssetWorkflowTextureResidencyOptions options = {});
        ~AssetWorkflowTextureResidency();

        AssetWorkflowTextureResidency(const AssetWorkflowTextureResidency&) = delete;
        AssetWorkflowTextureResidency& operator=(const AssetWorkflowTextureResidency&) = delete;
        AssetWorkflowTextureResidency(AssetWorkflowTextureResidency&&) = delete;
        AssetWorkflowTextureResidency& operator=(AssetWorkflowTextureResidency&&) = delete;

        [[nodiscard]] bool IsSubscribed() const noexcept;
        [[nodiscard]] AssetWorkflowTextureResidencyDiagnostics GetDiagnostics() const noexcept;

        [[nodiscard]] Core::Result UploadReadyTexture(Assets::AssetId id);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
