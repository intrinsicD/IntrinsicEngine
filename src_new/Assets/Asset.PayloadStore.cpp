module;

#include <mutex>

module Extrinsic.Asset.PayloadStore;

namespace Extrinsic::Assets
{
    [[nodiscard]] Core::Result AssetPayloadStore::Retire(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Entries.find(id);
        if (it == m_Entries.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        const auto poolIt = m_Pools.find(it->second.typeId);
        if (poolIt != m_Pools.end())
        {
            (void)poolIt->second->Erase(id);
        }

        m_Entries.erase(it);

        return Core::Ok();
    }
}
