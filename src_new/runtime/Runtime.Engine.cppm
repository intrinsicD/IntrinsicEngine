module;

#include <string>

export module Runtime.Engine;

import Apps.IApplication;
import Platform.IBackend;
import Graphics.IBackend;
import Compute.IBackend;
import ECS.World;

namespace Extrinsic::Runtime
{
    export struct EngineConfig
    {
        Apps::IApplication* pApplication;
        Platform::IBackend* pPlatformModule;
        Graphics::IBackend* pGraphicsBackend;
        Compute::IBackend* pComputeBackend;
        ECS::World* pWorld;
    };

    export struct InitializationDiagnostic
    {

    };

    export struct RunDiagnostic
    {
        size_t frameCounter = 0;
        double frameTimeSeconds = 0.0;
        double globalTimeSeconds = 0.0;
    };

    export struct ShutdownDiagnostic
    {

    };

    export class Engine
    {
    public:
        Engine(EngineConfig config);
        ~Engine();

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        InitializationDiagnostic Initialize();
        RunDiagnostic Run();
        ShutdownDiagnostic Shutdown();

        bool IsRunning() const;

        const EngineConfig& GetConfig() const;

    private:
        InitializationDiagnostic mInitializationDiagnostic;
        RunDiagnostic mRunDiagnostic;
        ShutdownDiagnostic mShutdownDiagnostic;

        bool mIsRunning = false;
        bool mIsPaused = false;
        EngineConfig mConfig;
    };
}
