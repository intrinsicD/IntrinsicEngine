//
// Created by alex on 14.04.26.
//

export module Sandbox;
import Extrinsic.Core.Application;
import Extrinsic.Runtime.Engine;

namespace Sandbox
{
    export class App final : public Extrinsic::Core::IApplication
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
