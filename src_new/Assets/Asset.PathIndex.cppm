module;

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

export module Extrinsic.Asset.PathIndex;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;

namespace Extrinsic::Assets
{
    export class AssetPathIndex
    {
    public:
        AssetPathIndex() = default;
        AssetPathIndex(const AssetPathIndex&) = delete;
        AssetPathIndex& operator=(const AssetPathIndex&) = delete;

        [[nodiscard]] Core::Expected<AssetId> Find(std::string_view absolutePath) const;
        [[nodiscard]] Core::Result Insert(std::string_view absolutePath, AssetId id);
        [[nodiscard]] Core::Result Erase(std::string_view absolutePath, AssetId id);
        [[nodiscard]] bool Contains(std::string_view absolutePath) const;
        [[nodiscard]] std::size_t Size() const;

    private:
        struct StringHash
        {
            using is_transparent = void;
            [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
            [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept
            {
                return std::hash<std::string_view>{}(s);
            }
        };

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, AssetId, StringHash, std::equal_to<>> m_Index{};
    };
}
