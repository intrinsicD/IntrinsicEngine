module;

#include <mutex>
#include <string>
#include <string_view>
#include <expected>
#include <type_traits>

module Extrinsic.Asset.Service;

namespace Extrinsic::Assets
{
    static_assert(!std::is_copy_constructible_v<AssetService>,
              "AssetService must be non-copyable (CLAUDE.md subsystem contract).");
    static_assert(!std::is_move_constructible_v<AssetService>,
                  "AssetService must be non-movable; loader thunks capture 'this'.");

    AssetService::AssetService()
    {
        m_LoadPipeline.BindRegistry(&m_Registry);
        m_LoadPipeline.BindEventBus(&m_EventBus);
    }

    [[nodiscard]] Core::Expected<AssetMeta> AssetService::GetMeta(AssetId id) const
    {
        return m_Registry.GetMeta(id);
    }

    [[nodiscard]] Core::Expected<std::string> AssetService::GetPath(AssetId id) const
    {
        std::scoped_lock lock(m_PathMutex);
        const auto it = m_PathById.find(id);
        if (it == m_PathById.end())
        {
            return Core::Err<std::string>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    [[nodiscard]] bool AssetService::IsAlive(AssetId id) const noexcept
    {
        return m_Registry.IsAlive(id);
    }

    Core::Expected<LoaderToken> AssetService::FindLoaderToken(AssetId id) const
    {
        std::scoped_lock lock(m_LoaderMutex);
        const auto it = m_LoaderByAsset.find(id);
        if (it == m_LoaderByAsset.end())
        {
            return Core::Err<LoaderToken>(Core::ErrorCode::AssetLoaderMissing);
        }
        return it->second;
    }

    void AssetService::EraseLoader(AssetId id)
    {
        LoaderToken token{};
        {
            std::scoped_lock lock(m_LoaderMutex);
            const auto it = m_LoaderByAsset.find(id);
            if (it == m_LoaderByAsset.end())
            {
                return;
            }
            token = it->second;
            m_LoaderByAsset.erase(it);
        }
        // Unregister outside m_LoaderMutex - the registry takes its own lock
        // and the thunk's destructor must not re-enter m_LoaderMutex.
        (void)m_LoaderRegistry.Unregister(token);
    }

    Core::Expected<LoaderToken> AssetService::GetReloadToken(AssetId id) const
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err<LoaderToken>(Core::ErrorCode::ResourceNotFound);
        }
        return FindLoaderToken(id);
    }

    Core::Result AssetService::Reload(AssetId id)
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        const auto meta = m_Registry.GetMeta(id);
        if (!meta.has_value())
        {
            return std::unexpected(meta.error());
        }

        if (meta->state != AssetState::Ready)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const auto tokenOr = FindLoaderToken(id);
        if (!tokenOr.has_value())
        {
            return std::unexpected(tokenOr.error());
        }
        const LoaderToken token = *tokenOr;

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

        // State machine: transition to Unloaded to reflect that the old
        // payload is about to be superseded.
        auto toUnloaded = m_Registry.SetState(id, AssetState::Ready, AssetState::Unloaded);
        if (!toUnloaded.has_value())
        {
            return toUnloaded;
        }

        // Invoke the captured loader while in Unloaded state. The thunk
        // publishes payload and updates payload slot only on success.
        //
        // If invocation fails we best-effort restore Ready so callers can
        // retry and retain previous behavior expectations for reload failure.
        auto invokeResult = m_LoaderRegistry.InvokeOr(
            token, Core::ErrorCode::AssetLoaderMissing, std::string_view(path));
        if (!invokeResult.has_value())
        {
            (void)m_Registry.SetState(id, AssetState::Unloaded, AssetState::Ready);
            return invokeResult;
        }

        LoadRequest req{.id = id, .typeId = meta->typeId, .path = path, .needsGpuUpload = false};
        auto result = m_LoadPipeline.EnqueueIO(std::move(req));
        if (!result.has_value())
        {
            (void)m_LoadPipeline.MarkFailed(id);
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

        EraseLoader(id);

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
