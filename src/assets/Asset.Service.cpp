module;

#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <utility>

module Extrinsic.Asset.Service;

import Extrinsic.Core.Hash;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    struct AssetService::Impl
    {
        AssetPathIndex pathIndex;
        AssetRegistry registry;
        AssetPayloadStore payloadStore;
        AssetEventBus eventBus;
        AssetLoadPipeline loadPipeline;

        // Service transactions precede pipeline, registry, payload, and path locks.
        // Decoders and event dispatch never run under this mutex.
        mutable std::mutex transactionMutex;
        std::unordered_map<AssetId, std::string, AssetIdHash> pathById;

        explicit Impl(AssetLoadPipelineTestHooks testHooks)
            : loadPipeline(std::move(testHooks))
        {
            loadPipeline.BindRegistry(&registry);
            loadPipeline.BindEventBus(&eventBus);
        }
    };

    AssetService::AssetService(AssetLoadPipelineTestHooks testHooks)
        : m_Impl(std::make_unique<Impl>(std::move(testHooks)))
    {
    }

    AssetService::~AssetService() = default;

    std::mutex& AssetService::TransactionMutex() const noexcept { return m_Impl->transactionMutex; }
    const AssetRegistry& AssetService::Registry() const noexcept { return m_Impl->registry; }
    AssetPayloadStore& AssetService::PayloadStore() noexcept { return m_Impl->payloadStore; }
    const AssetPayloadStore& AssetService::PayloadStore() const noexcept { return m_Impl->payloadStore; }

    Core::Expected<AssetMeta> AssetService::GetMeta(AssetId id) const
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        return m_Impl->registry.GetMeta(id);
    }

    Core::Expected<std::string> AssetService::GetPath(AssetId id) const
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        const auto it = m_Impl->pathById.find(id);
        if (it == m_Impl->pathById.end())
            return Core::Err<std::string>(Core::ErrorCode::ResourceNotFound);
        return it->second;
    }

    bool AssetService::IsAlive(AssetId id) const noexcept
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        return m_Impl->registry.IsAlive(id);
    }

    Core::Expected<PayloadTicket> AssetService::GetPayloadTicket(AssetId id) const
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        if (!m_Impl->registry.IsAlive(id))
            return Core::Err<PayloadTicket>(Core::ErrorCode::ResourceNotFound);
        return m_Impl->payloadStore.GetTicket(id);
    }

    Core::Expected<AssetId> AssetService::LoadErased(
        std::string_view path, uint32_t typeId, PreparedLoader loader)
    {
        if (path.empty())
            return Core::Err<AssetId>(Core::ErrorCode::InvalidArgument);
        const auto absolutePath = Core::Filesystem::GetAbsolutePath(std::string(path));
        auto findExisting = [&]() -> Core::Expected<AssetId>
        {
            auto found = m_Impl->pathIndex.Find(absolutePath);
            if (!found.has_value())
                return std::unexpected(found.error());
            const auto meta = m_Impl->registry.GetMeta(*found);
            if (!meta.has_value())
                return std::unexpected(meta.error());
            if (meta->typeId != typeId)
                return Core::Err<AssetId>(Core::ErrorCode::TypeMismatch);
            return *found;
        };

        AssetId id{};
        {
            std::scoped_lock lock(m_Impl->transactionMutex);
            if (auto found = findExisting(); found.has_value()
                || found.error() != Core::ErrorCode::ResourceNotFound)
                return found;
            auto created = m_Impl->registry.Create(Core::Hash::HashString(absolutePath), typeId);
            if (!created.has_value())
                return created;
            id = *created;
        }

        // Decode against a provisional identity; no payload or path is published yet.
        auto prepared = loader(absolutePath, id);
        std::scoped_lock lock(m_Impl->transactionMutex);
        if (!m_Impl->registry.IsAlive(id))
            return Core::Err<AssetId>(Core::ErrorCode::ResourceNotFound);
        if (!prepared.has_value())
        {
            (void)m_Impl->registry.Destroy(id);
            return std::unexpected(prepared.error());
        }
        if (auto winner = findExisting(); winner.has_value()
            || winner.error() != Core::ErrorCode::ResourceNotFound)
        {
            (void)m_Impl->registry.Destroy(id);
            return winner;
        }

        auto discard = [&]()
        {
            m_Impl->loadPipeline.Cancel(id);
            (void)m_Impl->payloadStore.Retire(id);
            m_Impl->pathById.erase(id);
            (void)m_Impl->registry.Destroy(id);
        };
        auto ticket = (*prepared)();
        if (!ticket.has_value())
        {
            discard();
            return std::unexpected(ticket.error());
        }
        if (auto slot = m_Impl->registry.SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));
            !slot.has_value())
        {
            discard();
            return std::unexpected(slot.error());
        }
        m_Impl->pathById[id] = absolutePath;
        if (auto queued = m_Impl->loadPipeline.EnqueueIO(LoadRequest{.id = id}); !queued.has_value())
        {
            discard();
            return std::unexpected(queued.error());
        }
        if (auto inserted = m_Impl->pathIndex.Insert(absolutePath, id); !inserted.has_value())
        {
            discard();
            return std::unexpected(inserted.error());
        }
        return id;
    }

    Core::Result AssetService::ReloadErased(AssetId id, uint32_t typeId, PreparedLoader loader)
    {
        PayloadTicket expectedTicket{};
        std::string path;
        {
            std::scoped_lock lock(m_Impl->transactionMutex);
            auto meta = m_Impl->registry.GetMeta(id);
            if (!meta.has_value())
                return std::unexpected(meta.error());
            if (meta->typeId != typeId)
                return Core::Err(Core::ErrorCode::TypeMismatch);
            if (meta->state != AssetState::Ready)
                return Core::Err(Core::ErrorCode::InvalidState);
            auto ticket = m_Impl->payloadStore.GetTicket(id);
            if (!ticket.has_value())
                return std::unexpected(ticket.error());
            expectedTicket = *ticket;
            const auto it = m_Impl->pathById.find(id);
            if (it == m_Impl->pathById.end())
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            path = it->second;
        }

        auto prepared = loader(path, id);
        std::scoped_lock lock(m_Impl->transactionMutex);
        auto meta = m_Impl->registry.GetMeta(id);
        if (!meta.has_value())
            return std::unexpected(meta.error());
        auto currentTicket = m_Impl->payloadStore.GetTicket(id);
        if (!currentTicket.has_value())
            return std::unexpected(currentTicket.error());
        if (*currentTicket != expectedTicket || meta->state != AssetState::Ready)
            return Core::Err(Core::ErrorCode::ResourceBusy);
        if (!prepared.has_value())
            return std::unexpected(prepared.error());

        // Checkpoint and rollback are one transaction: a dead or newer asset can
        // never receive the payload captured before an unlocked decoder call.
        auto checkpoint = m_Impl->payloadStore.CaptureCheckpoint(id);
        if (!checkpoint.has_value())
            return std::unexpected(checkpoint.error());
        auto restorePreviousPayload = [&]()
        {
            (void)m_Impl->payloadStore.RestoreCheckpoint(id, *checkpoint);
            (void)m_Impl->registry.SetPayloadSlot(id, meta->payloadSlot);
            auto currentState = m_Impl->registry.GetState(id);
            if (currentState.has_value() && *currentState != AssetState::Ready)
                (void)m_Impl->registry.SetState(id, *currentState, AssetState::Ready);
        };
        auto ticket = (*prepared)();
        if (!ticket.has_value())
            return std::unexpected(ticket.error());
        if (auto slot = m_Impl->registry.SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));
            !slot.has_value())
        {
            restorePreviousPayload();
            return slot;
        }
        if (auto reset = m_Impl->registry.SetState(id, AssetState::Ready, AssetState::Unloaded);
            !reset.has_value())
        {
            restorePreviousPayload();
            return reset;
        }
        auto queued = m_Impl->loadPipeline.EnqueueIO(LoadRequest{
            .id = id, .publishQueuedEvent = true, .queuedEvent = AssetEvent::Reloaded});
        if (!queued.has_value())
            restorePreviousPayload();
        return queued;
    }

    Core::Result AssetService::Destroy(AssetId id)
    {
        {
            std::scoped_lock lock(m_Impl->transactionMutex);
            if (!m_Impl->registry.IsAlive(id))
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            m_Impl->loadPipeline.Cancel(id);
        }
        // Existing Ready listeners can still read their payload, and may re-enter.
        m_Impl->eventBus.Flush(id);
        std::scoped_lock lock(m_Impl->transactionMutex);
        if (!m_Impl->registry.IsAlive(id))
            return Core::Ok(); // A nested/concurrent Destroy already retired this generation.
        m_Impl->loadPipeline.Cancel(id); // Join any reload queued by the unlocked callbacks.
        const auto path = m_Impl->pathById.find(id);
        if (path != m_Impl->pathById.end())
        {
            (void)m_Impl->pathIndex.Erase(path->second, id);
            m_Impl->pathById.erase(path);
        }
        (void)m_Impl->payloadStore.Retire(id);
        if (auto destroyed = m_Impl->registry.Destroy(id); !destroyed.has_value())
            return destroyed;
        m_Impl->eventBus.Publish(id, AssetEvent::Destroyed);
        return Core::Ok();
    }

    void AssetService::Tick()
    {
        m_Impl->eventBus.Flush();
    }

    Core::Result AssetService::CompleteCpuLoadAndFlushEvent(const AssetId id)
    {
        {
            std::scoped_lock lock(m_Impl->transactionMutex);
            if (!m_Impl->registry.IsAlive(id))
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            if (auto completed = m_Impl->loadPipeline.CompleteCpuLoad(id); !completed.has_value())
                return completed;
        }
        m_Impl->eventBus.Flush(id);
        return Core::Ok();
    }

    bool AssetService::PathIndexContains(std::string_view absolutePath) const
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        return m_Impl->pathIndex.Contains(absolutePath);
    }

    AssetEventBus::ListenerToken AssetService::SubscribeAll(AssetEventBus::ListenerCallback cb)
    {
        if (!cb)
            return AssetEventBus::InvalidToken;
        return m_Impl->eventBus.SubscribeAll([this, cb = std::move(cb)](AssetId id, AssetEvent event)
        {
            if (event == AssetEvent::Destroyed || IsAlive(id))
                cb(id, event);
        });
    }

    void AssetService::UnsubscribeAll(AssetEventBus::ListenerToken token)
    {
        m_Impl->eventBus.UnsubscribeAll(token);
    }

    std::size_t AssetService::LiveAssetCount() const noexcept
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        return m_Impl->registry.LiveCount();
    }

    Core::Result AssetService::ForceAssetState(AssetId id, AssetState expected, AssetState next)
    {
        std::scoped_lock lock(m_Impl->transactionMutex);
        return m_Impl->registry.SetState(id, expected, next);
    }
}
