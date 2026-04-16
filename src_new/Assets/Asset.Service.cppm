module;

#include <string_view>
#include <span>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <expected>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.PathIndex;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.EventBus;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Filesystem;

export namespace Extrinsic::Assets
{
    class AssetService
    {
    public:
        AssetService();

        template <class T, class Loader>
        [[nodiscard]] Core::Expected<AssetId> Load(std::string_view path, Loader&& loader);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> Read(AssetId id) const;

        Core::Result Reload(AssetId id);
        Core::Result Destroy(AssetId id);
        void Tick(); // flush bus + promotions
    private:
        AssetPathIndex m_PathIndex;
        AssetRegistry m_Registry;
        AssetPayloadStore m_PayloadStore;
        AssetLoadPipeline m_LoadPipeline;
        AssetEventBus m_EventBus;

        mutable std::mutex m_PathMutex;
        std::unordered_map<AssetId, std::string> m_PathById{};
    };

    template <class T, class Loader>
    [[nodiscard]] Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        const auto abs = Core::Filesystem::GetAbsolutePath(std::string(path));

        if (auto found = m_PathIndex.Find(abs); found.has_value())
        {
            return *found;
        }

        const uint32_t typeId = static_cast<uint32_t>(typeid(T).hash_code());
        auto id = m_Registry.Create(Core::Hash::HashString(abs), typeId);
        if (!id.has_value())
        {
            return id;
        }
        if (auto r = m_PathIndex.Insert(abs, *id); !r.has_value())
        {
            return std::unexpected(r.error());
        }

        auto payload = loader(abs, *id);
        if (!payload.has_value())
        {
            (void)m_Registry.SetState(*id, AssetState::Unloaded, AssetState::Failed);
            m_EventBus.Publish(*id, AssetEvent::Failed);
            return std::unexpected(payload.error());
        }

        if (auto ticket = m_PayloadStore.Publish(*id, std::move(*payload)); !ticket.has_value())
        {
            return std::unexpected(ticket.error());
        }

        {
            std::scoped_lock lock(m_PathMutex);
            m_PathById[*id] = abs;
        }

        LoadRequest req{.id = *id, .typeId = typeId, .path = nullptr, .needsGpuUpload = false};

        if (auto q = m_LoadPipeline.EnqueueIO(req); !q.has_value())
        {
            return std::unexpected(q.error());
        }

        return *id;
    }

    template <class T>
    [[nodiscard]] Core::Expected<std::span<const T>> AssetService::Read(AssetId id) const
    {
        return m_PayloadStore.ReadSpan<T>(id);
    }
}
