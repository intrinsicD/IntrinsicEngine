module;

#include <cstdint>
#include <memory>
#include <utility>

module Extrinsic.Runtime.Engine;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DeviceBootstrap;

namespace Extrinsic::Runtime
{
    AssetResidencyService::~AssetResidencyService() = default;

    void AssetResidencyService::InitializeGpuCache(
        Assets::AssetService& assets,
        Graphics::IRenderer& renderer,
        RHI::IDevice& device)
    {
        m_GpuAssetCache = std::make_unique<Graphics::GpuAssetCache>(
            renderer.GetBufferManager(),
            renderer.GetTextureManager(),
            renderer.GetSamplerManager(),
            device.GetTransferQueue());

        if (Core::Result fallback =
                InitializeRuntimeGpuAssetFallbackTexture(*m_GpuAssetCache, device);
            !fallback.has_value())
        {
            Core::Log::Warn(
                "[Runtime] GpuAssetCache fallback texture bootstrap failed: error={}; material code will use factor-only fallback.",
                static_cast<int>(fallback.error()));
        }

        m_GpuAssetCacheListener = assets.SubscribeAll(
            [cache = m_GpuAssetCache.get()](Assets::AssetId id,
                                            Assets::AssetEvent ev)
            {
                switch (ev)
                {
                case Assets::AssetEvent::Failed:    cache->NotifyFailed(id);    break;
                case Assets::AssetEvent::Reloaded:  cache->NotifyReloaded(id);  break;
                case Assets::AssetEvent::Destroyed: cache->NotifyDestroyed(id); break;
                case Assets::AssetEvent::Ready:
                    // Type-specific handoffs drive RequestUpload.
                    break;
                }
            });
    }

    void AssetResidencyService::InitializeSceneHandoffs(
        Assets::AssetService& assets,
        ECS::Scene::Registry& scene,
        Graphics::IRenderer& renderer,
        AssetResidencySceneHandoffOptions options)
    {
        m_AssetModelTextureHandoff =
            std::make_unique<AssetModelTextureHandoff>(assets, Cache());

        AssetModelSceneHandoffOptions modelSceneOptions{};
        modelSceneOptions.ObjectSpaceNormalBakeQueue =
            options.ObjectSpaceNormalBakeQueue;
        modelSceneOptions.ObjectSpaceNormalBakeGraphicsBackendOperational =
            options.ObjectSpaceNormalBakeGraphicsBackendOperational;

        m_AssetModelSceneHandoff =
            std::make_unique<AssetModelSceneHandoff>(
                assets,
                Cache(),
                scene,
                renderer,
                std::move(modelSceneOptions));
    }

    Graphics::GpuAssetCache& AssetResidencyService::Cache() noexcept
    {
        return *m_GpuAssetCache;
    }

    const Graphics::GpuAssetCache& AssetResidencyService::Cache() const noexcept
    {
        return *m_GpuAssetCache;
    }

    Graphics::GpuAssetCache* AssetResidencyService::CachePtr() noexcept
    {
        return m_GpuAssetCache.get();
    }

    const Graphics::GpuAssetCache*
    AssetResidencyService::CachePtr() const noexcept
    {
        return m_GpuAssetCache.get();
    }

    AssetModelTextureHandoff*
    AssetResidencyService::ModelTextureHandoff() noexcept
    {
        return m_AssetModelTextureHandoff.get();
    }

    const AssetModelTextureHandoff*
    AssetResidencyService::ModelTextureHandoff() const noexcept
    {
        return m_AssetModelTextureHandoff.get();
    }

    AssetModelSceneHandoff*
    AssetResidencyService::ModelSceneHandoff() noexcept
    {
        return m_AssetModelSceneHandoff.get();
    }

    const AssetModelSceneHandoff*
    AssetResidencyService::ModelSceneHandoff() const noexcept
    {
        return m_AssetModelSceneHandoff.get();
    }

    void AssetResidencyService::TickAssets(
        Assets::AssetService& assets,
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight)
    {
        assets.Tick();
        if (m_GpuAssetCache)
        {
            m_GpuAssetCache->Tick(currentFrame, framesInFlight);
        }
        if (m_AssetModelSceneHandoff)
        {
            static_cast<void>(
                m_AssetModelSceneHandoff->ResolvePendingMaterialTextureBindings());
        }
    }

    void AssetResidencyService::DestroySceneBorrowers()
    {
        m_AssetModelSceneHandoff.reset();
    }

    void AssetResidencyService::DestroyAssets(Assets::AssetService* assets)
    {
        DestroySceneBorrowers();
        m_AssetModelTextureHandoff.reset();
        if (assets != nullptr &&
            m_GpuAssetCacheListener != Assets::AssetEventBus::InvalidToken)
        {
            assets->UnsubscribeAll(m_GpuAssetCacheListener);
            m_GpuAssetCacheListener = Assets::AssetEventBus::InvalidToken;
        }
        m_GpuAssetCache.reset();
    }
}
