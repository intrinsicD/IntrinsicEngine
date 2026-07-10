module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

module Extrinsic.Runtime.WorldRegistry;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    WorldRegistry::WorldRegistry() = default;
    WorldRegistry::~WorldRegistry() = default;

    WorldHandle WorldRegistry::CreateWorld(std::string debugName)
    {
        std::uint32_t index = 0u;
        if (!m_FreeList.empty())
        {
            index = m_FreeList.back();
            m_FreeList.pop_back();
            WorldRecord& record = m_Worlds[index];
            ++record.Generation;
            if (record.Generation == 0u)
                record.Generation = 1u;
        }
        else
        {
            index = static_cast<std::uint32_t>(m_Worlds.size());
            m_Worlds.emplace_back();
            if (index == DefaultWorldHandle.Index)
                m_Worlds.back().Generation = DefaultWorldHandle.Generation;
        }

        WorldRecord& record = m_Worlds[index];
        record.Lifecycle = WorldLifecycle::Live;
        record.Scene = std::make_unique<ECS::Scene::Registry>();
        record.DebugName = debugName.empty() ? MakeDefaultName(index) : std::move(debugName);
        record.DestroyAnnouncedEpoch = 0u;

        const WorldHandle handle = HandleForIndex(index);
        if (!m_ActiveWorld.IsValid())
            m_ActiveWorld = handle;

        return handle;
    }

    Core::Result WorldRegistry::RequestSetActiveWorld(WorldHandle world)
    {
        if (!Contains(world))
            return Core::Err(Core::ErrorCode::ResourceNotFound);

        if (world == m_ActiveWorld)
        {
            m_PendingActiveWorld.reset();
            return Core::Ok();
        }

        m_PendingActiveWorld = world;
        return Core::Ok();
    }

    Core::Result WorldRegistry::RequestDestroyWorld(WorldHandle world)
    {
        WorldRecord* record = Find(world);
        if (record == nullptr)
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        if (world == m_ActiveWorld)
            return Core::Err(Core::ErrorCode::ResourceBusy);

        if (record->Lifecycle == WorldLifecycle::DestroyPending ||
            record->Lifecycle == WorldLifecycle::DestroyAnnounced)
        {
            return Core::Ok();
        }

        record->Lifecycle = WorldLifecycle::DestroyPending;
        return Core::Ok();
    }

    WorldHandle WorldRegistry::ActiveWorld() const noexcept
    {
        return m_ActiveWorld;
    }

    ECS::Scene::Registry* WorldRegistry::Get(WorldHandle world) noexcept
    {
        WorldRecord* record = Find(world);
        return record != nullptr ? record->Scene.get() : nullptr;
    }

    const ECS::Scene::Registry* WorldRegistry::Get(WorldHandle world) const noexcept
    {
        const WorldRecord* record = Find(world);
        return record != nullptr ? record->Scene.get() : nullptr;
    }

    bool WorldRegistry::Contains(WorldHandle world) const noexcept
    {
        return Find(world) != nullptr;
    }

    std::uint32_t WorldRegistry::LiveWorldCount() const noexcept
    {
        return static_cast<std::uint32_t>(std::count_if(
            m_Worlds.begin(),
            m_Worlds.end(),
            [](const WorldRecord& record)
            {
                return record.Scene != nullptr &&
                       record.Lifecycle != WorldLifecycle::Empty;
            }));
    }

    std::string_view WorldRegistry::DebugName(WorldHandle world) const noexcept
    {
        const WorldRecord* record = Find(world);
        return record != nullptr ? std::string_view{record->DebugName}
                                 : std::string_view{};
    }

    WorldRegistryMaintenanceStats WorldRegistry::ApplyMaintenance(
        KernelEventBus& events,
        JobService& jobs)
    {
        ++m_MaintenanceEpoch;

        WorldRegistryMaintenanceStats stats{};
        stats.ActiveWorld = m_ActiveWorld;

        if (m_PendingActiveWorld.has_value())
        {
            const WorldHandle requested = *m_PendingActiveWorld;
            m_PendingActiveWorld.reset();
            if (Contains(requested) && requested != m_ActiveWorld)
            {
                const WorldHandle previous = m_ActiveWorld;
                const std::string previousName{DebugName(previous)};
                const std::string currentName{DebugName(requested)};
                m_ActiveWorld = requested;
                ++stats.AppliedActiveWorldChanges;
                events.Publish(ActiveWorldChanged{
                    .Previous = previous,
                    .Current = requested,
                    .PreviousDebugName = previousName,
                    .CurrentDebugName = currentName,
                });
            }
        }

        for (std::uint32_t index = 0u; index < m_Worlds.size(); ++index)
        {
            WorldRecord& record = m_Worlds[index];
            if (record.Lifecycle == WorldLifecycle::DestroyPending)
            {
                const WorldHandle handle = HandleForIndex(index);
                record.Lifecycle = WorldLifecycle::DestroyAnnounced;
                record.DestroyAnnouncedEpoch = m_MaintenanceEpoch;
                ++stats.DestroyAnnouncements;
                events.Publish(WorldWillBeDestroyed{
                    .World = handle,
                    .DebugName = record.DebugName,
                });
                stats.CancelledJobs += jobs.CancelAllForWorld(handle);
                continue;
            }

            if (record.Lifecycle != WorldLifecycle::DestroyAnnounced ||
                record.DestroyAnnouncedEpoch >= m_MaintenanceEpoch)
            {
                continue;
            }

            const WorldHandle handle = HandleForIndex(index);
            if (handle == m_ActiveWorld)
                continue;

            record.Scene.reset();
            record.DebugName.clear();
            record.Lifecycle = WorldLifecycle::Empty;
            record.DestroyAnnouncedEpoch = 0u;
            m_FreeList.push_back(index);
            ++stats.DestroyedWorlds;
        }

        stats.ActiveWorld = m_ActiveWorld;
        stats.LiveWorlds = LiveWorldCount();
        return stats;
    }

    void WorldRegistry::Clear() noexcept
    {
        m_PendingActiveWorld.reset();
        m_ActiveWorld = {};
        m_FreeList.clear();
        m_Worlds.clear();
        m_MaintenanceEpoch = 0u;
    }

    WorldRegistry::WorldRecord* WorldRegistry::Find(WorldHandle world) noexcept
    {
        if (!world.IsValid() || world.Index >= m_Worlds.size())
            return nullptr;

        WorldRecord& record = m_Worlds[world.Index];
        if (record.Generation != world.Generation ||
            record.Lifecycle == WorldLifecycle::Empty ||
            record.Scene == nullptr)
        {
            return nullptr;
        }
        return &record;
    }

    const WorldRegistry::WorldRecord* WorldRegistry::Find(WorldHandle world) const noexcept
    {
        if (!world.IsValid() || world.Index >= m_Worlds.size())
            return nullptr;

        const WorldRecord& record = m_Worlds[world.Index];
        if (record.Generation != world.Generation ||
            record.Lifecycle == WorldLifecycle::Empty ||
            record.Scene == nullptr)
        {
            return nullptr;
        }
        return &record;
    }

    WorldHandle WorldRegistry::HandleForIndex(const std::uint32_t index) const noexcept
    {
        if (index >= m_Worlds.size())
            return {};
        return WorldHandle{index, m_Worlds[index].Generation};
    }

    std::string WorldRegistry::MakeDefaultName(const std::uint32_t index) const
    {
        return "World " + std::to_string(index);
    }
}
