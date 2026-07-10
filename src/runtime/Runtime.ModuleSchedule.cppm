module;

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.ModuleSchedule;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    export struct RuntimeModuleSimSystemScheduleContext
    {
        Core::FrameGraph& Graph;
        ECS::Scene::Registry& ActiveWorld;
        WorldHandle ActiveWorldHandle{};
        CommandBus& Commands;
        KernelEventBus& Events;
        JobService& Jobs;
        WorldRegistry& Worlds;
        ServiceRegistry& Services;
        std::uint64_t FrameIndex{0};
        double FixedDeltaSeconds{0.0};
    };

    export struct RuntimeModuleFrameHookDispatchContext
    {
        FramePhase Phase{FramePhase::AfterCommandDrain};
        ECS::Scene::Registry& ActiveWorld;
        WorldHandle ActiveWorldHandle{};
        CommandBus& Commands;
        KernelEventBus& Events;
        JobService& Jobs;
        WorldRegistry& Worlds;
        ServiceRegistry& Services;
        std::uint64_t FrameIndex{0};
        double FrameDeltaSeconds{0.0};
        double FixedStepAlpha{0.0};
    };

    export class RuntimeModuleSchedule
    {
    public:
        void Clear();
        void RegisterSimSystem(std::string moduleName, SimSystemDesc desc);
        void RegisterFrameHook(std::string moduleName,
                               FramePhase phase,
                               RuntimeFrameHook hook);
        [[nodiscard]] Core::Result FinalizeForBoot();
        void RegisterSimSystemsForTick(
            RuntimeModuleSimSystemScheduleContext context) const;
        void RunFrameHooks(
            RuntimeModuleFrameHookDispatchContext context) const;

    private:
        struct RuntimeModuleSimSystemRecord
        {
            std::string ModuleName{};
            SimSystemDesc Desc{};
            std::uint64_t Sequence{0};
        };

        struct RuntimeModuleFrameHookRecord
        {
            std::string ModuleName{};
            FramePhase Phase{FramePhase::AfterCommandDrain};
            RuntimeFrameHook Hook{};
            std::uint64_t Sequence{0};
        };

        std::vector<RuntimeModuleSimSystemRecord> m_SimSystems{};
        std::vector<RuntimeModuleFrameHookRecord> m_FrameHooks{};
        std::uint64_t m_NextRegistrationSequence{0};

        [[nodiscard]] static bool SimSystemLess(
            const RuntimeModuleSimSystemRecord& lhs,
            const RuntimeModuleSimSystemRecord& rhs);
        [[nodiscard]] static bool FrameHookLess(
            const RuntimeModuleFrameHookRecord& lhs,
            const RuntimeModuleFrameHookRecord& rhs);
    };
}
