module;

#include <mutex>
#include <string>

module Extrinsic.Asset.Service;

namespace Extrinsic::Assets
{
    AssetService::AssetService()
    {
        m_LoadPipeline.BindRegistry(&m_Registry);
        m_LoadPipeline.BindEventBus(&m_EventBus);
    }

    Core::Expected<AssetMeta> AssetService::GetMeta(AssetId id) const
    {
        return m_Registry.GetMeta(id);
    }

    bool AssetService::IsAlive(AssetId id) const noexcept
    {
        return m_Registry.IsAlive(id);
    }

    Core::Expected<std::string> AssetService::GetPath(AssetId id) const
    {
        std::scoped_lock lock(m_PathMutex);
        const auto it = m_PathById.find(id);
        if (it == m_PathById.end())
        {
            return Core::Err<std::string>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    Core::Result AssetService::Destroy(AssetId id)
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        // Drop any in-flight load state so the pipeline does not leak a
        // LoadRequest when a previously queued asset is destroyed (B6).
        m_LoadPipeline.Cancel(id);

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
