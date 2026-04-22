export module Extrinsic.Core.Config.Simulation;

namespace Extrinsic::Core::Config
{
    export struct SimulationConfig
    {
        // Number of fiber worker threads used by Tasks::Scheduler.
        // 0 = auto-detect (hardware_concurrency - 1, minimum 1).
        unsigned WorkerThreadCount = 0;
    };
}

