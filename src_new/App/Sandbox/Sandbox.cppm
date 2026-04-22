export module Sandbox;

import Extrinsic.Runtime.Engine;

namespace Sandbox
{
    export class App final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Extrinsic::Runtime::Engine& engine) override
        {
            (void)engine;
        }

        void OnSimTick(Extrinsic::Runtime::Engine& engine, double fixedDt) override
        {
            (void)engine;
            (void)fixedDt;
        }

        void OnVariableTick(Extrinsic::Runtime::Engine& engine,
                            double alpha, double dt) override
        {
            (void)engine;
            (void)alpha;
            (void)dt;
        }

        void OnShutdown(Extrinsic::Runtime::Engine& engine) override
        {
            (void)engine;
        }
    };
}
