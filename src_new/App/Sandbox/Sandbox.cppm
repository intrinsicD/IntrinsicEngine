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

        void OnUpdate(Extrinsic::Runtime::Engine& engine, double deltaSeconds) override
        {
            (void)engine;
            (void)deltaSeconds;
        }

        void OnShutdown(Extrinsic::Runtime::Engine& engine) override
        {
            (void)engine;
        }
    };
}
