module;

#include <mutex>
#include <string_view>
#include <string>
#include <unordered_map>

export module Extrinsic.Asset.PathIndex;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;

namespace Extrinsic::Assets
{
    export class AssetPathIndex
    {
    public:
        [[nodiscard]] Core::Expected<AssetId> Find(std::string_view absolutePath) const;
        [[nodiscard]] Core::Result Insert(std::string_view absolutePath, AssetId id);
        [[nodiscard]] Core::Result Erase(std::string_view absolutePath, AssetId id);

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, AssetId> m_Index{};
    };
}
