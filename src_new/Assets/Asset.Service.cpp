module;

#include <mutex>
#include <string>
#include <expected>

module Extrinsic.Asset.Service;

namespace Extrinsic::Assets
{
    AssetService::AssetService()
    {
        m_LoadPipeline.BindRegistry(&m_Registry);
        m_LoadPipeline.BindEventBus(&m_EventBus);
    }

    Core::Result AssetService::Reload(AssetId id)
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        auto toUnloaded = m_Registry.SetState(id, AssetState::Ready, AssetState::Unloaded);
        if (!toUnloaded.has_value())
        {
            return toUnloaded;
        }

        std::string path;
        {
            std::scoped_lock lock(m_PathMutex);
            const auto pathIt = m_PathById.find(id);
            if (pathIt == m_PathById.end())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            path = pathIt->second;
        }

        const auto meta = m_Registry.GetMeta(id);
        if (!meta.has_value())
        {
            return std::unexpected(meta.error());
        }

        LoadRequest req{.id = id, .typeId = meta->typeId, .path = path.c_str(), .needsGpuUpload = false};
        auto result = m_LoadPipeline.EnqueueIO(req);
        if (!result.has_value())
        {
            return result;
        }

        m_EventBus.Publish(id, AssetEvent::Reloaded);
        return Core::Ok();
    }

    Core::Result AssetService::Destroy(AssetId id)
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        std::string path;
        {
            std::scoped_lock lock(m_PathMutex);
            const auto pathIt = m_PathById.find(id);
            if (pathIt != m_PathById.end())
            {
                path = pathIt->second;
                m_PathById.erase(pathIt);
            }
        }

        if (!path.empty())
        {
            (void)m_PathIndex.Erase(path, id);
        }

        (void)m_PayloadStore.Retire(id);

        if (auto destroyed = m_Registry.Destroy(id); !destroyed.has_value())
        {
            return destroyed;
        }

        m_EventBus.Publish(id, AssetEvent::Destroyed);
        return Core::Ok();
    }

    void AssetService::Tick()
    {
        m_EventBus.Flush();
    }
}
