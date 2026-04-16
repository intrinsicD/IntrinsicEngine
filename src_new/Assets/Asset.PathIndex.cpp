module;

#include <string>
#include <mutex>

module Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    [[nodiscard]] Core::Expected<AssetId> AssetPathIndex::Find(std::string_view absolutePath) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Index.find(std::string(absolutePath));
        if (it == m_Index.end())
        {
            return Core::Err<AssetId>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    [[nodiscard]] Core::Result AssetPathIndex::Insert(std::string_view absolutePath, AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        const auto [_, inserted] = m_Index.emplace(std::string(absolutePath), id);
        if (!inserted)
        {
            return Core::Err(Core::ErrorCode::ResourceBusy);
        }
        return Core::Ok();
    }

    [[nodiscard]] Core::Result AssetPathIndex::Erase(std::string_view absolutePath, AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Index.find(std::string(absolutePath));
        if (it == m_Index.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        if (it->second != id)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        m_Index.erase(it);
        return Core::Ok();
    }
}
