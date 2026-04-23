export module Extrinsic.Sandbox;

import Extrinsic.Runtime.Engine;

namespace Extrinsic::Sandbox
{
    export class App final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            (void)engine;
        }

        void OnSimTick(Runtime::Engine& engine, double fixedDt) override
        {
            (void)engine;
            (void)fixedDt;
        }

        void OnVariableTick(Runtime::Engine& engine,
                            double alpha, double dt) override
        {
            (void)engine;
            (void)alpha;
            (void)dt;
        }

        void OnShutdown(Runtime::Engine& engine) override
        {
            (void)engine;
        }
    };
}
