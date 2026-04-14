//
// Created by alex on 14.04.26.
//

export module Sandbox;
import Extrinsic.Core.Application;
import Extrinsic.Runtime.Engine;

namespace Sandbox
{
    class App final : public Extrinsic::Core::IApplication
    {
    public:
        void OnInitialize(Engine::Core::Engine& engine) override
        {
            (void)engine;
            // Load scene, assets, camera, systems.
        }

        void OnUpdate(Engine::Core::Engine& engine, double deltaSeconds) override
        {
            (void)engine;
            (void)deltaSeconds;
            // Gameplay/editor update.
        }

        void OnShutdown(Engine::Core::Engine& engine) override
        {
            (void)engine;
        }
    };
}