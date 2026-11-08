// ARCH-022: an engine composed as the Sandbox composes it for point-cloud editing: the editing service and the
// modules it uses, and the app's editor workspace (the shell's), which the service records its edits in.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

#include "PointDomainFixture.hpp"
#include "RuntimeTestModule.hpp"

import Extrinsic.Runtime.StepRuns;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SpatialIndexCache;

namespace Intrinsic::Tests::PointCloudEditing
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;

    inline Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        return config;
    }

    struct Harness
    {
        RuntimeTestKernel Engine;
        R::EditorWorkspaceAttachment Shell{};

        // `addModules` composes more (a plugin host) before the engine initializes.
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>(),
                         Config::EngineConfigSectionRegistry sections = {},
                         const std::function<void(RuntimeTestKernel&)>& addModules = {})
            : Engine(std::move(config), std::move(application))
        {
            Engine.EmplaceModule<R::AsyncWorkModule>();
            Engine.EmplaceModule<R::EngineConfigControl>(std::move(sections));
            Engine.EmplaceModule<R::EditorUiModule>();
            Engine.EmplaceModule<R::SceneDocumentModule>();
            Engine.EmplaceModule<R::SpatialIndexCache>();
            Engine.EmplaceModule<R::PointCloudEditingModule>();
            Engine.EmplaceModule<R::StepRunsModule>(); // as the Sandbox: GPU transactions are refused without it
            if (addModules) addModules(Engine);
            Engine.Initialize();
            Shell.Attach(Engine.Worlds(), Engine.Services());
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell); // the shell prepares one every frame
            Editing().UseEditorWorkspace(Shell);
        }
        // The engine shuts down while the shell is attached, as in tests of the Sandbox's order.
        ~Harness() { Engine.Shutdown(); }

        Extrinsic::ECS::Scene::Registry& Scene() { return *Engine.Worlds().Get(Engine.ActiveWorld()); }
        R::PointCloudEditingService& Editing() { return *Engine.Services().Find<R::PointCloudEditingService>(); }
        R::EditorCommandHistory& History() { return *Engine.Services().Find<R::EditorCommandHistory>(); }

        // Frames as the Sandbox runs them: the shell prepares its workspace, commands run, completions apply.
        void Frame(const int frames = 1)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell);
                Engine.Commands().Drain(Scene()); // the asynchronous input validation runs as commands
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Asks again until the asynchronous input validation answered (`ask` returns something false until then), as
        // a window asks each frame. The queued check runs on the command bus; a new workspace frame in between would
        // supersede it.
        template <class Ask>
        auto Settled(Ask ask)
        {
            auto answer = ask();
            for (int attempt = 0; !answer && attempt < 50; ++attempt)
            {
                Engine.Commands().Drain(Scene());
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                answer = ask();
            }
            return answer;
        }
    };

    // Samples on a 3-wide grid with normals along z and an unrelated float property `keep`.
    inline entt::entity MakeOrientedSamples(Extrinsic::ECS::Scene::Registry& scene, const R::GeometryElementDomain domain)
    {
        const auto entity = MakePointDomainSource(scene, domain);
        auto& props = PointDomainProperties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = {float(i % 3), float(i / 3), .15f * float(int(i % 3) - 1)};
        props.GetOrAdd<glm::vec3>("directions").Vector().assign(props.Size(), glm::vec3(0, 0, 1));
        std::ranges::fill(props.GetOrAdd<float>("keep").Vector(), 42.f);
        return entity;
    }
}
