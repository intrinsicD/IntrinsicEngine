// Test-only runtime modules and stack kernel preserve fixture lifecycle ordering
// while sharing compiled registration and shutdown behavior across test targets.
#pragma once

#include <memory>
#include <string_view>
#include <utility>

import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.Module;

namespace Intrinsic::Tests
{
    class RuntimeTestModule : public Extrinsic::Runtime::IRuntimeModule
    {
    public:
        void BindKernel(Extrinsic::Runtime::Engine& kernel) noexcept { m_Kernel = &kernel; }

        void ResolveForComposition() { Resolve(); }
        void FrameForComposition(double alpha, double dt) { Frame(alpha, dt); }
        void ShutdownForComposition();

        [[nodiscard]] std::string_view Name() const noexcept override;

        [[nodiscard]] Extrinsic::Core::Result
        OnRegister(Extrinsic::Runtime::EngineSetup& setup) override;

        [[nodiscard]] Extrinsic::Core::Result OnResolve(Extrinsic::Runtime::EngineSetup&) override;

        void OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&) override;

    protected:
        [[nodiscard]] Extrinsic::Runtime::Engine& Kernel() noexcept { return *m_Kernel; }

        virtual void Resolve() {}
        virtual void Frame(double, double) {}
        virtual void Shutdown() {}

    private:
        Extrinsic::Runtime::Engine* m_Kernel{};
        bool m_ShutdownInvoked{false};
    };

    class RuntimeTestResolveModule final : public Extrinsic::Runtime::IRuntimeModule
    {
    public:
        explicit RuntimeTestResolveModule(RuntimeTestModule& target) noexcept;

        [[nodiscard]] std::string_view Name() const noexcept override;

        [[nodiscard]] Extrinsic::Core::Result OnRegister(Extrinsic::Runtime::EngineSetup&) override;

        [[nodiscard]] Extrinsic::Core::Result OnResolve(Extrinsic::Runtime::EngineSetup&) override;

        void OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&) override;

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

    class RuntimeTestKernel final : public Extrinsic::Runtime::Engine
    {
    public:
        explicit RuntimeTestKernel(Extrinsic::Core::Config::EngineConfig config);

        ~RuntimeTestKernel();

        template <typename TModule>
        RuntimeTestKernel(Extrinsic::Core::Config::EngineConfig config,
                          std::unique_ptr<TModule> module)
            : Engine(std::move(config))
        {
            m_LifecycleModule = &AddRuntimeTestModule(*this, std::move(module));
        }

        void Shutdown();

    private:
        RuntimeTestModule* m_LifecycleModule{};
    };
} // namespace Intrinsic::Tests
