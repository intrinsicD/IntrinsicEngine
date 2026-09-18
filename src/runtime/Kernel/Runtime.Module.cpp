module;

#include <functional>
#include <utility>

module Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
    extern "C++"
    {
    EngineSetup::EngineSetup(
        CommandBus& commands,
        KernelEventBus& events,
        JobService& jobs,
        WorldRegistry& worlds,
        ServiceRegistry& services,
        FrameHookRegistrar frameHookRegistrar,
        RuntimeRenderRecipeActivationKernel renderRecipeActivation,
        ViewportInputHookRegistrar viewportInputHookRegistrar,
        const bool* initializedState)
        : m_Commands(commands)
        , m_Events(events)
        , m_Jobs(jobs)
        , m_Worlds(worlds)
        , m_Services(services)
        , m_FrameHookRegistrar(std::move(frameHookRegistrar))
        , m_RenderRecipeActivation(std::move(renderRecipeActivation))
        , m_ViewportInputHookRegistrar(std::move(viewportInputHookRegistrar))
        , m_InitializedState(initializedState)
    {
    }

    Core::Result EngineSetup::RegisterFrameHook(
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

    Core::Result EngineSetup::RegisterViewportInputHook(
        RuntimeViewportInputHook hook)
    {
        if (!hook)
            return Core::Err(Core::ErrorCode::InvalidArgument);
        if (!m_ViewportInputHookRegistrar)
            return Core::Err(Core::ErrorCode::InvalidState);
        m_ViewportInputHookRegistrar(std::move(hook));
        return Core::Ok();
    }
    }
}
