module;

#include <cstddef>
#include <string_view>
#include <string>
#include <memory>

export module Extrinsic.Asset.PathIndex;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;

namespace Extrinsic::Assets
{
    export class AssetPathIndex
    {
    public:
        AssetPathIndex();
        ~AssetPathIndex();
        AssetPathIndex(const AssetPathIndex&) = delete;
        AssetPathIndex& operator=(const AssetPathIndex&) = delete;

        [[nodiscard]] Core::Expected<AssetId> Find(std::string_view absolutePath) const;
        [[nodiscard]] Core::Result Insert(std::string_view absolutePath, AssetId id);
        [[nodiscard]] Core::Result Erase(std::string_view absolutePath, AssetId id);

        [[nodiscard]] bool Contains(std::string_view absolutePath) const;
        [[nodiscard]] std::size_t Size() const;

    private:
        static constexpr std::size_t kShardCount = 32;

        struct Impl;

        [[nodiscard]] static std::size_t ShardIndex(std::string_view absolutePath) noexcept;
        std::unique_ptr<Impl> m_Impl;
    };
}
