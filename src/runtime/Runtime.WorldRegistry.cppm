module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.WorldRegistry;

export import Extrinsic.Runtime.WorldHandle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;

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
        std::string DebugName{};
    };

    export struct WorldRegistryMaintenanceStats
    {
        std::uint32_t ActiveWorldChanges{0};
        std::uint32_t DestroyAnnouncedWorlds{0};
        std::uint32_t DestroyedWorlds{0};
        std::uint32_t CancelledJobs{0};
    };

    export class WorldRegistry
    {
    public:
        WorldRegistry();
        ~WorldRegistry();

        WorldRegistry(const WorldRegistry&) = delete;
        WorldRegistry& operator=(const WorldRegistry&) = delete;
        WorldRegistry(WorldRegistry&&) noexcept;
        WorldRegistry& operator=(WorldRegistry&&) noexcept;

        [[nodiscard]] WorldHandle CreateWorld(std::string debugName = {});
        [[nodiscard]] bool RequestDestroyWorld(WorldHandle world) noexcept;
        [[nodiscard]] bool RequestSetActiveWorld(WorldHandle world) noexcept;

        [[nodiscard]] WorldHandle ActiveWorld() const noexcept;
        [[nodiscard]] ECS::Scene::Registry* Get(WorldHandle world) noexcept;
        [[nodiscard]] const ECS::Scene::Registry* Get(WorldHandle world) const noexcept;
        [[nodiscard]] bool Contains(WorldHandle world) const noexcept;
        [[nodiscard]] bool IsDestroyAnnounced(WorldHandle world) const noexcept;
        [[nodiscard]] std::size_t WorldCount() const noexcept;
        [[nodiscard]] std::size_t PendingDestroyCount() const noexcept;

        [[nodiscard]] WorldRegistryMaintenanceStats ApplyDeferredOperations(
            EventBus& events,
            JobService& jobs);
        void Clear() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
