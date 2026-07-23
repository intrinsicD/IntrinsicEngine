module;

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.ModuleSchedule;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    export struct RuntimeModuleFrameHookDispatchContext
    {
        FramePhase Phase{FramePhase::UiBegin};
        ECS::Scene::Registry& ActiveWorld;
        WorldHandle ActiveWorldHandle{};
        CommandBus& Commands;
        KernelEventBus& Events;
        JobService& Jobs;
        WorldRegistry& Worlds;
        ServiceRegistry& Services;
        EditorInputCaptureSnapshot& EditorCapture;
        RuntimeFramePacingDiagnostics& Pacing;
        std::uint64_t FrameIndex{0};
        double FrameDeltaSeconds{0.0};
        double FixedStepAlpha{0.0};
    };

    export class RuntimeModuleSchedule
    {
    public:
        void Clear();
        void RegisterFrameHook(std::string moduleName,
                               FramePhase phase,
                               RuntimeFrameHook hook);
        void RegisterViewportInputHook(
            std::string moduleName,
            RuntimeViewportInputHook hook);
        void FinalizeForBoot();
        void RunFrameHooks(
            RuntimeModuleFrameHookDispatchContext context) const;
        void RunViewportInputHooks(
            RuntimeViewportInputHookContext context) const;

    private:
        struct RuntimeModuleFrameHookRecord
        {
            std::string ModuleName{};
            FramePhase Phase{FramePhase::UiBegin};
            RuntimeFrameHook Hook{};
            std::uint64_t Sequence{0};
        };

        struct RuntimeModuleViewportInputHookRecord
        {
            std::string ModuleName{};
            RuntimeViewportInputHook Hook{};
            std::uint64_t Sequence{0};
        };

        std::vector<RuntimeModuleFrameHookRecord> m_FrameHooks{};
        std::vector<RuntimeModuleViewportInputHookRecord>
            m_ViewportInputHooks{};
        std::uint64_t m_NextRegistrationSequence{0};

        [[nodiscard]] static bool FrameHookLess(
            const RuntimeModuleFrameHookRecord& lhs,
            const RuntimeModuleFrameHookRecord& rhs);
        [[nodiscard]] static bool ViewportInputHookLess(
            const RuntimeModuleViewportInputHookRecord& lhs,
            const RuntimeModuleViewportInputHookRecord& rhs);
    };
}
