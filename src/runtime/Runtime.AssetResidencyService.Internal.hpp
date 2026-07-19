#pragma once

// Include-only Engine module glue. Runtime.Engine.cpp is the sole include
// owner; include this only after its required asset, graphics, RHI, ECS, and
// runtime module imports.

namespace Extrinsic::Runtime
{
    struct AssetResidencySceneHandoffOptions
    {
        WorldHandle World{DefaultWorldHandle};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
    };

    class AssetResidencyService
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
