module;

#include <mutex>

module Extrinsic.Asset.PayloadStore;

namespace Extrinsic::Assets
{
    Core::Result AssetPayloadStore::Retire(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Entries.find(id);
        if (it == m_Entries.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        (void)m_TypePools.Erase(it->second.typeId, id);
        m_Entries.erase(it);
        return Core::Ok();
    }

    Core::Expected<PayloadTicket> AssetPayloadStore::GetTicket(AssetId id) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Entries.find(id);
        if (it == m_Entries.end())
        {
            return Core::Err<PayloadTicket>(Core::ErrorCode::AssetNotLoaded);
        }
        return it->second.ticket;
    }

    std::size_t AssetPayloadStore::Size() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Entries.size();
    }
}
