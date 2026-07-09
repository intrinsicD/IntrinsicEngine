module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.WorldRegistry;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export struct WorldWillBeDestroyed
    {
        WorldHandle World{};
        std::string DebugName{};
    };

    export struct ActiveWorldChanged
    {
        WorldHandle Previous{};
        WorldHandle Current{};
        std::string PreviousDebugName{};
        std::string CurrentDebugName{};
    };

    export struct WorldRegistryMaintenanceStats
    {
        WorldHandle ActiveWorld{};
        std::uint32_t LiveWorlds{0};
        std::uint32_t AppliedActiveWorldChanges{0};
        std::uint32_t DestroyAnnouncements{0};
        std::uint32_t DestroyedWorlds{0};
        std::uint64_t CancelledJobs{0};
    };

    export class WorldRegistry
    {
    public:
        WorldRegistry();
        ~WorldRegistry();

        WorldRegistry(const WorldRegistry&) = delete;
        WorldRegistry& operator=(const WorldRegistry&) = delete;

        [[nodiscard]] WorldHandle CreateWorld(std::string debugName = {});

        [[nodiscard]] Core::Result RequestSetActiveWorld(WorldHandle world);
        [[nodiscard]] Core::Result RequestDestroyWorld(WorldHandle world);

        [[nodiscard]] WorldHandle ActiveWorld() const noexcept;
        [[nodiscard]] ECS::Scene::Registry* Get(WorldHandle world) noexcept;
        [[nodiscard]] const ECS::Scene::Registry* Get(WorldHandle world) const noexcept;
        [[nodiscard]] bool Contains(WorldHandle world) const noexcept;
        [[nodiscard]] std::uint32_t LiveWorldCount() const noexcept;
        [[nodiscard]] std::string_view DebugName(WorldHandle world) const noexcept;

        [[nodiscard]] WorldRegistryMaintenanceStats ApplyMaintenance(
            KernelEventBus& events,
            JobService& jobs);

        void Clear() noexcept;

    private:
        enum class WorldLifecycle : std::uint8_t
        {
            Empty,
            Live,
            DestroyPending,
            DestroyAnnounced,
        };

        struct WorldRecord
        {
            std::uint32_t Generation{1u};
            WorldLifecycle Lifecycle{WorldLifecycle::Empty};
            std::unique_ptr<ECS::Scene::Registry> Scene{};
            std::string DebugName{};
            std::uint64_t DestroyAnnouncedEpoch{0u};
        };

        [[nodiscard]] WorldRecord* Find(WorldHandle world) noexcept;
        [[nodiscard]] const WorldRecord* Find(WorldHandle world) const noexcept;
        [[nodiscard]] WorldHandle HandleForIndex(std::uint32_t index) const noexcept;
        [[nodiscard]] std::string MakeDefaultName(std::uint32_t index) const;

        std::vector<WorldRecord> m_Worlds{};
        std::vector<std::uint32_t> m_FreeList{};
        std::optional<WorldHandle> m_PendingActiveWorld{};
        WorldHandle m_ActiveWorld{};
        std::uint64_t m_MaintenanceEpoch{0u};
    };
}
