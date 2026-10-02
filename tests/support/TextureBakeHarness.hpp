// Shared texture-bake fixture: the bake module composed as the runtime does over an operational
// mock device (with the shared scheduler and a provided JobService), and a seamed quad carrying
// `v:heat` and UV atlases. Used by the bake contract tests and the Sandbox bake panel tests.
// Include after "MockRHI.hpp".
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;

namespace Extrinsic::Tests::TextureBakeFixture
{
    namespace Runtime = Extrinsic::Runtime;
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    inline constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();
    inline constexpr float kAtlasExtent = 16.0f;

    [[nodiscard]] inline glm::vec2 Texel(const float x, const float y)
    {
        return glm::vec2{x / kAtlasExtent, y / kAtlasExtent};
    }

    // Quad v0(0,0) v1(1,0) v2(1,1) v3(0,1) as faces (v0,v1,v2), (v0,v2,v3).
    // h0..h2 / h3..h5 are the face loops (target vertices 1,2,0 / 2,3,0);
    // h6..h9 form the boundary loop. The canonical `h:texcoord` cuts a UV
    // seam along the diagonal (two charts); `h:custom_atlas` is one chart;
    // `v:texcoord` is a third, vertex-domain layout.
    [[nodiscard]] inline ECS::EntityHandle MakeSeamedQuad(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(4u);
        vertices.Properties
            .GetOrAdd<glm::vec3>(std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f})
            .Vector() = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                         {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        vertices.Properties.GetOrAdd<float>("v:heat", 0.0f).Vector() =
            {-2.0f, 0.0f, 3.5f, -0.25f};
        vertices.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f}).Vector() =
            {Texel(1.0f, 1.0f), Texel(15.0f, 1.0f), Texel(15.0f, 15.0f), Texel(1.0f, 15.0f)};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(5u);
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV0}, kInvalid)
            .Vector() = {0u, 1u, 2u, 2u, 3u};
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV1}, kInvalid)
            .Vector() = {1u, 2u, 0u, 3u, 0u};

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(10u);
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeToVertex}, kInvalid)
            .Vector() = {1u, 2u, 0u, 2u, 3u, 0u, 0u, 1u, 2u, 3u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeNext}, kInvalid)
            .Vector() = {1u, 2u, 0u, 4u, 5u, 3u, 9u, 6u, 7u, 8u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeFace}, kInvalid)
            .Vector() = {0u, 0u, 0u, 1u, 1u, 1u, kInvalid, kInvalid, kInvalid, kInvalid};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        // Face 0 texels (v1 7,1)(v2 7,7)(v0 1,1); face 1 (v2 15,7)(v3 9,7)(v0 9,1).
        // Boundary corners carry no triangle; NaN there must not matter.
        halfedges.Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.0f}).Vector() =
            {Texel(7.0f, 1.0f), Texel(7.0f, 7.0f), Texel(1.0f, 1.0f),
             Texel(15.0f, 7.0f), Texel(9.0f, 7.0f), Texel(9.0f, 1.0f),
             glm::vec2{nan}, glm::vec2{nan}, glm::vec2{nan}, glm::vec2{nan}};
        halfedges.Properties.GetOrAdd<glm::vec2>("h:custom_atlas", glm::vec2{0.0f}).Vector() =
            {Texel(14.0f, 2.0f), Texel(14.0f, 14.0f), Texel(2.0f, 2.0f),
             Texel(14.0f, 14.0f), Texel(2.0f, 14.0f), Texel(2.0f, 2.0f),
             glm::vec2{0.0f}, glm::vec2{0.0f}, glm::vec2{0.0f}, glm::vec2{0.0f}};

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(2u);
        faces.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kFaceHalfedge}, kInvalid)
            .Vector() = {0u, 3u};
        return entity;
    }

    // Composes the bake module exactly as the runtime does, with an
    // operational mock device, so requests run the real validation and
    // scheduling path. GPU recording needs a renderer frame and is covered by
    // the gpu;vulkan smoke.
    // Scheduled bakes submit a run job, which runs on the shared scheduler.
    struct BakeSchedulerScope
    {
        BakeSchedulerScope()
        {
            if (Core::Tasks::Scheduler::IsInitialized())
                Core::Tasks::Scheduler::Shutdown();
            Core::Tasks::Scheduler::Initialize(1u);
        }
        ~BakeSchedulerScope()
        {
            Core::Tasks::Scheduler::WaitForAll();
            Core::Tasks::Scheduler::Shutdown();
        }
        BakeSchedulerScope(const BakeSchedulerScope&) = delete;
        BakeSchedulerScope& operator=(const BakeSchedulerScope&) = delete;
    };

    struct BakeHarness
    {
        BakeHarness()
            : Renderer(Graphics::CreateRenderer())
        {
            World = Worlds.CreateWorld("TextureBake");
            Renderer->Initialize(Device);
        }

        ~BakeHarness()
        {
            Stop();
            Services.Reset();
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        [[nodiscard]] Runtime::EngineSetup MakeSetup()
        {
            return Runtime::EngineSetup{
                Commands, Events, Jobs, Worlds, Services,
                [this](const Runtime::FramePhase phase, Runtime::RuntimeFrameHook hook)
                {
                    if (phase == Runtime::FramePhase::Maintenance)
                        MaintenanceHooks.push_back(std::move(hook));
                },
                Runtime::RuntimeRenderRecipeActivationKernel{.ActiveConfig = &Config},
                {},
                &Initialized,
            };
        }

        [[nodiscard]] bool Start()
        {
            Services.BeginRegistration();
            if (!Services.Provide<RHI::IDevice>(Device, "Test.Device").has_value() ||
                // AsyncWorkModule's service in the engine; the editor session's job surface reads it.
                !Services.Provide<Runtime::JobService>(Jobs, "Test.Jobs").has_value() ||
                !Services.Provide<Graphics::IRenderer>(*Renderer, "Test.Renderer").has_value() ||
                !Services.Provide<Runtime::RenderExtractionCache>(Extraction, "Test.Extraction").has_value())
            {
                return false;
            }
            Runtime::EngineSetup registration = MakeSetup();
            Registered = Asset.OnRegister(registration).has_value() &&
                         Document.OnRegister(registration).has_value();
            // RunMaintenance drives only the texture-bake hooks under test.
            MaintenanceHooks.clear();
            Registered = Registered &&
                         TextureBake.OnRegister(registration).has_value();
            if (!Registered)
                return false;
            Services.BeginResolution();
            Runtime::EngineSetup resolution = MakeSetup();
            if (!Document.OnResolve(resolution).has_value() ||
                !Asset.OnResolve(resolution).has_value() ||
                !TextureBake.OnResolve(resolution).has_value())
            {
                return false;
            }
            Services.Lock();
            Service = Services.Find<Runtime::TextureBakeService>();
            return Service != nullptr && Service->Available();
        }

        void Stop()
        {
            if (!Registered)
                return;
            Initialized = false;
            Events.Publish(Runtime::RuntimeShutdownAnnounced{});
            (void)Events.Pump();
            (void)Jobs.ShutdownGpuQueueParticipants([this] { Device.WaitIdle(); });
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            TextureBake.OnShutdown(context);
            Asset.OnShutdown(context);
            Document.OnShutdown(context);
            Services.Reset();
            Registered = false;
            Service = nullptr;
        }

        [[nodiscard]] ECS::Scene::Registry& Scene() { return *Worlds.Get(World); }

        [[nodiscard]] Extrinsic::Assets::AssetService& Assets()
        {
            return *Services.Find<Extrinsic::Assets::AssetService>();
        }

        // Lets the run jobs' (empty) work finish, then drains completions on
        // this, the main, thread as the engine frame does.
        void DrainJobs()
        {
            Core::Tasks::Scheduler::WaitForAll();
            (void)Jobs.DrainCompletions(Events);
        }

        // Runs the modules' maintenance phase as one engine frame would.
        void RunMaintenance()
        {
            Runtime::EditorInputCaptureSnapshot capture{};
            Runtime::RuntimeFramePacingDiagnostics pacing{};
            Runtime::RuntimeFrameHookContext context{
                .ActiveWorld = Scene(),
                .ActiveWorldHandle = World,
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
                .EditorCapture = capture,
                .Pacing = pacing,
            };
            for (const Runtime::RuntimeFrameHook& hook : MaintenanceHooks)
                hook(context);
        }

        // First member: outlives the job service and everything a job reaches.
        BakeSchedulerScope Scheduler{};
        Extrinsic::Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{};
        Runtime::RenderExtractionCache Extraction{};
        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Core::Config::EngineConfig Config{};
        bool Initialized{false};
        bool Registered{false};
        Runtime::AssetWorkflowModule Asset{};
        Runtime::SceneDocumentModule Document{};
        Runtime::TextureBakeModule TextureBake{};
        Runtime::TextureBakeService* Service{};
        Runtime::WorldHandle World{};
        std::vector<Runtime::RuntimeFrameHook> MaintenanceHooks{};
    };

    [[nodiscard]] inline Runtime::PropertyTextureBakeRequest HeatRequest(
        const BakeHarness& harness,
        const ECS::EntityHandle entity,
        std::string outputName,
        Runtime::GeometryPropertyRef texcoords = {})
    {
        return Runtime::PropertyTextureBakeRequest{
            .World = harness.World,
            .StableEntityId = Runtime::StableEntityLookup::ToRenderId(entity),
            .Source = Runtime::GeometryPropertyRef{
                .Domain = Runtime::GeometryElementDomain::MeshVertex,
                .Name = "v:heat",
            },
            .Texcoords = std::move(texcoords),
            .Width = 16u,
            .Height = 16u,
            .PaddingTexels = 2u,
            .OutputName = std::move(outputName),
        };
    }
}
