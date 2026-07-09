module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.Module;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    export enum class FramePhase : std::uint8_t
    {
        AfterCommandDrain,
        UiBuild,
        BeforeExtraction,
        Maintenance,
    };

    export struct RuntimeShutdownAnnounced
    {
    };

    export struct RuntimeFrameHookContext
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

    export using RuntimeFrameHook =
        std::function<void(RuntimeFrameHookContext&)>;

    export struct SimSystemContext
    {
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

    export struct SimSystemDesc
    {
        std::string Name{};
        Core::FrameGraphPassOptions Options{
            .MainThreadOnly = true,
            .AllowParallel = false,
            .DebugCategory = "RuntimeModule",
        };
        std::vector<Core::Hash::StringID> WaitForSignals{};
        std::vector<Core::Hash::StringID> SignalLabels{};
        std::function<void(Core::FrameGraphBuilder&)> Setup{};
        std::function<void(SimSystemContext&)> Execute{};
    };

    export struct RuntimeModuleShutdownContext
    {
        CommandBus& Commands;
        KernelEventBus& Events;
        JobService& Jobs;
        WorldRegistry& Worlds;
        ServiceRegistry& Services;
    };

    export class EngineSetup
    {
    public:
        using SimSystemRegistrar = std::function<void(SimSystemDesc)>;
        using FrameHookRegistrar =
            std::function<void(FramePhase, RuntimeFrameHook)>;

        EngineSetup(CommandBus& commands,
                    KernelEventBus& events,
                    JobService& jobs,
                    WorldRegistry& worlds,
                    ServiceRegistry& services,
                    SimSystemRegistrar simSystemRegistrar,
                    FrameHookRegistrar frameHookRegistrar)
            : m_Commands(commands)
            , m_Events(events)
            , m_Jobs(jobs)
            , m_Worlds(worlds)
            , m_Services(services)
            , m_SimSystemRegistrar(std::move(simSystemRegistrar))
            , m_FrameHookRegistrar(std::move(frameHookRegistrar))
        {
        }

        [[nodiscard]] CommandBus& Commands() noexcept { return m_Commands; }
        [[nodiscard]] KernelEventBus& Events() noexcept { return m_Events; }
        [[nodiscard]] JobService& Jobs() noexcept { return m_Jobs; }
        [[nodiscard]] WorldRegistry& Worlds() noexcept { return m_Worlds; }
        [[nodiscard]] ServiceRegistry& Services() noexcept { return m_Services; }

        template <typename TCommand>
        void RegisterCommandHandler(
            std::function<CommandOutcome(CommandContext&, const TCommand&)> handler)
        {
            m_Commands.RegisterHandler<TCommand>(std::move(handler));
        }

        template <typename TEvent>
        [[nodiscard]] KernelEventSubscription Subscribe(
            std::function<void(const TEvent&)> listener)
        {
            return m_Events.Subscribe<TEvent>(std::move(listener));
        }

        [[nodiscard]] Core::Result RegisterSimSystem(SimSystemDesc desc)
        {
            if (desc.Name.empty() || !desc.Execute)
                return Core::Err(Core::ErrorCode::InvalidArgument);
            if (!m_SimSystemRegistrar)
                return Core::Err(Core::ErrorCode::InvalidState);
            m_SimSystemRegistrar(std::move(desc));
            return Core::Ok();
        }

        [[nodiscard]] Core::Result RegisterFrameHook(
            FramePhase phase,
            RuntimeFrameHook hook)
        {
            if (!hook)
                return Core::Err(Core::ErrorCode::InvalidArgument);
            if (!m_FrameHookRegistrar)
                return Core::Err(Core::ErrorCode::InvalidState);
            m_FrameHookRegistrar(phase, std::move(hook));
            return Core::Ok();
        }

    private:
        CommandBus& m_Commands;
        KernelEventBus& m_Events;
        JobService& m_Jobs;
        WorldRegistry& m_Worlds;
        ServiceRegistry& m_Services;
        SimSystemRegistrar m_SimSystemRegistrar{};
        FrameHookRegistrar m_FrameHookRegistrar{};
    };

    export class IRuntimeModule
    {
    public:
        virtual ~IRuntimeModule() = default;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual Core::Result OnRegister(EngineSetup& setup) = 0;
        [[nodiscard]] virtual Core::Result OnResolve(EngineSetup& setup) = 0;
        virtual void OnShutdown(RuntimeModuleShutdownContext& context) = 0;
    };
}
