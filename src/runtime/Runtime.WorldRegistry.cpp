module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        enum class WorldLifecycleState : std::uint8_t
        {
            Alive,
            DestroyAnnounced,
            Destroyed,
        };
    }

    struct WorldRegistry::Impl
    {
        struct Record
        {
            std::uint32_t Generation{1u};
            WorldLifecycleState State{WorldLifecycleState::Alive};
            bool DestroyRequested{false};
            std::string DebugName{};
            std::unique_ptr<ECS::Scene::Registry> Scene{};
        };

        std::vector<Record> Worlds{};
        WorldHandle Active{};
        WorldHandle PendingActive{};

        [[nodiscard]] WorldHandle HandleForIndex(const std::size_t index) const noexcept
        {
            if (index >= Worlds.size() ||
                index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return {};
            }
            return WorldHandle{static_cast<std::uint32_t>(index), Worlds[index].Generation};
        }

        [[nodiscard]] Record* Resolve(WorldHandle world) noexcept
        {
            if (!world.IsValid() || world.Index >= Worlds.size())
            {
                return nullptr;
            }

            Record& record = Worlds[world.Index];
            if (record.Generation != world.Generation ||
                record.State == WorldLifecycleState::Destroyed ||
                record.Scene == nullptr)
            {
                return nullptr;
            }
            return &record;
        }

        [[nodiscard]] const Record* Resolve(WorldHandle world) const noexcept
        {
            if (!world.IsValid() || world.Index >= Worlds.size())
            {
                return nullptr;
            }

            const Record& record = Worlds[world.Index];
            if (record.Generation != world.Generation ||
                record.State == WorldLifecycleState::Destroyed ||
                record.Scene == nullptr)
            {
                return nullptr;
            }
            return &record;
        }

        [[nodiscard]] std::size_t CountSurvivingWorldsExcept(
            WorldHandle world) const noexcept
        {
            std::size_t count = 0;
            for (std::size_t i = 0; i < Worlds.size(); ++i)
            {
                const Record& record = Worlds[i];
                if (record.State == WorldLifecycleState::Destroyed ||
                    record.Scene == nullptr ||
                    record.DestroyRequested ||
                    HandleForIndex(i) == world)
                {
                    continue;
                }
                ++count;
            }
            return count;
        }

        [[nodiscard]] WorldHandle FindFirstLiveWorldExcept(
            WorldHandle world) const noexcept
        {
            for (std::size_t i = 0; i < Worlds.size(); ++i)
            {
                const Record& record = Worlds[i];
                if (record.State != WorldLifecycleState::Alive ||
                    record.Scene == nullptr ||
                    record.DestroyRequested)
                {
                    continue;
                }

                const WorldHandle candidate = HandleForIndex(i);
                if (candidate != world)
                {
                    return candidate;
                }
            }
            return {};
        }

        [[nodiscard]] std::string DebugNameOf(WorldHandle world) const
        {
            if (const Record* record = Resolve(world))
            {
                return record->DebugName;
            }
            return {};
        }
    };

    WorldRegistry::WorldRegistry()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    WorldRegistry::~WorldRegistry() = default;
    WorldRegistry::WorldRegistry(WorldRegistry&&) noexcept = default;
    WorldRegistry& WorldRegistry::operator=(WorldRegistry&&) noexcept = default;

    WorldHandle WorldRegistry::CreateWorld(std::string debugName)
    {
        if (m_Impl == nullptr ||
            m_Impl->Worlds.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            return {};
        }

        const std::size_t index = m_Impl->Worlds.size();
        if (debugName.empty())
        {
            debugName = "world-" + std::to_string(index);
        }

        m_Impl->Worlds.push_back(Impl::Record{
            .Generation = 1u,
            .State = WorldLifecycleState::Alive,
            .DestroyRequested = false,
            .DebugName = std::move(debugName),
            .Scene = std::make_unique<ECS::Scene::Registry>()});

        const WorldHandle handle = m_Impl->HandleForIndex(index);
        if (!m_Impl->Active.IsValid())
        {
            m_Impl->Active = handle;
        }
        return handle;
    }

    bool WorldRegistry::RequestDestroyWorld(WorldHandle world) noexcept
    {
        if (m_Impl == nullptr)
        {
            return false;
        }

        Impl::Record* record = m_Impl->Resolve(world);
        if (record == nullptr || record->DestroyRequested)
        {
            return record != nullptr;
        }

        if (m_Impl->CountSurvivingWorldsExcept(world) == 0)
        {
            return false;
        }

        record->DestroyRequested = true;
        if (m_Impl->PendingActive == world)
        {
            m_Impl->PendingActive = {};
        }
        return true;
    }

    bool WorldRegistry::RequestSetActiveWorld(WorldHandle world) noexcept
    {
        if (m_Impl == nullptr)
        {
            return false;
        }

        const Impl::Record* record = m_Impl->Resolve(world);
        if (record == nullptr || record->DestroyRequested)
        {
            return false;
        }

        m_Impl->PendingActive = world;
        return true;
    }

    WorldHandle WorldRegistry::ActiveWorld() const noexcept
    {
        return m_Impl != nullptr ? m_Impl->Active : WorldHandle{};
    }

    ECS::Scene::Registry* WorldRegistry::Get(WorldHandle world) noexcept
    {
        if (m_Impl == nullptr)
        {
            return nullptr;
        }

        Impl::Record* record = m_Impl->Resolve(world);
        return record != nullptr ? record->Scene.get() : nullptr;
    }

    const ECS::Scene::Registry* WorldRegistry::Get(WorldHandle world) const noexcept
    {
        if (m_Impl == nullptr)
        {
            return nullptr;
        }

        const Impl::Record* record = m_Impl->Resolve(world);
        return record != nullptr ? record->Scene.get() : nullptr;
    }

    bool WorldRegistry::Contains(WorldHandle world) const noexcept
    {
        return m_Impl != nullptr && m_Impl->Resolve(world) != nullptr;
    }

    bool WorldRegistry::IsDestroyAnnounced(WorldHandle world) const noexcept
    {
        if (m_Impl == nullptr)
        {
            return false;
        }

        const Impl::Record* record = m_Impl->Resolve(world);
        return record != nullptr &&
               record->State == WorldLifecycleState::DestroyAnnounced;
    }

    std::size_t WorldRegistry::WorldCount() const noexcept
    {
        if (m_Impl == nullptr)
        {
            return 0;
        }

        std::size_t count = 0;
        for (const Impl::Record& record : m_Impl->Worlds)
        {
            if (record.State != WorldLifecycleState::Destroyed &&
                record.Scene != nullptr)
            {
                ++count;
            }
        }
        return count;
    }

    std::size_t WorldRegistry::PendingDestroyCount() const noexcept
    {
        if (m_Impl == nullptr)
        {
            return 0;
        }

        return static_cast<std::size_t>(
            std::count_if(
                m_Impl->Worlds.begin(),
                m_Impl->Worlds.end(),
                [](const Impl::Record& record)
                {
                    return record.State != WorldLifecycleState::Destroyed &&
                           record.DestroyRequested;
                }));
    }

    WorldRegistryMaintenanceStats WorldRegistry::ApplyDeferredOperations(
        EventBus& events,
        JobService& jobs)
    {
        WorldRegistryMaintenanceStats stats{};
        if (m_Impl == nullptr)
        {
            return stats;
        }

        if (m_Impl->PendingActive.IsValid())
        {
            const WorldHandle requested = m_Impl->PendingActive;
            m_Impl->PendingActive = {};

            const Impl::Record* requestedRecord = m_Impl->Resolve(requested);
            if (requestedRecord != nullptr &&
                !requestedRecord->DestroyRequested &&
                requested != m_Impl->Active)
            {
                const WorldHandle previous = m_Impl->Active;
                m_Impl->Active = requested;
                events.Publish(ActiveWorldChanged{
                    .Previous = previous,
                    .Current = requested,
                    .DebugName = requestedRecord->DebugName});
                ++stats.ActiveWorldChanges;
            }
        }

        for (std::size_t i = 0; i < m_Impl->Worlds.size(); ++i)
        {
            Impl::Record& record = m_Impl->Worlds[i];
            if (record.State != WorldLifecycleState::DestroyAnnounced ||
                !record.DestroyRequested ||
                record.Scene == nullptr)
            {
                continue;
            }

            const WorldHandle handle = m_Impl->HandleForIndex(i);
            if (m_Impl->Active == handle)
            {
                const WorldHandle replacement =
                    m_Impl->FindFirstLiveWorldExcept(handle);
                if (!replacement.IsValid())
                {
                    continue;
                }

                const WorldHandle previous = m_Impl->Active;
                m_Impl->Active = replacement;
                events.Publish(ActiveWorldChanged{
                    .Previous = previous,
                    .Current = replacement,
                    .DebugName = m_Impl->DebugNameOf(replacement)});
                ++stats.ActiveWorldChanges;
            }

            record.Scene.reset();
            record.State = WorldLifecycleState::Destroyed;
            record.DestroyRequested = false;
            ++record.Generation;
            ++stats.DestroyedWorlds;
        }

        for (std::size_t i = 0; i < m_Impl->Worlds.size(); ++i)
        {
            Impl::Record& record = m_Impl->Worlds[i];
            if (record.State != WorldLifecycleState::Alive ||
                !record.DestroyRequested ||
                record.Scene == nullptr)
            {
                continue;
            }

            const WorldHandle handle = m_Impl->HandleForIndex(i);
            events.Publish(WorldWillBeDestroyed{
                .World = handle,
                .DebugName = record.DebugName});
            stats.CancelledJobs += jobs.CancelAllForWorld(handle);
            record.State = WorldLifecycleState::DestroyAnnounced;
            ++stats.DestroyAnnouncedWorlds;
        }

        return stats;
    }

    void WorldRegistry::Clear() noexcept
    {
        if (m_Impl == nullptr)
        {
            return;
        }

        m_Impl->Worlds.clear();
        m_Impl->Active = {};
        m_Impl->PendingActive = {};
    }
}
