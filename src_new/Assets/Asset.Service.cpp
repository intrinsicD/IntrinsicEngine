module;

#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <expected>
#include <type_traits>
#include <unordered_map>
#include <functional>

module Extrinsic.Asset.Service;

import Extrinsic.Core.Hash;
import Extrinsic.Core.CallbackRegistry;
import Extrinsic.Core.StrongHandle;
import Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    // Internal callback registry using the same StrongHandle machinery.
    using InternalLoaderRegistry = Core::CallbackRegistry<Core::Result(std::string_view), AssetLoaderTag>;
    using InternalLoaderToken = InternalLoaderRegistry::Token;

    struct AssetService::Impl
    {
        AssetPathIndex pathIndex;
        AssetRegistry registry;
        AssetPayloadStore payloadStore;
        AssetLoadPipeline loadPipeline;
        AssetEventBus eventBus;

        mutable std::mutex pathMutex;
        std::unordered_map<AssetId, std::string, AssetIdHash> pathById;

        mutable std::mutex loaderMutex;
        std::unordered_map<AssetId, InternalLoaderToken, AssetIdHash> loaderByAsset;
        InternalLoaderRegistry loaderRegistry;

        Impl()
        {
            loadPipeline.BindRegistry(&registry);
            loadPipeline.BindEventBus(&eventBus);
        }

        [[nodiscard]] Core::Expected<InternalLoaderToken> FindLoaderToken(AssetId id) const
        {
            std::scoped_lock lock(loaderMutex);
            const auto it = loaderByAsset.find(id);
            if (it == loaderByAsset.end())
            {
                return Core::Err<InternalLoaderToken>(Core::ErrorCode::AssetLoaderMissing);
            }
            return it->second;
        }

        void EraseLoader(AssetId id)
        {
            InternalLoaderToken token{};
            {
                std::scoped_lock lock(loaderMutex);
                const auto it = loaderByAsset.find(id);
                if (it == loaderByAsset.end())
                {
                    return;
                }
                token = it->second;
                loaderByAsset.erase(it);
            }
            (void)loaderRegistry.Unregister(token);
        }
    };

    static_assert(!std::is_copy_constructible_v<AssetService>,
              "AssetService must be non-copyable (CLAUDE.md subsystem contract).");
    static_assert(!std::is_move_constructible_v<AssetService>,
                  "AssetService must be non-movable; loader thunks capture 'this'.");

    AssetService::AssetService()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetService::~AssetService() = default;

    AssetRegistry& AssetService::Registry() noexcept { return m_Impl->registry; }
    AssetEventBus& AssetService::EventBus() noexcept { return m_Impl->eventBus; }
    AssetPayloadStore& AssetService::PayloadStore() noexcept { return m_Impl->payloadStore; }
    const AssetPayloadStore& AssetService::PayloadStore() const noexcept { return m_Impl->payloadStore; }
    AssetLoadPipeline& AssetService::LoadPipeline() noexcept { return m_Impl->loadPipeline; }

    [[nodiscard]] Core::Expected<AssetMeta> AssetService::GetMeta(AssetId id) const
    {
        return m_Impl->registry.GetMeta(id);
    }

    [[nodiscard]] Core::Expected<std::string> AssetService::GetPath(AssetId id) const
    {
        std::scoped_lock lock(m_Impl->pathMutex);
        const auto it = m_Impl->pathById.find(id);
        if (it == m_Impl->pathById.end())
        {
            return Core::Err<std::string>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    [[nodiscard]] bool AssetService::IsAlive(AssetId id) const noexcept
    {
        return m_Impl->registry.IsAlive(id);
    }

    [[nodiscard]] Core::Expected<LoaderToken> AssetService::GetReloadToken(AssetId id) const
    {
        if (!m_Impl->registry.IsAlive(id))
        {
            return Core::Err<LoaderToken>(Core::ErrorCode::ResourceNotFound);
        }
        auto tokenOr = m_Impl->FindLoaderToken(id);
        if (!tokenOr.has_value())
        {
            return Core::Err<LoaderToken>(tokenOr.error());
        }
        return LoaderToken{tokenOr->Index, tokenOr->Generation};
    }

    Core::Expected<AssetId> AssetService::LoadErased(
        std::string_view path,
        uint32_t typeId,
        std::function<Core::Expected<PayloadTicket>(std::string_view, AssetId)> loader)
    {
        if (path.empty())
        {
            return Core::Err<AssetId>(Core::ErrorCode::InvalidArgument);
        }

        const auto abs = Core::Filesystem::GetAbsolutePath(std::string(path));

        if (auto found = m_Impl->pathIndex.Find(abs); found.has_value())
        {
            const auto meta = m_Impl->registry.GetMeta(*found);
            if (!meta.has_value())
            {
                return std::unexpected(meta.error());
            }
            if (meta->typeId != typeId)
            {
                return Core::Err<AssetId>(Core::ErrorCode::TypeMismatch);
            }
            return *found;
        }

        // Speculatively create the registry entry so the loader receives a
        // valid AssetId for PayloadStore::Publish().
        auto idResult = m_Impl->registry.Create(Core::Hash::HashString(abs), typeId);
        if (!idResult.has_value())
        {
            return idResult;
        }
        const AssetId id = *idResult;

        // Run the erased loader (decode + publish) with the real id.
        auto ticket = loader(std::string_view(abs), id);
        if (!ticket.has_value())
        {
            // Full unwind — no ghost registry entry.
            (void)m_Impl->registry.Destroy(id);
            return std::unexpected(ticket.error());
        }

        if (auto r = m_Impl->pathIndex.Insert(abs, id); !r.has_value())
        {
            (void)m_Impl->payloadStore.Retire(id);
            (void)m_Impl->registry.Destroy(id);
            if (auto winner = m_Impl->pathIndex.Find(abs); winner.has_value())
            {
                return *winner;
            }
            return std::unexpected(r.error());
        }

        {
            std::scoped_lock lock(m_Impl->pathMutex);
            m_Impl->pathById[id] = abs;
        }

        if (auto r = m_Impl->registry.SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));
            !r.has_value())
        {
            (void)m_Impl->payloadStore.Retire(id);
            (void)m_Impl->pathIndex.Erase(abs, id);
            {
                std::scoped_lock lock(m_Impl->pathMutex);
                m_Impl->pathById.erase(id);
            }
            (void)m_Impl->registry.Destroy(id);
            return std::unexpected(r.error());
        }

        auto thunk = [loader = std::move(loader), this, id](
                         std::string_view p) -> Core::Result {
            if (!m_Impl->registry.IsAlive(id))
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            auto ticketInner = loader(p, id);
            if (!ticketInner.has_value())
            {
                return std::unexpected(ticketInner.error());
            }
            if (auto r = m_Impl->registry.SetPayloadSlot(
                    id, static_cast<uint32_t>(ticketInner->slot));
                !r.has_value())
            {
                return std::unexpected(r.error());
            }
            return Core::Ok();
        };

        const InternalLoaderToken token = m_Impl->loaderRegistry.Register(std::move(thunk));
        {
            std::scoped_lock lock(m_Impl->loaderMutex);
            m_Impl->loaderByAsset[id] = token;
        }

        LoadRequest req{.id = id, .typeId = typeId, .path = abs, .needsGpuUpload = false};
        if (auto q = m_Impl->loadPipeline.EnqueueIO(std::move(req)); !q.has_value())
        {
            (void)m_Impl->loaderRegistry.Unregister(token);
            {
                std::scoped_lock lock(m_Impl->loaderMutex);
                m_Impl->loaderByAsset.erase(id);
            }
            (void)m_Impl->payloadStore.Retire(id);
            (void)m_Impl->pathIndex.Erase(abs, id);
            {
                std::scoped_lock lock(m_Impl->pathMutex);
                m_Impl->pathById.erase(id);
            }
            (void)m_Impl->registry.Destroy(id);
            return std::unexpected(q.error());
        }

        return id;
    }

    Core::Result AssetService::Reload(AssetId id)
    {
        if (!m_Impl->registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        const auto meta = m_Impl->registry.GetMeta(id);
        if (!meta.has_value())
        {
            return std::unexpected(meta.error());
        }

        if (meta->state != AssetState::Ready)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const auto tokenOr = m_Impl->FindLoaderToken(id);
        if (!tokenOr.has_value())
        {
            return std::unexpected(tokenOr.error());
        }
        const InternalLoaderToken token = *tokenOr;

        std::string path;
        {
            std::scoped_lock lock(m_Impl->pathMutex);
            const auto pathIt = m_Impl->pathById.find(id);
            if (pathIt == m_Impl->pathById.end())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            path = pathIt->second;
        }

        auto toUnloaded = m_Impl->registry.SetState(id, AssetState::Ready, AssetState::Unloaded);
        if (!toUnloaded.has_value())
        {
            return toUnloaded;
        }

        auto invokeResult = m_Impl->loaderRegistry.InvokeOr(
            token, Core::ErrorCode::AssetLoaderMissing, std::string_view(path));
        if (!invokeResult.has_value())
        {
            (void)m_Impl->registry.SetState(id, AssetState::Unloaded, AssetState::Ready);
            return invokeResult;
        }

        LoadRequest req{.id = id, .typeId = meta->typeId, .path = path, .needsGpuUpload = false};
        auto result = m_Impl->loadPipeline.EnqueueIO(std::move(req));
        if (!result.has_value())
        {
            (void)m_Impl->loadPipeline.MarkFailed(id);
            return result;
        }

        m_Impl->eventBus.Publish(id, AssetEvent::Reloaded);
        return Core::Ok();
    }

    Core::Result AssetService::Destroy(AssetId id)
    {
        if (!m_Impl->registry.IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        m_Impl->EraseLoader(id);

        std::string path;
        {
            std::scoped_lock lock(m_Impl->pathMutex);
            const auto pathIt = m_Impl->pathById.find(id);
            if (pathIt != m_Impl->pathById.end())
            {
                path = pathIt->second;
                m_Impl->pathById.erase(pathIt);
            }
        }

        if (!path.empty())
        {
            (void)m_Impl->pathIndex.Erase(path, id);
        }

        (void)m_Impl->payloadStore.Retire(id);

        if (auto destroyed = m_Impl->registry.Destroy(id); !destroyed.has_value())
        {
            return destroyed;
        }

        m_Impl->eventBus.Publish(id, AssetEvent::Destroyed);
        return Core::Ok();
    }

    void AssetService::Tick()
    {
        m_Impl->eventBus.Flush();
    }

    bool AssetService::HasLoaderCallback(LoaderToken token) const
    {
        InternalLoaderToken internal{token.Index, token.Generation};
        return m_Impl->loaderRegistry.Contains(internal);
    }

    std::size_t AssetService::LoaderCallbackCount() const
    {
        return m_Impl->loaderRegistry.Size();
    }

    bool AssetService::PathIndexContains(std::string_view absolutePath) const
    {
        return m_Impl->pathIndex.Contains(absolutePath);
    }

    AssetEventBus::ListenerToken AssetService::SubscribeAll(AssetEventBus::ListenerCallback cb)
    {
        return m_Impl->eventBus.SubscribeAll(std::move(cb));
    }

    void AssetService::UnsubscribeAll(AssetEventBus::ListenerToken token)
    {
        m_Impl->eventBus.UnsubscribeAll(token);
    }

    std::size_t AssetService::LiveAssetCount() const noexcept
    {
        return m_Impl->registry.LiveCount();
    }

    Core::Result AssetService::ForceAssetState(AssetId id, AssetState expected, AssetState next)
    {
        return m_Impl->registry.SetState(id, expected, next);
    }
}
