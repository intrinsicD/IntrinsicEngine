module;

#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Core.Filesystem;
import Extrinsic.Core.Hash;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.PathIndex;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.TypePool;

export namespace Extrinsic::Assets
{
    // AssetService is the top-level facade: it wires a Registry, PathIndex,
    // PayloadStore, LoadPipeline and EventBus together and exposes a minimal
    // async load/read/reload/destroy API.
    //
    // Thread model: AssetService is thread-safe; every internal subsystem has
    // its own lock. Tick() must be called from the main thread to drain the
    // EventBus.
    class AssetService
    {
    public:
        AssetService();
        AssetService(const AssetService&) = delete;
        AssetService& operator=(const AssetService&) = delete;

        // Load a new asset, or return the existing AssetId if the path has
        // already been registered.
        //
        // Loader is any callable with signature:
        //   Core::Expected<T>(std::string_view absolutePath, AssetId id)
        // It runs synchronously inside Load() so the caller controls threading.
        template <class T, class Loader>
        [[nodiscard]] Core::Expected<AssetId> Load(std::string_view path, Loader&& loader);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> Read(AssetId id) const;

        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;

        [[nodiscard]] Core::Expected<std::string> GetPath(AssetId id) const;

        // Transitions the asset back through the Unloaded -> QueuedIO state
        // machine. Requires the caller to previously have Ready'd this asset.
        //
        // The user-supplied loader must be provided again because the service
        // does not retain any typed decoder.
        template <class T, class Loader>
        [[nodiscard]] Core::Result Reload(AssetId id, Loader&& loader);

        Core::Result Destroy(AssetId id);

        // Drain pending events. Call from the main thread.
        void Tick();

        // Exposed for advanced wiring / testing.
        [[nodiscard]] AssetRegistry& Registry() noexcept { return m_Registry; }
        [[nodiscard]] AssetEventBus& EventBus() noexcept { return m_EventBus; }
        [[nodiscard]] AssetPayloadStore& PayloadStore() noexcept { return m_PayloadStore; }
        [[nodiscard]] AssetLoadPipeline& LoadPipeline() noexcept { return m_LoadPipeline; }
        [[nodiscard]] AssetPathIndex& PathIndex() noexcept { return m_PathIndex; }

        // Produce a stable type id for a C++ type T. The same type yields the
        // same id within a single process.
        template <class T>
        [[nodiscard]] static uint32_t TypeIdOf() noexcept;

    private:
        AssetPathIndex m_PathIndex;
        AssetRegistry m_Registry;
        AssetPayloadStore m_PayloadStore;
        AssetLoadPipeline m_LoadPipeline;
        AssetEventBus m_EventBus;

        mutable std::mutex m_PathMutex;
        std::unordered_map<AssetId, std::string> m_PathById{};
    };

    template <class T>
    uint32_t AssetService::TypeIdOf() noexcept
    {
        // Delegate to the single RTTI-free type-key authority. TypePools::Type<T>()
        // returns a stable per-type pointer address.
        const auto p = TypePools<AssetId>::Type<std::remove_cvref_t<T>>();
        if constexpr (sizeof(std::uintptr_t) > sizeof(uint32_t))
        {
            return static_cast<uint32_t>(p ^ (p >> 32));
        }
        else
        {
            return static_cast<uint32_t>(p);
        }
    }

    template <class T, class Loader>
    Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        if (path.empty())
        {
            return Core::Err<AssetId>(Core::ErrorCode::InvalidArgument);
        }

        const auto abs = Core::Filesystem::GetAbsolutePath(std::string(path));

        // First: run the caller's loader before mutating any state. If it
        // fails we can just return the error - nothing has been allocated
        // yet, no Failed ghost asset is left behind (B1).
        if (auto found = m_PathIndex.Find(abs); found.has_value())
        {
            return *found;
        }

        auto payload = loader(std::string_view(abs), AssetId{});
        if (!payload.has_value())
        {
            return std::unexpected(payload.error());
        }

        const uint32_t typeId = TypeIdOf<T>();
        auto idResult = m_Registry.Create(Core::Hash::HashString(abs), typeId);
        if (!idResult.has_value())
        {
            return idResult;
        }
        const AssetId id = *idResult;

        if (auto r = m_PathIndex.Insert(abs, id); !r.has_value())
        {
            // Concurrent Load for the same path raced us to Insert. Clean up
            // our speculative id and return the winner's id so callers see
            // idempotent "first caller wins" semantics (B2).
            (void)m_Registry.Destroy(id);
            if (auto winner = m_PathIndex.Find(abs); winner.has_value())
            {
                return *winner;
            }
            return std::unexpected(r.error());
        }

        {
            std::scoped_lock lock(m_PathMutex);
            m_PathById[id] = abs;
        }

        auto ticket = m_PayloadStore.Publish(id, std::move(*payload));
        if (!ticket.has_value())
        {
            // Full unwind - no stale PathIndex entry, no zombie registry slot.
            (void)m_PathIndex.Erase(abs, id);
            {
                std::scoped_lock lock(m_PathMutex);
                m_PathById.erase(id);
            }
            (void)m_Registry.Destroy(id);
            return std::unexpected(ticket.error());
        }

        if (auto r = m_Registry.SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));
            !r.has_value())
        {
            (void)m_PayloadStore.Retire(id);
            (void)m_PathIndex.Erase(abs, id);
            {
                std::scoped_lock lock(m_PathMutex);
                m_PathById.erase(id);
            }
            (void)m_Registry.Destroy(id);
            return std::unexpected(r.error());
        }

        LoadRequest req{.id = id, .typeId = typeId, .path = abs, .needsGpuUpload = false};

        if (auto q = m_LoadPipeline.EnqueueIO(std::move(req)); !q.has_value())
        {
            (void)m_PayloadStore.Retire(id);
            (void)m_PathIndex.Erase(abs, id);
            {
                std::scoped_lock lock(m_PathMutex);
                m_PathById.erase(id);
            }
            (void)m_Registry.Destroy(id);
            return std::unexpected(q.error());
        }

        return id;
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetService::Read(AssetId id) const
    {
        if (!m_Registry.IsAlive(id))
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }
        return m_PayloadStore.ReadSpan<T>(id);
    }

    template <class T, class Loader>
    Core::Result AssetService::Reload(AssetId id, Loader&& loader)
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

        if (meta->typeId != TypeIdOf<T>())
        {
            return Core::Err(Core::ErrorCode::TypeMismatch);
        }

        // Reload is only valid from Ready; other states indicate a load is
        // already in flight or the asset has already failed.
        if (meta->state != AssetState::Ready)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
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

        // Run the loader BEFORE mutating state so a failed reload leaves
        // the asset in its prior Ready state with its old payload intact (B3).
        auto payload = loader(std::string_view(path), id);
        if (!payload.has_value())
        {
            return std::unexpected(payload.error());
        }

        auto toUnloaded = m_Registry.SetState(id, AssetState::Ready, AssetState::Unloaded);
        if (!toUnloaded.has_value())
        {
            // The asset is no longer Ready - someone concurrently destroyed
            // or reloaded it. Drop the decoded payload and surface the error.
            return toUnloaded;
        }

        auto ticket = m_PayloadStore.Publish(id, std::move(*payload));
        if (!ticket.has_value())
        {
            (void)m_LoadPipeline.MarkFailed(id);
            return std::unexpected(ticket.error());
        }
        (void)m_Registry.SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));

        LoadRequest req{.id = id, .typeId = meta->typeId, .path = path, .needsGpuUpload = false};
        if (auto r = m_LoadPipeline.EnqueueIO(std::move(req)); !r.has_value())
        {
            (void)m_LoadPipeline.MarkFailed(id);
            return r;
        }

        m_EventBus.Publish(id, AssetEvent::Reloaded);
        return Core::Ok();
    }
}
