#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.Module;

namespace Intrinsic::Tests
{
    // Test-only bridge used while production applications compose ordinary
    // runtime modules directly. It maps a fixture's resolve/frame/
    // shutdown behavior onto IRuntimeModule without restoring a production
    // application callback interface.
    class RuntimeTestModule : public Extrinsic::Runtime::IRuntimeModule
    {
    public:
        void BindKernel(Extrinsic::Runtime::Engine& kernel) noexcept { m_Kernel = &kernel; }

        void ResolveForComposition() { Resolve(); }
        void FrameForComposition(double alpha, double dt) { Frame(alpha, dt); }
        void ShutdownForComposition()
        {
            if (m_ShutdownInvoked)
                return;
            m_ShutdownInvoked = true;
            Shutdown();
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            // Preserve the former application variable-tick position ahead of
            // ordinary production UiBuild hooks. This ordering is confined to the
            // test bridge; production composition has no privileged app callback.
            return "Application.RuntimeTestModule";
        }

        [[nodiscard]] Extrinsic::Core::Result
        OnRegister(Extrinsic::Runtime::EngineSetup& setup) override
        {
            m_ShutdownInvoked = false;
            return setup.RegisterFrameHook(
                Extrinsic::Runtime::FramePhase::UiBuild,
                [this](Extrinsic::Runtime::RuntimeFrameHookContext& context)
                { Frame(context.FixedStepAlpha, context.FrameDeltaSeconds); });
        }

        [[nodiscard]] Extrinsic::Core::Result OnResolve(Extrinsic::Runtime::EngineSetup&) override
        {
            return Extrinsic::Core::Ok();
        }

        void OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&) override
        {
            ShutdownForComposition();
        }

    protected:
        [[nodiscard]] Extrinsic::Runtime::Engine& Kernel() noexcept { return *m_Kernel; }

        virtual void Resolve() {}
        virtual void Frame(double, double) {}
        virtual void Shutdown() {}

    private:
        Extrinsic::Runtime::Engine* m_Kernel{};
        bool m_ShutdownInvoked{false};
    };

    // The removed application callback initialized after every runtime module had
    // completed OnResolve. Keep that timing in fixtures without moving their
    // UiBuild hook behind production UI hooks: this companion sorts last for boot
    // resolution and invokes the test-owned initialization callback. Reverse
    // module shutdown reaches it first, so concrete fixture state is also released
    // while production module services are still live.
    class RuntimeTestResolveModule final : public Extrinsic::Runtime::IRuntimeModule
    {
    public:
        explicit RuntimeTestResolveModule(RuntimeTestModule& target) noexcept
            : m_Target(target)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "~Application.RuntimeTestResolveModule";
        }

        [[nodiscard]] Extrinsic::Core::Result OnRegister(Extrinsic::Runtime::EngineSetup&) override
        {
            return Extrinsic::Core::Ok();
        }

        [[nodiscard]] Extrinsic::Core::Result OnResolve(Extrinsic::Runtime::EngineSetup&) override
        {
            m_Target.ResolveForComposition();
            return Extrinsic::Core::Ok();
        }

        void OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&) override
        {
            m_Target.ShutdownForComposition();
        }

    private:
        RuntimeTestModule& m_Target;
    };

    template <typename TModule>
    TModule& AddRuntimeTestModule(Extrinsic::Runtime::Engine& kernel,
                                  std::unique_ptr<TModule> module)
    {
        module->BindKernel(kernel);
        TModule& ref = *module;
        kernel.AddModule(std::move(module));
        kernel.AddModule(std::make_unique<RuntimeTestResolveModule>(ref));
        return ref;
    }

    // Stack-only test kernel convenience: preserves concise fixture setup while
    // the supplied behavior is installed as an ordinary test runtime module.
    class RuntimeTestKernel final : public Extrinsic::Runtime::Engine
    {
    public:
        explicit RuntimeTestKernel(Extrinsic::Core::Config::EngineConfig config)
            : Engine(std::move(config))
        {
        }

        ~RuntimeTestKernel() { Shutdown(); }

        template <typename TModule>
        RuntimeTestKernel(Extrinsic::Core::Config::EngineConfig config,
                          std::unique_ptr<TModule> module)
            : Engine(std::move(config))
        {
            m_LifecycleModule = &AddRuntimeTestModule(*this, std::move(module));
        }

        void Shutdown()
        {
            // Match the removed application lifecycle exactly: announce and quiesce
            // first, tear down concrete test-owned state while module services remain
            // live, then let Engine reverse-shutdown the ordinary runtime modules.
            BeginShutdown();
            if (m_LifecycleModule != nullptr)
                m_LifecycleModule->ShutdownForComposition();
            Engine::Shutdown();
        }

    private:
        RuntimeTestModule* m_LifecycleModule{};
    };
} // namespace Intrinsic::Tests
