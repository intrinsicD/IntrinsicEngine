module;

module Runtime.Engine;

import Runtime.Engine;
import Core.Timer;

namespace Extrinsic::Runtime
{
    Engine::Engine(EngineConfig config)
        : mConfig(std::move(config))
    {
    }

    Engine::~Engine() = default;

    InitializationDiagnostic Engine::Initialize()
    {
        // Initialization logic here
        // tell everyone to startup in a specific order
        mInitializationDiagnostic = InitializationDiagnostic{};
        return mInitializationDiagnostic;
    }

    RunDiagnostic Engine::Run()
    {
        Core::Timer globalTime;
        Core::Timer frameTime;
        globalTime.Start();
        mRunDiagnostic = RunDiagnostic{};
        while (mIsRunning)
        {
            if (mIsPaused)
            {
                //query if actually unpaused;
                mIsPaused = mConfig.pPlatformModule->IsPaused();
                continue;
            }

            frameTime.Start();

            ++mRunDiagnostic.frameCounter;
            mRunDiagnostic.globalTimeSeconds = globalTime.ElapsedSeconds();
            mRunDiagnostic.frameTimeSeconds = frameTime.ElapsedSeconds();
            frameTime.Stop();
        }
        return mRunDiagnostic;
    }

    ShutdownDiagnostic Engine::Shutdown()
    {
        // Shutdown logic here
        // tell everyone to shutdown in a specific order
        // force stop all pending and queued work
        mShutdownDiagnostic = ShutdownDiagnostic{};
        return mShutdownDiagnostic;
    }

    bool Engine::IsRunning() const
    {
        // Return running state
        return mIsRunning;
    }

    const EngineConfig& Engine::GetConfig() const
    {
        return mConfig;
    }
}