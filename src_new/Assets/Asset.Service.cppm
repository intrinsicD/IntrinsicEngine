module;

#include <string_view>
#include <span>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <expected>
#include <functional>
#include <memory>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.PathIndex;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.TypePool;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Filesystem;

export namespace Extrinsic::Assets
{
    class AssetService
    {
    public:
        AssetService();
        AssetService(const AssetService&) = delete;
        AssetService& operator=(const AssetService&) = delete;

        template <class T, class Loader>
        [[nodiscard]] Core::Expected<AssetId> Load(std::string_view path, Loader&& loader);

        template <class T, class Loader>
        Core::Result Reload(AssetId id, Loader&& loader);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> Read(AssetId id) const;

        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;

        [[nodiscard]] Core::Expected<std::string> GetPath(AssetId id) const;

        Core::Result Reload(AssetId id);
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

        struct Loaders
        {
            std::shared_ptr<std::function<void(std::string_view)>> FindLoader(uint32_t typeId)
            {
                auto it = m_LoaderByType.find(typeId);
                if (it != m_LoaderByType.end())
                {
                    return it->second;
                }
                return {};
            }

            std::unordered_map<uint32_t, std::shared_ptr<std::function<void(std::string_view)>>> m_LoaderByType{};
        };
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
    [[nodiscard]] Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        if (path.empty())
        {
            return Core::Err<AssetId>(Core::ErrorCode::InvalidArgument);
        }

        const auto abs = Core::Filesystem::GetAbsolutePath(std::string(path));

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
