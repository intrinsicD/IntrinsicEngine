#include <memory>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;

// RUNTIME-070: contract surface around the
// `AssetWorkflowModule::OnRegister()` → fallback texture bootstrap
// bootstrap. These tests stay on the CPU/Null path: the Null backend hard-
// codes `IDevice::IsOperational() == false`, so the bootstrap is gated off
// and `FallbackTextureReady` must stay `false`. The "operational device"
// branch is covered by the GPU/Vulkan smoke under GRAPHICS-033D once the
// promoted Vulkan device flips operational.

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

    class StubApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Extrinsic::Runtime::Engine& /*engine*/) override {}
        void OnSimTick(Extrinsic::Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Extrinsic::Runtime::Engine& /*engine*/,
                            double /*alpha*/,
                            double /*dt*/) override {}
        void OnShutdown(Extrinsic::Runtime::Engine& /*engine*/) override {}
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig SingleWorkerEngineConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        return config;
    }
}

TEST(GpuAssetCacheFallbackBootstrap, NullDeviceLeavesFallbackUnreadyDeterministically)
{
    Extrinsic::Core::Config::EngineConfig config = SingleWorkerEngineConfig();
    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());

    engine.EmplaceModule<
        Extrinsic::Runtime::SceneDocumentModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::AssetWorkflowModule>();
    engine.Initialize();

    const auto diagnostics = RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine).GetDiagnostics();
    EXPECT_FALSE(diagnostics.FallbackTextureReady)
        << "Null device must not back the fallback texture; bootstrap is "
           "gated on IDevice::IsOperational() per RUNTIME-070.";
    EXPECT_EQ(diagnostics.TextureCreateFailures, 0u)
        << "Gated bootstrap must not increment failure counters on the Null path.";

    engine.Shutdown();
}

TEST(GpuAssetCacheFallbackBootstrap, ReInitializeRebootstrapsExactlyOnce)
{
    Extrinsic::Core::Config::EngineConfig config = SingleWorkerEngineConfig();
    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());

    engine.EmplaceModule<
        Extrinsic::Runtime::SceneDocumentModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::AssetWorkflowModule>();
    engine.Initialize();
    const auto firstDiagnostics = RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine).GetDiagnostics();
    EXPECT_FALSE(firstDiagnostics.FallbackTextureReady);
    engine.Shutdown();

    engine.Initialize();
    const auto secondDiagnostics = RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine).GetDiagnostics();
    EXPECT_FALSE(secondDiagnostics.FallbackTextureReady)
        << "Re-Initialize must rebuild the cache cleanly with the same Null-gated "
           "fallback state — no leftover lease, no double-bootstrap counter drift.";
    EXPECT_EQ(secondDiagnostics.TextureCreateFailures, 0u);
    engine.Shutdown();
}
