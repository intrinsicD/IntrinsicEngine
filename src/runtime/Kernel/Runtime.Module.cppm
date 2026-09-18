// Runtime setup capabilities and frame hooks used by composition implementations.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

export module Extrinsic.Runtime.Module;
export import Extrinsic.Runtime.ModuleLifecycle;

export import Extrinsic.Runtime.RenderRecipeActivation;

import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;

extern "C++"
{
    namespace Extrinsic::ECS::Scene { class Registry; }
    namespace Extrinsic::Runtime { class WorldRegistry; }
}

namespace Extrinsic::Runtime
{
    // C++ language linkage lets the kernel name this phase and the capture
    // snapshot below through matching forward declarations, so Engine's
    // interface does not have to import this composition module.
    export extern "C++"
    {
    enum class FramePhase : std::uint8_t
    {
        UiBegin,
        UiBuild,
        UiEndCapture,
        BeforeExtraction,
        Maintenance,
        // App-composed simulation modules run after the promoted ECS fixed-step
        // bundle and before post-simulation event delivery. Appended to keep the
        // existing phase values stable while placing execution explicitly in
        // Engine::RunFrame rather than relying on enum order.
        Simulation,
    };
    }

    export struct RuntimeShutdownAnnounced
    {
    };

    export extern "C++"
    {
    struct EditorInputCaptureSnapshot
    {
        bool CapturedKeyboard{false};
        bool CapturedMouse{false};
        bool WidgetsActive{false};

        [[nodiscard]] bool CapturesViewportInput() const noexcept
        {
            return CapturedKeyboard || CapturedMouse || WidgetsActive;
        }
    };
    }

    export struct RuntimeFrameHookContext
    {
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

    export using RuntimeFrameHook =
        std::function<void(RuntimeFrameHookContext&)>;

    export struct RuntimeViewportInputHookContext
    {
        const Core::Config::EngineConfig& Config;
        WorldHandle ActiveWorldHandle{};
        const Platform::Input::Context& Input;
        Core::Extent2D Viewport{};
        const EditorInputCaptureSnapshot& EditorCapture;
        Graphics::RenderFrameInput& RenderInput;
        double FrameDeltaSeconds{0.0};
    };

    export using RuntimeViewportInputHook =
        std::function<void(RuntimeViewportInputHookContext&)>;

    export extern "C++"
    {
    struct RuntimeModuleShutdownContext
    {
        CommandBus& Commands;
        KernelEventBus& Events;
        JobService& Jobs;
        WorldRegistry& Worlds;
        ServiceRegistry& Services;
    };
    }

    export extern "C++"
    {
    class EngineSetup
    {
    public:
        using FrameHookRegistrar =
            std::function<void(FramePhase, RuntimeFrameHook)>;
        using ViewportInputHookRegistrar =
            std::function<void(RuntimeViewportInputHook)>;

        EngineSetup(CommandBus& commands,
                    KernelEventBus& events,
                    JobService& jobs,
                    WorldRegistry& worlds,
                    ServiceRegistry& services,
                    FrameHookRegistrar frameHookRegistrar,
                    RuntimeRenderRecipeActivationKernel
                        renderRecipeActivation = {},
                    ViewportInputHookRegistrar
                        viewportInputHookRegistrar = {},
                    const bool* initializedState = nullptr);

        [[nodiscard]] CommandBus& Commands() noexcept { return m_Commands; }
        [[nodiscard]] KernelEventBus& Events() noexcept { return m_Events; }
        [[nodiscard]] JobService& Jobs() noexcept { return m_Jobs; }
        [[nodiscard]] WorldRegistry& Worlds() noexcept { return m_Worlds; }
        [[nodiscard]] ServiceRegistry& Services() noexcept { return m_Services; }
        [[nodiscard]] const RuntimeRenderRecipeActivationKernel&
        RenderRecipeActivation() const noexcept
        {
            return m_RenderRecipeActivation;
        }
        [[nodiscard]] const bool* InitializedState() const noexcept
        {
            return m_InitializedState;
        }

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

        [[nodiscard]] Core::Result RegisterFrameHook(
            FramePhase phase,
            RuntimeFrameHook hook);

        [[nodiscard]] Core::Result RegisterViewportInputHook(
            RuntimeViewportInputHook hook);

    private:
        CommandBus& m_Commands;
        KernelEventBus& m_Events;
        JobService& m_Jobs;
        WorldRegistry& m_Worlds;
        ServiceRegistry& m_Services;
        FrameHookRegistrar m_FrameHookRegistrar{};
        RuntimeRenderRecipeActivationKernel m_RenderRecipeActivation{};
        ViewportInputHookRegistrar m_ViewportInputHookRegistrar{};
        const bool* m_InitializedState{};
    };
    }

}
