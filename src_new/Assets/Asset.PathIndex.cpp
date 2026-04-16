module;

#include <string>

module Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    [[nodiscard]] Core::Expected<AssetId> AssetPathIndex::Find(std::string_view absolutePath) const
    {
        if (!m_Index.contains(std::string(absolutePath)))
        {
            return Core::Err<AssetId>(Core::ErrorCode::ResourceNotFound);
        }
        return m_Index.at(std::string(absolutePath));
    }

    [[nodiscard]] Core::Result AssetPathIndex::Insert(std::string_view absolutePath, AssetId id)
    {
        if (m_Index.contains(std::string(absolutePath)))
        {
            return Core::Err(Core::ErrorCode::ResourceBusy);
        }
        m_Index.emplace(std::string(absolutePath), id);
        return Core::Ok();
    }

    [[nodiscard]] Core::Result AssetPathIndex::Erase(std::string_view absolutePath, AssetId id)
    {
        if (!m_Index.contains(std::string(absolutePath)))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        m_Index.erase(std::string(absolutePath));
        return Core::Ok();
    }
}
