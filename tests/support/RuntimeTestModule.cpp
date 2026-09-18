#include "RuntimeTestModule.hpp"

namespace Intrinsic::Tests
{
    void RuntimeTestModule::ShutdownForComposition()
    {
        if (m_ShutdownInvoked)
            return;
        m_ShutdownInvoked = true;
        Shutdown();
    }

    std::string_view RuntimeTestModule::Name() const noexcept
    {
        // Fixtures tick before ordinary production UiBuild hooks.
        return "Application.RuntimeTestModule";
    }

    Extrinsic::Core::Result RuntimeTestModule::OnRegister(Extrinsic::Runtime::EngineSetup& setup)
    {
        m_ShutdownInvoked = false;
        return setup.RegisterFrameHook(
            Extrinsic::Runtime::FramePhase::UiBuild,
            [this](Extrinsic::Runtime::RuntimeFrameHookContext& context)
            { Frame(context.FixedStepAlpha, context.FrameDeltaSeconds); });
    }

    Extrinsic::Core::Result RuntimeTestModule::OnResolve(Extrinsic::Runtime::EngineSetup&)
    {
        return Extrinsic::Core::Ok();
    }

    void RuntimeTestModule::OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&)
    {
        ShutdownForComposition();
    }

    RuntimeTestResolveModule::RuntimeTestResolveModule(RuntimeTestModule& target) noexcept
        : m_Target(target)
    {
    }

    std::string_view RuntimeTestResolveModule::Name() const noexcept
    {
        // Resolve fixtures after production modules; reverse shutdown releases
        // fixture state first while production services are still live.
        return "~Application.RuntimeTestResolveModule";
    }

    Extrinsic::Core::Result RuntimeTestResolveModule::OnRegister(Extrinsic::Runtime::EngineSetup&)
    {
        return Extrinsic::Core::Ok();
    }

    Extrinsic::Core::Result RuntimeTestResolveModule::OnResolve(Extrinsic::Runtime::EngineSetup&)
    {
        m_Target.ResolveForComposition();
        return Extrinsic::Core::Ok();
    }

    void RuntimeTestResolveModule::OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&)
    {
        m_Target.ShutdownForComposition();
    }

    RuntimeTestKernel::RuntimeTestKernel(Extrinsic::Core::Config::EngineConfig config)
        : Engine(std::move(config))
    {
    }

    RuntimeTestKernel::~RuntimeTestKernel()
    {
        Shutdown();
    }

    void RuntimeTestKernel::Shutdown()
    {
        // Quiesce before releasing fixture state, then reverse-shutdown modules.
        BeginShutdown();
        if (m_LifecycleModule != nullptr)
            m_LifecycleModule->ShutdownForComposition();
        Engine::Shutdown();
    }
}
