module;

#include <cstdint>
#include <mutex>

module Extrinsic.Asset.Registry;

namespace Extrinsic::Assets
{
    bool AssetRegistry::IsAliveLocked(AssetId id) const noexcept
    {
        if (!id.IsValid())
        {
            return false;
        }
        if (id.Index >= m_Generations.size())
        {
            return false;
        }
        return m_Allocated[id.Index] != 0u && m_Generations[id.Index] == id.Generation;
    }

    [[nodiscard]] Core::Expected<AssetId> AssetRegistry::Create(uint32_t pathHash, uint32_t typeId)
    {
        std::scoped_lock lock(m_Mutex);

        uint32_t index = 0;
        if (!m_FreeList.empty())
        {
            index = m_FreeList.back();
            m_FreeList.pop_back();

            m_Allocated[index] = 1u;
            m_PathHashes[index] = pathHash;
            m_States[index] = AssetState::Unloaded;
            m_TypeIds[index] = typeId;
            m_PayloadSlots[index] = 0u;

            return AssetId{index, m_Generations[index]};
        }

        index = static_cast<uint32_t>(m_Generations.size());
        m_PathHashes.push_back(pathHash);
        m_States.push_back(AssetState::Unloaded);
        m_TypeIds.push_back(typeId);
        m_PayloadSlots.push_back(0u);
        m_Generations.push_back(1u);
        m_Allocated.push_back(1u);

        return AssetId{index, 1u};
    }

    [[nodiscard]] bool AssetRegistry::IsAlive(AssetId id) const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return IsAliveLocked(id);
    }

    [[nodiscard]] Core::Expected<AssetMeta> AssetRegistry::GetMeta(AssetId id) const
    {
        std::scoped_lock lock(m_Mutex);
        if (!IsAliveLocked(id))
        {
            return Core::Err<AssetMeta>(Core::ErrorCode::ResourceNotFound);
        }

        return AssetMeta{
            .pathHash = m_PathHashes[id.Index],
            .state = m_States[id.Index],
            .typeId = m_TypeIds[id.Index],
            .payloadSlot = m_PayloadSlots[id.Index],
        };
    }

    [[nodiscard]] Core::Result AssetRegistry::SetState(AssetId id, AssetState expected, AssetState next)
    {
        std::scoped_lock lock(m_Mutex);
        if (!IsAliveLocked(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        if (m_States[id.Index] != expected)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_States[id.Index] = next;
        return Core::Ok();
    }

    [[nodiscard]] Core::Result AssetRegistry::Destroy(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        if (!IsAliveLocked(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        m_Allocated[id.Index] = 0u;
        ++m_Generations[id.Index];
        m_PathHashes[id.Index] = 0u;
        m_States[id.Index] = AssetState::Unloaded;
        m_TypeIds[id.Index] = 0u;
        m_PayloadSlots[id.Index] = 0u;
        m_FreeList.push_back(id.Index);

        return Core::Ok();
    }
}
