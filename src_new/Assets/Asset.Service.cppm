module;

#include <string_view>
#include <span>
#include <cstdint>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.PathIndex;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Filesystem;

export namespace Extrinsic::Assets
{
    class AssetService
    {
    public:
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
        AssetLoadPipeline m_LoadPipeline;
    };

    template <class T, class Loader>
    [[nodiscard]] Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        const auto abs = Core::Filesystem::GetAbsolutePath(std::string(path));

        if (auto found = m_PathIndex.Find(abs); found.has_value())
        {
            return *found;
        }

        constexpr uint32_t typeId = /* compile-time type hash */ 0;
        auto id = m_Registry.Create(Core::Hash::HashString(abs), typeId);
        if (!id.has_value()) return id;

        if (auto r = m_PathIndex.Insert(abs, *id); !r.has_value())
            return r.error();

        LoadRequest req{.id = *id, .typeId = typeId, .path = abs.c_str(), .needsGpuUpload = false};
        if (auto q = m_LoadPipeline.EnqueueIO(req); !q.has_value())
            return q.error();

        return *id;
    }

    template <class T>
    [[nodiscard]] Core::Expected<std::span<const T>> AssetService::Read(AssetId id) const
    {
    }
}
