module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.SelectionController;

import Extrinsic.ECS.Components.Selection;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Sel = Extrinsic::ECS::Components::Selection;

        using EntityHandle = Extrinsic::ECS::EntityHandle;

        [[nodiscard]] bool IsValidEntity(const entt::registry& registry, EntityHandle entity) noexcept
        {
            return entity != Extrinsic::ECS::InvalidEntityHandle && registry.valid(entity);
        }

        void RemoveTagIfPresent(entt::registry& registry, EntityHandle entity)
        {
            if (IsValidEntity(registry, entity) && registry.all_of<Sel::SelectedTag>(entity))
                registry.remove<Sel::SelectedTag>(entity);
        }

        void AddSelectedTag(entt::registry& registry, EntityHandle entity)
        {
            if (IsValidEntity(registry, entity) && !registry.all_of<Sel::SelectedTag>(entity))
                registry.emplace<Sel::SelectedTag>(entity);
        }
    }

    SelectionController::SelectionController(const SelectionControllerConfig& config) noexcept
        : m_Config(config)
    {
    }

    // --- coalescing --------------------------------------------------------

    void SelectionController::RequestHoverPick(std::uint32_t pixelX, std::uint32_t pixelY) noexcept
    {
        ++m_Diagnostics.HoverRequestsSubmitted;

        // A pending *click* always wins: a hover that arrives while a click is
        // queued is merged away so the click is the pick the renderer resolves.
        if (m_PendingPick.has_value())
        {
            ++m_Diagnostics.PickRequestsCoalesced;
            if (m_PendingPick->Kind == SelectionPickKind::Click)
                return;
        }

        m_PendingPick = PendingSelectionPick{
            .PixelX = pixelX,
            .PixelY = pixelY,
            .Kind   = SelectionPickKind::Hover,
            .Mode   = SelectionPickMode::Replace,
        };
    }

    void SelectionController::RequestClickPick(std::uint32_t pixelX, std::uint32_t pixelY) noexcept
    {
        RequestClickPick(pixelX, pixelY, m_Config.ClickMode);
    }

    void SelectionController::RequestClickPick(std::uint32_t    pixelX,
                                               std::uint32_t    pixelY,
                                               SelectionPickMode mode) noexcept
    {
        ++m_Diagnostics.ClickRequestsSubmitted;

        // A click supersedes any pending pick (hover or earlier click); the
        // latest click position / mode wins.
        if (m_PendingPick.has_value())
            ++m_Diagnostics.PickRequestsCoalesced;

        m_PendingPick = PendingSelectionPick{
            .PixelX = pixelX,
            .PixelY = pixelY,
            .Kind   = SelectionPickKind::Click,
            .Mode   = mode,
        };
    }

    bool SelectionController::HasPendingPick() const noexcept
    {
        return m_PendingPick.has_value();
    }

    std::optional<PendingSelectionPick> SelectionController::PeekPendingPick() const noexcept
    {
        return m_PendingPick;
    }

    std::optional<PendingSelectionPick> SelectionController::ConsumePendingPick() noexcept
    {
        if (!m_PendingPick.has_value())
            return std::nullopt;

        PendingSelectionPick pick = *m_PendingPick;
        pick.Sequence             = m_NextPickSequence++;
        m_PendingPick.reset();

        m_InFlightPicks.push_back(pick);
        // Bound the tracking queue so a pick whose readback is never published
        // (recycled / invalidated GPU slot) cannot grow it without limit.
        while (m_Config.MaxTrackedInFlightPicks != 0u
               && m_InFlightPicks.size() > m_Config.MaxTrackedInFlightPicks)
        {
            m_InFlightPicks.pop_front();
            ++m_Diagnostics.InFlightPicksDropped;
        }

        ++m_Diagnostics.PicksDrained;
        return pick;
    }

    PendingSelectionPick SelectionController::TakeInFlightPick(
        std::optional<std::uint64_t> pickSequence) noexcept
    {
        if (pickSequence.has_value())
        {
            const auto it = std::find_if(
                m_InFlightPicks.begin(), m_InFlightPicks.end(),
                [seq = *pickSequence](const PendingSelectionPick& p) { return p.Sequence == seq; });
            if (it != m_InFlightPicks.end())
            {
                const PendingSelectionPick pick = *it;
                m_InFlightPicks.erase(it);
                return pick;
            }
        }
        else if (!m_InFlightPicks.empty())
        {
            const PendingSelectionPick pick = m_InFlightPicks.front();
            m_InFlightPicks.pop_front();
            return pick;
        }

        // No tracked pick matched: treat as an untracked readback applied as a
        // click in the configured mode.
        ++m_Diagnostics.UntrackedReadbacks;
        return PendingSelectionPick{
            .Sequence = 0u,
            .PixelX   = 0u,
            .PixelY   = 0u,
            .Kind     = SelectionPickKind::Click,
            .Mode     = m_Config.ClickMode,
        };
    }

    // --- snapshot ----------------------------------------------------------

    void SelectionController::RebuildSnapshot()
    {
        m_SelectedStableIds.clear();
        m_SelectedStableIds.reserve(m_Selected.size());
        for (const EntityHandle entity : m_Selected)
            m_SelectedStableIds.push_back(ToStableEntityId(entity));
    }

    // --- selection-set mutation -------------------------------------------

    void SelectionController::SetSelectionSet(Registry&                        registry,
                                              const std::vector<EntityHandle>& desired)
    {
        auto&      raw     = registry.Raw();
        const bool changed = desired != m_Selected;

        if (!changed)
            return;

        // Drop tags for entities leaving the set.
        for (const EntityHandle entity : m_Selected)
        {
            if (std::find(desired.begin(), desired.end(), entity) == desired.end())
                RemoveTagIfPresent(raw, entity);
        }
        // Add tags for entities entering the set.
        for (const EntityHandle entity : desired)
        {
            if (std::find(m_Selected.begin(), m_Selected.end(), entity) == m_Selected.end())
                AddSelectedTag(raw, entity);
        }

        m_Selected = desired;
        RebuildSnapshot();
        ++m_Diagnostics.SelectionChangesEmitted;
    }

    void SelectionController::ApplyClickSelection(Registry&        registry,
                                                  EntityHandle     entity,
                                                  SelectionPickMode mode)
    {
        std::vector<EntityHandle> desired = m_Selected;
        const auto                it      = std::find(desired.begin(), desired.end(), entity);
        const bool                present = it != desired.end();

        switch (mode)
        {
            case SelectionPickMode::Replace:
                desired.assign(1, entity);
                break;
            case SelectionPickMode::Add:
                if (!present)
                    desired.push_back(entity);
                break;
            case SelectionPickMode::Toggle:
                if (present)
                    desired.erase(it);
                else
                    desired.push_back(entity);
                break;
        }

        SetSelectionSet(registry, desired);
    }

    // --- hover mutation ----------------------------------------------------

    void SelectionController::ApplyHover(Registry& registry, EntityHandle entity)
    {
        if (m_HasHovered && m_Hovered == entity)
            return;

        auto& raw = registry.Raw();
        if (m_HasHovered && IsValidEntity(raw, m_Hovered) && raw.all_of<Sel::HoveredTag>(m_Hovered))
            raw.remove<Sel::HoveredTag>(m_Hovered);

        if (!raw.all_of<Sel::HoveredTag>(entity))
            raw.emplace<Sel::HoveredTag>(entity);

        m_Hovered    = entity;
        m_HasHovered = true;
        ++m_Diagnostics.HoverChangesEmitted;
    }

    void SelectionController::ClearHover(Registry& registry)
    {
        if (!m_HasHovered)
            return;

        auto& raw = registry.Raw();
        if (IsValidEntity(raw, m_Hovered) && raw.all_of<Sel::HoveredTag>(m_Hovered))
            raw.remove<Sel::HoveredTag>(m_Hovered);

        m_Hovered    = Extrinsic::ECS::InvalidEntityHandle;
        m_HasHovered = false;
        ++m_Diagnostics.HoverChangesEmitted;
    }

    // --- readback consumption ----------------------------------------------

    void SelectionController::ApplyHitReadback(Registry&                    registry,
                                               std::uint32_t                stableEntityId,
                                               std::optional<std::uint64_t> pickSequence)
    {
        ++m_Diagnostics.ReadbacksConsumed;
        ++m_Diagnostics.Hits;

        const PendingSelectionPick pick = TakeInFlightPick(pickSequence);

        const EntityHandle entity = ToEntityHandle(stableEntityId);
        if (!IsValidEntity(registry.Raw(), entity))
        {
            ++m_Diagnostics.StaleEntityHits;
            return;
        }
        if (!registry.Raw().all_of<Sel::SelectableTag>(entity))
        {
            ++m_Diagnostics.NonSelectableHitsRejected;
            return;
        }

        if (pick.Kind == SelectionPickKind::Hover)
            ApplyHover(registry, entity);
        else
            ApplyClickSelection(registry, entity, pick.Mode);
    }

    void SelectionController::ApplyNoHitReadback(Registry&                    registry,
                                                 std::optional<std::uint64_t> pickSequence)
    {
        ++m_Diagnostics.ReadbacksConsumed;
        ++m_Diagnostics.NoHits;

        const PendingSelectionPick pick = TakeInFlightPick(pickSequence);

        if (pick.Kind == SelectionPickKind::Hover)
        {
            if (m_Config.ClearHoverOnBackgroundHover)
                ClearHover(registry);
            return;
        }

        if (pick.Mode == SelectionPickMode::Replace && m_Config.ClearSelectionOnBackgroundClick)
            SetSelectionSet(registry, {});
    }

    void SelectionController::ConsumeHit(Registry&     registry,
                                         std::uint32_t stableEntityId,
                                         std::uint64_t pickSequence)
    {
        ApplyHitReadback(registry, stableEntityId, pickSequence);
    }

    void SelectionController::ConsumeHit(Registry& registry, std::uint32_t stableEntityId)
    {
        ApplyHitReadback(registry, stableEntityId, std::nullopt);
    }

    void SelectionController::ConsumeNoHit(Registry& registry, std::uint64_t pickSequence)
    {
        ApplyNoHitReadback(registry, pickSequence);
    }

    void SelectionController::ConsumeNoHit(Registry& registry)
    {
        ApplyNoHitReadback(registry, std::nullopt);
    }

    // --- programmatic selection -------------------------------------------

    void SelectionController::ClearSelection(Registry& registry)
    {
        SetSelectionSet(registry, {});
    }

    bool SelectionController::SetSelectedEntity(Registry& registry, EntityHandle entity)
    {
        if (!IsValidEntity(registry.Raw(), entity))
            return false;

        SetSelectionSet(registry, {entity});
        return true;
    }

    bool SelectionController::SetSelectedByStableEntityId(Registry& registry, std::uint32_t stableEntityId)
    {
        return SetSelectedEntity(registry, ToEntityHandle(stableEntityId));
    }

    // --- snapshot / introspection accessors --------------------------------

    std::span<const std::uint32_t> SelectionController::SelectedStableIds() const noexcept
    {
        return std::span<const std::uint32_t>(m_SelectedStableIds.data(), m_SelectedStableIds.size());
    }

    bool SelectionController::HasHovered() const noexcept
    {
        return m_HasHovered;
    }

    std::uint32_t SelectionController::HoveredStableId() const noexcept
    {
        return m_HasHovered ? ToStableEntityId(m_Hovered) : 0u;
    }

    std::size_t SelectionController::SelectedCount() const noexcept
    {
        return m_Selected.size();
    }

    bool SelectionController::IsSelected(EntityHandle entity) const noexcept
    {
        return std::find(m_Selected.begin(), m_Selected.end(), entity) != m_Selected.end();
    }

    SelectionController::EntityHandle SelectionController::HoveredEntity() const noexcept
    {
        return m_Hovered;
    }

    std::size_t SelectionController::InFlightPickCount() const noexcept
    {
        return m_InFlightPicks.size();
    }

    std::optional<std::uint64_t> SelectionController::OldestInFlightSequence() const noexcept
    {
        if (m_InFlightPicks.empty())
            return std::nullopt;
        return m_InFlightPicks.front().Sequence;
    }

    const SelectionControllerDiagnostics& SelectionController::GetDiagnostics() const noexcept
    {
        return m_Diagnostics;
    }

    SelectionControllerConfig& SelectionController::GetConfig() noexcept
    {
        return m_Config;
    }

    const SelectionControllerConfig& SelectionController::GetConfig() const noexcept
    {
        return m_Config;
    }
}
