module;

#include <mutex>
#include <string>
#include <string_view>

module Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    Core::Expected<AssetId> AssetPathIndex::Find(std::string_view absolutePath) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Index.find(absolutePath);
        if (it == m_Index.end())
        {
            return Core::Err<AssetId>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    Core::Result AssetPathIndex::Insert(std::string_view absolutePath, AssetId id)
    {
        if (absolutePath.empty() || !id.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        std::scoped_lock lock(m_Mutex);
        const auto [_, inserted] = m_Index.emplace(std::string(absolutePath), id);
        if (!inserted)
        {
            return Core::Err(Core::ErrorCode::ResourceBusy);
        }
        return Core::Ok();
    }

    Core::Result AssetPathIndex::Erase(std::string_view absolutePath, AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Index.find(absolutePath);
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

    bool AssetPathIndex::Contains(std::string_view absolutePath) const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Index.find(absolutePath) != m_Index.end();
    }

    std::size_t AssetPathIndex::Size() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Index.size();
    }
}
