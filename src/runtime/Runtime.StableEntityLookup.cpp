module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.StableEntityLookup;

import Extrinsic.ECS.Component.StableId;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Comp = Extrinsic::ECS::Components;
    }

    void StableEntityLookup::EraseWinner(StableId id, std::uint32_t renderId)
    {
        m_StableIdToEntity.erase(id);
        m_RenderIdToStableId.erase(renderId);
    }

    void StableEntityLookup::InsertWinner(EntityHandle entity, StableId id)
    {
        const std::uint32_t renderId = ToRenderId(entity);

        const auto it = m_StableIdToEntity.find(id);
        if (it == m_StableIdToEntity.end())
        {
            m_StableIdToEntity.emplace(id, entity);
            m_RenderIdToStableId[renderId] = id;
            return;
        }

        if (it->second == entity)
            return; // re-tracking the same entity / id is idempotent

        // Two distinct live entities claim the same durable id: keep the
        // smallest-render-id winner deterministically, independent of order.
        ++m_Diagnostics.DuplicateStableIds;

        const std::uint32_t existingRenderId = ToRenderId(it->second);
        if (renderId < existingRenderId)
        {
            m_RenderIdToStableId.erase(existingRenderId);
            it->second                     = entity;
            m_RenderIdToStableId[renderId] = id;
        }
    }

    void StableEntityLookup::Rebuild(const Registry& registry)
    {
        m_StableIdToEntity.clear();
        m_RenderIdToStableId.clear();
        ++m_Diagnostics.Rebuilds;

        const auto view = registry.Raw().view<const Comp::StableId>();
        for (const auto [entity, id] : view.each())
        {
            if (Comp::IsValid(id))
                InsertWinner(entity, id);
        }

        m_Diagnostics.TrackedStableIds = static_cast<std::uint32_t>(m_StableIdToEntity.size());
    }

    bool StableEntityLookup::Track(const Registry& registry, EntityHandle entity)
    {
        if (!registry.IsValid(entity))
            return false;

        const auto& raw = registry.Raw();
        if (!raw.all_of<Comp::StableId>(entity))
            return false;

        const Comp::StableId id = raw.get<Comp::StableId>(entity);
        if (!Comp::IsValid(id))
            return false;

        ++m_Diagnostics.IncrementalTracks;

        // If this entity was previously the winner for a *different* durable id
        // (its StableId component was reassigned via hot-reload / undo / editor
        // edit), drop that stale winner entry so the old id stops resolving to
        // this entity. The reverse mirror holds winners only, so a reverse entry
        // for this render id means this entity currently owns `previousId`. A
        // full Rebuild would re-derive winners, but the incremental path must
        // reconcile on its own.
        const std::uint32_t renderId   = ToRenderId(entity);
        const auto          reverseIt  = m_RenderIdToStableId.find(renderId);
        if (reverseIt != m_RenderIdToStableId.end() && reverseIt->second != id)
        {
            const StableId previousId = reverseIt->second;
            const auto     prevIt     = m_StableIdToEntity.find(previousId);
            if (prevIt != m_StableIdToEntity.end() && prevIt->second == entity)
                m_StableIdToEntity.erase(prevIt);
            m_RenderIdToStableId.erase(reverseIt);
        }

        InsertWinner(entity, id);
        m_Diagnostics.TrackedStableIds = static_cast<std::uint32_t>(m_StableIdToEntity.size());

        const auto it = m_StableIdToEntity.find(id);
        return it != m_StableIdToEntity.end() && it->second == entity;
    }

    void StableEntityLookup::Forget(EntityHandle entity)
    {
        const std::uint32_t renderId = ToRenderId(entity);

        const auto rit = m_RenderIdToStableId.find(renderId);
        if (rit == m_RenderIdToStableId.end())
            return; // entity was never a winner

        const StableId id = rit->second;
        const auto     it = m_StableIdToEntity.find(id);
        if (it != m_StableIdToEntity.end() && it->second == entity)
        {
            m_StableIdToEntity.erase(it);
            ++m_Diagnostics.IncrementalForgets;
            m_Diagnostics.TrackedStableIds = static_cast<std::uint32_t>(m_StableIdToEntity.size());
        }
        m_RenderIdToStableId.erase(rit);
    }

    void StableEntityLookup::Clear() noexcept
    {
        m_StableIdToEntity.clear();
        m_RenderIdToStableId.clear();
        m_Diagnostics.TrackedStableIds = 0u;
    }

    std::optional<StableEntityLookup::EntityHandle> StableEntityLookup::ResolveByStableId(
        const Registry& registry, StableId id)
    {
        if (!Comp::IsValid(id))
        {
            ++m_Diagnostics.MissingStableIdLookups;
            return std::nullopt;
        }

        const auto it = m_StableIdToEntity.find(id);
        if (it == m_StableIdToEntity.end())
        {
            ++m_Diagnostics.MissingStableIdLookups;
            return std::nullopt;
        }

        const EntityHandle entity = it->second;
        if (!registry.IsValid(entity))
        {
            // Entity destroyed without a Forget: reject and lazily self-heal.
            ++m_Diagnostics.StaleEntityResolves;
            ++m_Diagnostics.StaleEntriesPruned;
            EraseWinner(id, ToRenderId(entity));
            m_Diagnostics.TrackedStableIds = static_cast<std::uint32_t>(m_StableIdToEntity.size());
            return std::nullopt;
        }

        return entity;
    }

    std::optional<StableEntityLookup::EntityHandle> StableEntityLookup::ResolveByRenderId(
        const Registry& registry, std::uint32_t renderId)
    {
        const EntityHandle entity = ToEntityHandle(renderId);
        if (entity == Extrinsic::ECS::InvalidEntityHandle || !registry.IsValid(entity))
        {
            ++m_Diagnostics.StaleEntityResolves;
            return std::nullopt;
        }
        return entity;
    }

    std::vector<StableEntityLookup::EntityHandle> StableEntityLookup::ResolveSelected(
        const Registry& registry, std::span<const StableId> ids)
    {
        std::vector<EntityHandle> resolved;
        resolved.reserve(ids.size());
        for (const StableId id : ids)
        {
            if (const std::optional<EntityHandle> entity = ResolveByStableId(registry, id))
                resolved.push_back(*entity);
        }
        return resolved;
    }

    std::size_t StableEntityLookup::PruneStale(const Registry& registry)
    {
        std::size_t pruned = 0u;
        for (auto it = m_StableIdToEntity.begin(); it != m_StableIdToEntity.end();)
        {
            if (!registry.IsValid(it->second))
            {
                m_RenderIdToStableId.erase(ToRenderId(it->second));
                it = m_StableIdToEntity.erase(it);
                ++pruned;
            }
            else
            {
                ++it;
            }
        }

        m_Diagnostics.StaleEntriesPruned += static_cast<std::uint32_t>(pruned);
        m_Diagnostics.TrackedStableIds = static_cast<std::uint32_t>(m_StableIdToEntity.size());
        return pruned;
    }
}
