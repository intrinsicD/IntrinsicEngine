module;

#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.AssetResidencyService;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Scene.Registry;
export import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
export import Extrinsic.Runtime.AssetModelSceneHandoff;
export import Extrinsic.Runtime.AssetModelTextureHandoff;
export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

namespace Extrinsic::Runtime
{
    export struct AssetResidencySceneHandoffOptions
    {
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
    };

    export class AssetResidencyService
    {
    public:
        AssetResidencyService() = default;
        ~AssetResidencyService();

        AssetResidencyService(const AssetResidencyService&) = delete;
        AssetResidencyService& operator=(const AssetResidencyService&) = delete;
        AssetResidencyService(AssetResidencyService&&) = delete;
        AssetResidencyService& operator=(AssetResidencyService&&) = delete;

        void InitializeGpuCache(Assets::AssetService& assets,
                                Graphics::IRenderer& renderer,
                                RHI::IDevice& device);
        void InitializeSceneHandoffs(
            Assets::AssetService& assets,
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            AssetResidencySceneHandoffOptions options = {});

        [[nodiscard]] Graphics::GpuAssetCache& Cache() noexcept;
        [[nodiscard]] const Graphics::GpuAssetCache& Cache() const noexcept;
        [[nodiscard]] Graphics::GpuAssetCache* CachePtr() noexcept;
        [[nodiscard]] const Graphics::GpuAssetCache* CachePtr() const noexcept;

        [[nodiscard]] AssetModelTextureHandoff* ModelTextureHandoff() noexcept;
        [[nodiscard]] const AssetModelTextureHandoff*
            ModelTextureHandoff() const noexcept;
        [[nodiscard]] AssetModelSceneHandoff* ModelSceneHandoff() noexcept;
        [[nodiscard]] const AssetModelSceneHandoff*
            ModelSceneHandoff() const noexcept;

        void TickAssets(Assets::AssetService& assets,
                        std::uint64_t currentFrame,
                        std::uint32_t framesInFlight);
        void DestroySceneBorrowers();
        void DestroyAssets(Assets::AssetService* assets);

    private:
        std::unique_ptr<Graphics::GpuAssetCache> m_GpuAssetCache{};
        Assets::AssetEventBus::ListenerToken m_GpuAssetCacheListener{
            Assets::AssetEventBus::InvalidToken};
        std::unique_ptr<AssetModelTextureHandoff> m_AssetModelTextureHandoff{};
        std::unique_ptr<AssetModelSceneHandoff> m_AssetModelSceneHandoff{};
    };
}
