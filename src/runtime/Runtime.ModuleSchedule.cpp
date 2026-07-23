module;

#include <algorithm>
#include <string>
#include <utility>

module Extrinsic.Runtime.ModuleSchedule;

namespace Extrinsic::Runtime
{
    bool RuntimeModuleSchedule::FrameHookLess(
        const RuntimeModuleFrameHookRecord& lhs,
        const RuntimeModuleFrameHookRecord& rhs)
    {
        if (lhs.Phase != rhs.Phase)
            return lhs.Phase < rhs.Phase;
        if (lhs.ModuleName != rhs.ModuleName)
            return lhs.ModuleName < rhs.ModuleName;
        return lhs.Sequence < rhs.Sequence;
    }

    bool RuntimeModuleSchedule::ViewportInputHookLess(
        const RuntimeModuleViewportInputHookRecord& lhs,
        const RuntimeModuleViewportInputHookRecord& rhs)
    {
        if (lhs.ModuleName != rhs.ModuleName)
            return lhs.ModuleName < rhs.ModuleName;
        return lhs.Sequence < rhs.Sequence;
    }

    void RuntimeModuleSchedule::Clear()
    {
        m_FrameHooks.clear();
        m_ViewportInputHooks.clear();
        m_NextRegistrationSequence = 0;
    }

    void RuntimeModuleSchedule::RegisterFrameHook(
        std::string moduleName,
        const FramePhase phase,
        RuntimeFrameHook hook)
    {
        m_FrameHooks.push_back(RuntimeModuleFrameHookRecord{
            .ModuleName = std::move(moduleName),
            .Phase = phase,
            .Hook = std::move(hook),
            .Sequence = m_NextRegistrationSequence++,
        });
    }

    void RuntimeModuleSchedule::RegisterViewportInputHook(
        std::string moduleName,
        RuntimeViewportInputHook hook)
    {
        m_ViewportInputHooks.push_back(
            RuntimeModuleViewportInputHookRecord{
                .ModuleName = std::move(moduleName),
                .Hook = std::move(hook),
                .Sequence = m_NextRegistrationSequence++,
            });
    }

    void RuntimeModuleSchedule::FinalizeForBoot()
    {
        std::sort(m_FrameHooks.begin(),
                  m_FrameHooks.end(),
                  FrameHookLess);
        std::sort(m_ViewportInputHooks.begin(),
                  m_ViewportInputHooks.end(),
                  ViewportInputHookLess);
    }

    void RuntimeModuleSchedule::RunFrameHooks(
        RuntimeModuleFrameHookDispatchContext context) const
    {
        RuntimeFrameHookContext hookContext{
            .ActiveWorld = context.ActiveWorld,
            .ActiveWorldHandle = context.ActiveWorldHandle,
            .Commands = context.Commands,
            .Events = context.Events,
            .Jobs = context.Jobs,
            .Worlds = context.Worlds,
            .Services = context.Services,
            .EditorCapture = context.EditorCapture,
            .Pacing = context.Pacing,
            .FrameIndex = context.FrameIndex,
            .FrameDeltaSeconds = context.FrameDeltaSeconds,
            .FixedStepAlpha = context.FixedStepAlpha,
        };

        for (const RuntimeModuleFrameHookRecord& hook : m_FrameHooks)
        {
            if (hook.Phase == context.Phase && hook.Hook)
                hook.Hook(hookContext);
        }
    }

    void RuntimeModuleSchedule::RunViewportInputHooks(
        RuntimeViewportInputHookContext context) const
    {
        for (const RuntimeModuleViewportInputHookRecord& hook :
             m_ViewportInputHooks)
        {
            if (hook.Hook)
                hook.Hook(context);
        }
    }
}
