// ARCH-022: a Vulkan engine composed as the Sandbox composes it, with one plugin harness (PluginHarness.hpp): the
// scenario's step runs once per frame (after the shell prepares its workspace) until it answers true or 90 s pass.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include "PluginHarness.hpp"

import Extrinsic.Runtime.EngineConfigBoot;

namespace Intrinsic::Tests::Plugins
{
    class GpuScenario final : public RuntimeTestModule
    {
    public:
        std::function<bool()> Step{};
        Harness* Owner{nullptr};
        bool Done{false}, TimedOut{false};

    protected:
        void Frame(double, double) override
        {
            if (Started == std::chrono::steady_clock::time_point{}) Started = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(90))
            {
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if (Owner == nullptr || !Step || !Kernel().GetDevice().IsOperational()) return;
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Owner->Shell); // as the Sandbox shell does every frame
            if (Step())
            {
                Done = true;
                Kernel().RequestExit();
            }
        }

    private:
        std::chrono::steady_clock::time_point Started{};
    };

    inline Config::EngineConfig VulkanConfig()
    {
        auto config = R::CreateReferenceEngineConfig();
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return config;
    }

    // `PluginHarness` is the plugin's harness type, constructible from (config, application).
    template <class PluginHarness>
    struct Gpu
    {
        std::unique_ptr<PluginHarness> H{};
        GpuScenario* App{nullptr};

        Gpu()
        {
            auto app = std::make_unique<GpuScenario>();
            App = app.get();
            H = std::make_unique<PluginHarness>(VulkanConfig(), std::move(app));
            App->Owner = H.get();
        }
        bool Run(std::function<bool()> step)
        {
            App->Step = std::move(step);
            App->Done = false;
            H->Engine.Run();
            return H->Engine.GetDevice().IsOperational() && !App->TimedOut && App->Done;
        }
    };
}
