#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "MockRHI.hpp"

namespace
{
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace Graphics = Extrinsic::Graphics;
    namespace Platform = Extrinsic::Platform;
    namespace Runtime = Extrinsic::Runtime;
    namespace Sel = Extrinsic::ECS::Components::Selection;
    namespace Tf = Extrinsic::ECS::Components::Transform;

    class ExitAfterOneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override { ++InitializeCalls; }
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++VariableTicks;
            engine.RequestExit();
        }
        void Shutdown() override { ++ShutdownCalls; }

        std::uint32_t InitializeCalls{0u};
        std::uint32_t VariableTicks{0u};
        std::uint32_t ShutdownCalls{0u};
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend =
            Core::Config::WindowBackend::Null;
        return config;
    }

    struct DirectHarness
    {
        DirectHarness()
        {
            InitialWorld = Worlds.CreateWorld("Interaction");
            Core::Config::WindowConfig windowConfig{};
            windowConfig.Backend =
                Core::Config::WindowBackend::Null;
            windowConfig.Width = 800;
            windowConfig.Height = 600;
            Window = Platform::CreateWindow(windowConfig);
            Renderer = Graphics::CreateRenderer();
            Services.BeginRegistration();
        }

        [[nodiscard]] Runtime::EngineSetup MakeSetup(
            const bool frameRegistrar = true,
            const bool viewportRegistrar = true)
        {
            Runtime::EngineSetup::FrameHookRegistrar frames{};
            if (frameRegistrar)
            {
                frames =
                    [this](
                        const Runtime::FramePhase phase,
                        Runtime::RuntimeFrameHook hook)
                    {
                        FrameHooks.push_back(
                            FrameHookRecord{
                                .Phase = phase,
                                .Hook = std::move(hook),
                            });
                    };
            }
            Runtime::EngineSetup::
                ViewportInputHookRegistrar viewport{};
            if (viewportRegistrar)
            {
                viewport =
                    [this](
                        Runtime::RuntimeViewportInputHook hook)
                    {
                        ViewportHooks.push_back(
                            std::move(hook));
                    };
            }
            return Runtime::EngineSetup{
                Commands,
                Events,
                Jobs,
                Worlds,
                Services,
                std::move(frames),
                {},
                std::move(viewport),
            };
        }

        [[nodiscard]] Core::Result ProvideBuiltins()
        {
            if (!Window || !Renderer)
                return Core::Err(
                    Core::ErrorCode::InvalidState);
            if (Core::Result result =
                    Services.Provide<Platform::IWindow>(
                        *Window, "Test.Platform");
                !result.has_value())
            {
                return result;
            }
            if (Core::Result result =
                    Services.Provide<Graphics::IRenderer>(
                        *Renderer, "Test.Renderer");
                !result.has_value())
            {
                return result;
            }
            if (UiHost)
            {
                if (Core::Result result =
                        Services.Provide<Runtime::EditorUiHost>(
                            *UiHost, "Test.EditorUi");
                    !result.has_value())
                {
                    return result;
                }
            }
            if (Cameras)
            {
                if (Core::Result result =
                        Services.Provide<Runtime::CameraControllerRegistry>(*Cameras, "Test.Cameras");
                    !result.has_value())
                {
                    return result;
                }
            }
            return Services.Provide<
                Runtime::RenderExtractionCache>(
                Extraction, "Test.Extraction");
        }

        [[nodiscard]] Core::Result Register(
            const bool withDocument = false)
        {
            if (Core::Result provided = ProvideBuiltins();
                !provided.has_value())
            {
                return provided;
            }
            Runtime::EngineSetup setup = MakeSetup();
            if (withDocument)
            {
                Document = std::make_unique<
                    Runtime::SceneDocumentModule>();
                if (Core::Result registered =
                        Document->OnRegister(setup);
                    !registered.has_value())
                {
                    return registered;
                }
            }
            return Interaction.OnRegister(setup);
        }

        [[nodiscard]] Core::Result ResolveDocument()
        {
            if (!Document)
                return Core::Ok();
            Runtime::EngineSetup setup = MakeSetup();
            return Document->OnResolve(setup);
        }

        [[nodiscard]] Core::Result ResolveInteraction()
        {
            Runtime::EngineSetup setup = MakeSetup();
            return Interaction.OnResolve(setup);
        }

        [[nodiscard]] Core::Result Start(
            const bool withDocument = false)
        {
            if (Core::Result registered =
                    Register(withDocument);
                !registered.has_value())
            {
                return registered;
            }
            Services.BeginResolution();
            if (Core::Result document =
                    ResolveDocument();
                !document.has_value())
            {
                return document;
            }
            if (Core::Result interaction =
                    ResolveInteraction();
                !interaction.has_value())
            {
                return interaction;
            }
            Services.Lock();
            Started = true;
            return Core::Ok();
        }

        void Announce()
        {
            if (Announced)
                return;
            Events.Publish(
                Runtime::RuntimeShutdownAnnounced{});
            (void)Events.Pump();
            Announced = true;
        }

        void Stop()
        {
            if (Stopped)
                return;
            Announce();
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            Interaction.OnShutdown(context);
            if (Document)
                Document->OnShutdown(context);
            Stopped = true;
            Started = false;
        }

        struct FrameHookRecord
        {
            Runtime::FramePhase Phase{
                Runtime::FramePhase::Maintenance};
            Runtime::RuntimeFrameHook Hook{};
        };

        void InitializeRendererForHooks()
        {
            if (RendererInitialized)
                return;
            Renderer->Initialize(Device);
            RendererInitialized = true;
        }

        [[nodiscard]] Platform::Backends::Null::NullWindow&
        InputWindow()
        {
            return static_cast<
                Platform::Backends::Null::NullWindow&>(*Window);
        }

        void InvokeViewportHook(
            const std::size_t index,
            Graphics::RenderFrameInput& renderInput,
            const Runtime::EditorInputCaptureSnapshot&
                capture = {},
            const Platform::Extent2D viewport = {
                .Width = 64,
                .Height = 32},
            const Core::Offset2D viewportOrigin = {},
            const Platform::Extent2D framebufferExtent = {})
        {
            Core::Config::EngineConfig config = HeadlessConfig();
            Runtime::SetSelectionInteractionConfig(config, SelectionSettings);
            const Platform::Input::Context input{};
            Runtime::RuntimeViewportInputHookContext context{
                .Config = config,
                .ActiveWorldHandle =
                    Worlds.ActiveWorld(),
                .Input = input,
                .Viewport = viewport,
                .EditorCapture = capture,
                .RenderInput = renderInput,
                .ViewportOrigin = viewportOrigin,
                .FramebufferExtent = framebufferExtent,
            };
            ViewportHooks.at(index)(context);
        }

        void InvokeFrameHook(
            const std::size_t index,
            Runtime::EditorInputCaptureSnapshot& capture,
            Runtime::RuntimeFramePacingDiagnostics& pacing)
        {
            ECS::Scene::Registry* const scene =
                Worlds.Get(Worlds.ActiveWorld());
            ASSERT_NE(scene, nullptr);
            Runtime::RuntimeFrameHookContext context{
                .ActiveWorld = *scene,
                .ActiveWorldHandle =
                    Worlds.ActiveWorld(),
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
                .EditorCapture = capture,
                .Pacing = pacing,
            };
            FrameHooks.at(index).Hook(context);
        }

        ~DirectHarness()
        {
            Stop();
            if (RendererInitialized && Renderer)
                Renderer->Shutdown();
        }

        Runtime::SelectionInteractionConfig SelectionSettings{};
        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Extrinsic::Tests::MockDevice Device{};
        std::unique_ptr<Platform::IWindow> Window{};
        std::unique_ptr<Graphics::IRenderer> Renderer{};
        Runtime::RenderExtractionCache Extraction{};
        std::unique_ptr<Runtime::EditorUiHost> UiHost{};
        std::unique_ptr<Runtime::CameraControllerRegistry> Cameras{};
        Runtime::SceneInteractionModule Interaction{};
        std::unique_ptr<Runtime::SceneDocumentModule>
            Document{};
        std::vector<FrameHookRecord> FrameHooks{};
        std::vector<Runtime::RuntimeViewportInputHook>
            ViewportHooks{};
        Runtime::WorldHandle InitialWorld{};
        bool RendererInitialized{false};
        bool Started{false};
        bool Announced{false};
        bool Stopped{false};
    };

    struct ScopedScenePath
    {
        explicit ScopedScenePath(std::string_view name)
            : Path(
                  std::filesystem::temp_directory_path() /
                  name)
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        ~ScopedScenePath()
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path{};
    };

    [[nodiscard]] ECS::EntityHandle MakeSelectable(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        scene.Raw().emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    [[nodiscard]] ECS::EntityHandle MakeTransformSelectable(
        ECS::Scene::Registry& scene,
        const glm::vec3 position = glm::vec3{0.0f})
    {
        const ECS::EntityHandle entity = MakeSelectable(scene);
        scene.Raw().emplace<Tf::Component>(
            entity,
            Tf::Component{
                .Position = position,
                .Scale = glm::vec3{1.0f},
            });
        return entity;
    }

    [[nodiscard]] Graphics::CameraViewInput OrthoCameraInput()
    {
        Graphics::CameraViewInput input{};
        input.View = glm::lookAt(
            glm::vec3{0.0f, 0.0f, 5.0f},
            glm::vec3{0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f});
        input.Projection = glm::ortho(
            -4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f);
        input.Position = {0.0f, 0.0f, 5.0f};
        input.Forward = {0.0f, 0.0f, -1.0f};
        input.Up = {0.0f, 1.0f, 0.0f};
        input.NearPlane = 0.1f;
        input.FarPlane = 100.0f;
        input.Valid = true;
        return input;
    }

    [[nodiscard]] bool HasSelectedTag(
        const ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity)
    {
        return scene.Raw().all_of<Sel::SelectedTag>(entity);
    }

    [[nodiscard]] bool HasHoveredTag(
        const ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity)
    {
        return scene.Raw().all_of<Sel::HoveredTag>(entity);
    }

    // A frontend-style matrix session: Begin on `entity`, then one accepted
    // translation preview of `dx` along world X.
    void BeginTranslatePreview(
        Runtime::GizmoInteraction& gizmo,
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity,
        const float dx)
    {
        const ECS::EntityHandle selected[] = {entity};
        ASSERT_EQ(gizmo.Begin(scene, selected, Runtime::GizmoMode::Translate,
                              Runtime::GizmoOrientation::Global,
                              Runtime::GizmoPivotMode::WorldOrigins)
                      .Status,
                  Runtime::GizmoStatus::Ok);
        ASSERT_EQ(gizmo.Preview(scene,
                                glm::translate(glm::mat4{1.0f}, glm::vec3{dx, 0.0f, 0.0f}) *
                                    gizmo.SessionFrame().Matrix)
                      .Status,
                  Runtime::GizmoStatus::Ok);
    }

    // A Main controller bound to the active world, as CameraModule leaves it.
    Runtime::ICameraController& BindMainCamera(
        DirectHarness& harness,
        const Core::Config::CameraControllerKind kind = Core::Config::CameraControllerKind::FreeLook)
    {
        harness.Cameras->ResetForWorld(harness.Worlds.ActiveWorld());
        harness.Cameras->Register(Runtime::CameraControllerSlot::Main, Runtime::CreateCameraController(kind));
        return harness.Cameras->Resolve(Runtime::CameraControllerSlot::Main);
    }

    // A window whose framebuffer is twice its window extent (HiDPI).
    class HiDpiWindow final : public Platform::IWindow
    {
    public:
        void PollEvents() override {}
        [[nodiscard]] bool ShouldClose() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] bool WasResized() const override { return false; }
        void AcknowledgeResize() override {}
        [[nodiscard]] bool ConsumeInputActivity() override { return false; }
        [[nodiscard]] Platform::Extent2D GetWindowExtent() const override { return {.Width = 400, .Height = 300}; }
        [[nodiscard]] Platform::Extent2D GetFramebufferExtent() const override { return {.Width = 800, .Height = 600}; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }
        void Listen(EventCallbackFn) override {}
        [[nodiscard]] std::vector<Platform::Event> DrainEvents() override { return {}; }
        void OnUpdate() override {}
        void WaitForEventsTimeout(double) override {}
        void SetClipboardText(std::string_view) override {}
        [[nodiscard]] std::string GetClipboardText() const override { return {}; }
        void SetCursorMode(Platform::CursorMode) override {}
        [[nodiscard]] Platform::CursorMode GetCursorMode() const override { return Platform::CursorMode::Normal; }
    };

    void ExpectRect(const Runtime::EditorSceneViewportRect actual, const Runtime::EditorSceneViewportRect expected)
    {
        EXPECT_FLOAT_EQ(actual.X, expected.X);
        EXPECT_FLOAT_EQ(actual.Y, expected.Y);
        EXPECT_FLOAT_EQ(actual.Width, expected.Width);
        EXPECT_FLOAT_EQ(actual.Height, expected.Height);
    }

    void PublishHit(
        Graphics::SelectionSystem& system,
        const ECS::EntityHandle entity,
        const std::uint64_t sequence)
    {
        system.PublishPickResult(
            Graphics::PickReadbackResult{
                .EncodedId = Graphics::EncodeSelectionId(
                    Graphics::SelectionPrimitiveDomain::Entity,
                    1u),
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        entity),
                .Hit = true,
                .Sequence = sequence,
            });
    }
}

TEST(SceneInteractionModule,
     PublishesExactServicesAndSupportsOptionalDocument)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start().has_value());

    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneInteractionModule>(),
        &harness.Interaction);
    Runtime::SelectionController* const selection =
        harness.Services
            .Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        0u);
    EXPECT_EQ(selection->SelectedCount(), 0u);
    EXPECT_EQ(harness.ViewportHooks.size(), 1u);
    EXPECT_EQ(harness.FrameHooks.size(), 4u);

    harness.Stop();
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneInteractionModule>(),
        nullptr);
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SelectionController>(),
        nullptr);
}

TEST(SceneInteractionModule,
     DuplicatePublicationFailsClosed)
{
    DirectHarness harness;
    Runtime::SceneInteractionModule duplicate;
    Runtime::EngineSetup setup = harness.MakeSetup();

    ASSERT_TRUE(
        harness.Interaction.OnRegister(setup).has_value());
    const Core::Result duplicateResult =
        duplicate.OnRegister(setup);
    EXPECT_FALSE(duplicateResult.has_value());
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneInteractionModule>(),
        &harness.Interaction);

    Runtime::RuntimeModuleShutdownContext context{
        .Commands = harness.Commands,
        .Events = harness.Events,
        .Jobs = harness.Jobs,
        .Worlds = harness.Worlds,
        .Services = harness.Services,
    };
    duplicate.OnShutdown(context);
    harness.Interaction.OnShutdown(context);
    harness.Stopped = true;
}

TEST(SceneInteractionModule,
     PreexistingSelectionPublicationFailsClosed)
{
    DirectHarness harness;
    Runtime::SelectionController occupied;
    ASSERT_TRUE(
        harness.Services
            .Provide<Runtime::SelectionController>(
                occupied, "Occupied.Selection")
            .has_value());

    Runtime::EngineSetup setup = harness.MakeSetup();
    EXPECT_FALSE(
        harness.Interaction.OnRegister(setup)
            .has_value());
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneInteractionModule>(),
        nullptr);
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SelectionController>(),
        &occupied);
    EXPECT_TRUE(
        harness.Services
            .Withdraw<Runtime::SelectionController>(
                occupied)
            .has_value());
}

TEST(SceneInteractionModule,
     PartialRegistrationAndResolveFailureRollBack)
{
    {
        DirectHarness harness;
        Runtime::EngineSetup missingViewport =
            harness.MakeSetup(true, false);
        const Core::Result failed =
            harness.Interaction.OnRegister(
                missingViewport);
        EXPECT_FALSE(failed.has_value());
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SceneInteractionModule>(),
            nullptr);
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SelectionController>(),
            nullptr);
        ASSERT_EQ(harness.FrameHooks.size(), 4u);

        Runtime::EditorInputCaptureSnapshot capture{};
        Runtime::RuntimeFramePacingDiagnostics pacing{};
        ECS::Scene::Registry* const scene =
            harness.Worlds.Get(harness.InitialWorld);
        ASSERT_NE(scene, nullptr);
        for (const auto& record : harness.FrameHooks)
        {
            Runtime::RuntimeFrameHookContext context{
                .ActiveWorld = *scene,
                .ActiveWorldHandle =
                    harness.InitialWorld,
                .Commands = harness.Commands,
                .Events = harness.Events,
                .Jobs = harness.Jobs,
                .Worlds = harness.Worlds,
                .Services = harness.Services,
                .EditorCapture = capture,
                .Pacing = pacing,
            };
            record.Hook(context);
        }
        EXPECT_EQ(pacing.SelectionPickDrainMicros, 0u);
        EXPECT_EQ(pacing.SelectionReadbackMicros, 0u);
        EXPECT_EQ(pacing.PreRenderSetupMicros, 0u);

        Runtime::EngineSetup valid = harness.MakeSetup();
        ASSERT_TRUE(
            harness.Interaction.OnRegister(valid)
                .has_value());
        ASSERT_EQ(harness.FrameHooks.size(), 8u);
        ASSERT_EQ(harness.ViewportHooks.size(), 1u);
        EXPECT_EQ(
            harness.FrameHooks[4].Phase,
            Runtime::FramePhase::BeforeExtraction);
        EXPECT_EQ(
            harness.FrameHooks[5].Phase,
            Runtime::FramePhase::Maintenance);

        harness.InitializeRendererForHooks();
        ASSERT_TRUE(harness.ProvideBuiltins().has_value());
        harness.Services.BeginResolution();
        ASSERT_TRUE(
            harness.ResolveInteraction().has_value());
        harness.Services.Lock();
        harness.Started = true;

        Runtime::SelectionController& selection =
            *harness.Services
                 .Find<Runtime::SelectionController>();
        selection.RequestClickPick(12u, 18u);
        Graphics::RenderFrameInput renderInput{};
        harness.InvokeViewportHook(
            0u, renderInput, capture);

        // The registrar has no unregister surface. The failed attempt's four
        // retained lambdas therefore remain in the harness, but their weak
        // state expired during rollback and all are inert.
        for (std::size_t index = 0u; index < 4u; ++index)
            harness.InvokeFrameHook(index, capture, pacing);
        EXPECT_TRUE(selection.HasPendingPick());
        EXPECT_EQ(
            selection.GetDiagnostics().PicksDrained,
            0u);
        EXPECT_FALSE(renderInput.HasPendingPick);
        EXPECT_EQ(
            harness.Renderer->GetSelectionSystem()
                .GetDiagnostics()
                .PickRequestCount,
            0u);

        // Invoking the retry's live records produces exactly one effect: one
        // controller drain and one renderer-side request, with no duplicate
        // callback from the stale records.
        harness.InvokeFrameHook(4u, capture, pacing);
        harness.InvokeFrameHook(5u, capture, pacing);
        EXPECT_FALSE(selection.HasPendingPick());
        EXPECT_EQ(selection.InFlightPickCount(), 1u);
        EXPECT_TRUE(renderInput.HasPendingPick);
        EXPECT_EQ(
            selection.GetDiagnostics().PicksDrained,
            1u);
        EXPECT_EQ(
            harness.Renderer->GetSelectionSystem()
                .GetDiagnostics()
                .PickRequestCount,
            1u);
    }

    {
        DirectHarness harness;
        Runtime::EngineSetup setup = harness.MakeSetup();
        ASSERT_TRUE(
            harness.Interaction.OnRegister(setup)
                .has_value());
        harness.Services.BeginResolution();
        const Core::Result failed =
            harness.Interaction.OnResolve(setup);
        EXPECT_FALSE(failed.has_value());
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SceneInteractionModule>(),
            nullptr);
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SelectionController>(),
            nullptr);
    }

    {
        DirectHarness harness;
        ASSERT_TRUE(harness.Register(true).has_value());
        harness.Services.BeginResolution();
        ASSERT_TRUE(
            harness.ResolveDocument().has_value());
        auto conflict =
            harness.Document
                ->RegisterReplacementParticipant(
                    Runtime::
                        SceneReplacementParticipantDesc{
                            .Name =
                                "Runtime.SceneInteractionModule",
                            .BeforeReplace = {},
                            .AfterReplace = {},
                        });
        ASSERT_TRUE(conflict.has_value());

        const Core::Result failed =
            harness.ResolveInteraction();
        EXPECT_FALSE(failed.has_value());
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SceneInteractionModule>(),
            nullptr);
        EXPECT_EQ(
            harness.Services
                .Find<Runtime::SelectionController>(),
            nullptr);
        EXPECT_TRUE(
            harness.Document
                ->UnregisterReplacementParticipant(
                    *conflict)
                .has_value());
    }
}

TEST(SceneInteractionModule,
     RegisteredHooksIssueCorrelateRefineAndResetSelection)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());
    ASSERT_EQ(harness.ViewportHooks.size(), 1u);
    ASSERT_EQ(harness.FrameHooks.size(), 4u);
    EXPECT_EQ(
        harness.FrameHooks[0].Phase,
        Runtime::FramePhase::BeforeExtraction);
    EXPECT_EQ(
        harness.FrameHooks[1].Phase,
        Runtime::FramePhase::Maintenance);
    EXPECT_EQ(
        harness.FrameHooks[2].Phase,
        Runtime::FramePhase::UiBegin);
    // The Idle hook keeps minimized frames pumping focus events.
    EXPECT_EQ(
        harness.FrameHooks[3].Phase,
        Runtime::FramePhase::Idle);

    Runtime::SelectionController& selection =
        *harness.Services
             .Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity =
        MakeSelectable(scene);
    Graphics::SelectionSystem& selectionSystem =
        harness.Renderer->GetSelectionSystem();
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};

    selection.RequestClickPick(7u, 9u);
    Graphics::RenderFrameInput hitInput{};
    harness.InvokeViewportHook(
        0u, hitInput, capture);
    harness.InvokeFrameHook(0u, capture, pacing);

    EXPECT_FALSE(selection.HasPendingPick());
    EXPECT_EQ(selection.InFlightPickCount(), 1u);
    ASSERT_TRUE(hitInput.HasPendingPick);
    EXPECT_EQ(hitInput.Pick.X, 7u);
    EXPECT_EQ(hitInput.Pick.Y, 9u);
    ASSERT_NE(hitInput.Pick.Sequence, 0u);
    const auto issuedHit =
        selectionSystem.ConsumePick();
    ASSERT_TRUE(issuedHit.has_value());
    EXPECT_EQ(issuedHit->PixelX, 7u);
    EXPECT_EQ(issuedHit->PixelY, 9u);

    selectionSystem.PublishPickResult(
        Graphics::PickReadbackResult{
            .EncodedId =
                Graphics::EncodeSelectionId(
                    Graphics::
                        SelectionPrimitiveDomain::Entity,
                    1u),
            .StableEntityId =
                Runtime::SelectionController::
                    ToStableEntityId(entity),
            .Hit = true,
            .Sequence = hitInput.Pick.Sequence,
        });
    harness.InvokeFrameHook(1u, capture, pacing);

    EXPECT_TRUE(selection.IsSelected(entity));
    EXPECT_EQ(selection.SelectedCount(), 1u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    ASSERT_TRUE(
        harness.Interaction
            .LastRefinedPrimitive()
            .has_value());
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        1u);

    // A second frame proves the same hook chain correlates a background
    // readback to its exact request and applies the default replacement reset.
    selection.RequestClickPick(11u, 13u);
    Graphics::RenderFrameInput missInput{};
    harness.InvokeViewportHook(
        0u, missInput, capture);
    harness.InvokeFrameHook(0u, capture, pacing);
    ASSERT_TRUE(missInput.HasPendingPick);
    ASSERT_NE(missInput.Pick.Sequence, 0u);
    EXPECT_GT(
        missInput.Pick.Sequence,
        hitInput.Pick.Sequence);
    ASSERT_TRUE(
        selectionSystem.ConsumePick().has_value());

    selectionSystem.PublishPickResult(
        Graphics::PickReadbackResult{
            .Hit = false,
            .Sequence = missInput.Pick.Sequence,
        });
    harness.InvokeFrameHook(1u, capture, pacing);

    EXPECT_FALSE(selection.IsSelected(entity));
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_FALSE(
        harness.Interaction
            .LastRefinedPrimitive()
            .has_value());
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        2u);
}

TEST(SceneInteractionModule,
     OwnerHooksCorrelateOutOfOrderAndMissingReadbacksBySequence)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());

    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    Graphics::SelectionSystem& selectionSystem =
        harness.Renderer->GetSelectionSystem();
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};

    const auto issuePick =
        [&](const bool hover,
            const std::uint32_t x,
            const std::uint32_t y)
        {
            if (hover)
                selection.RequestHoverPick(x, y);
            else
                selection.RequestClickPick(x, y);
            Graphics::RenderFrameInput input{};
            input.Camera = OrthoCameraInput();
            harness.InvokeViewportHook(0u, input, capture);
            harness.InvokeFrameHook(0u, capture, pacing);
            EXPECT_TRUE(input.HasPendingPick);
            EXPECT_NE(input.Pick.Sequence, 0u);
            EXPECT_TRUE(selectionSystem.ConsumePick().has_value());
            return input.Pick.Sequence;
        };

    const ECS::EntityHandle clickTarget = MakeSelectable(scene);
    const ECS::EntityHandle hoverTarget = MakeSelectable(scene);
    const std::uint64_t clickSequence =
        issuePick(false, 1u, 1u);
    const std::uint64_t hoverSequence =
        issuePick(true, 2u, 2u);
    ASSERT_NE(clickSequence, hoverSequence);
    PublishHit(selectionSystem, hoverTarget, hoverSequence);
    PublishHit(selectionSystem, clickTarget, clickSequence);
    harness.InvokeFrameHook(1u, capture, pacing);

    EXPECT_TRUE(selection.IsSelected(clickTarget));
    EXPECT_TRUE(HasSelectedTag(scene, clickTarget));
    EXPECT_TRUE(selection.HasHovered());
    EXPECT_EQ(selection.HoveredEntity(), hoverTarget);
    EXPECT_TRUE(HasHoveredTag(scene, hoverTarget));
    EXPECT_EQ(selection.InFlightPickCount(), 0u);

    const ECS::EntityHandle lostHoverTarget = MakeSelectable(scene);
    const ECS::EntityHandle newerClickTarget = MakeSelectable(scene);
    const std::uint64_t lostHoverSequence =
        issuePick(true, 3u, 3u);
    const std::uint64_t newerClickSequence =
        issuePick(false, 4u, 4u);
    PublishHit(
        selectionSystem,
        newerClickTarget,
        newerClickSequence);
    harness.InvokeFrameHook(1u, capture, pacing);

    EXPECT_TRUE(selection.IsSelected(newerClickTarget));
    EXPECT_FALSE(HasSelectedTag(scene, lostHoverTarget));
    EXPECT_FALSE(HasHoveredTag(scene, lostHoverTarget));
    EXPECT_EQ(selection.InFlightPickCount(), 1u);
    EXPECT_EQ(
        selection.OldestInFlightSequence(),
        lostHoverSequence);
}

TEST(SceneInteractionModule,
     OwnerBoundsCorrelationAndRejectsZeroUnknownAndStaleWorldResults)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());

    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    selection.GetConfig().MaxTrackedInFlightPicks = 0u;
    ECS::Scene::Registry& firstScene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle target = MakeSelectable(firstScene);
    Graphics::SelectionSystem& selectionSystem =
        harness.Renderer->GetSelectionSystem();
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};

    std::uint64_t firstSequence = 0u;
    std::uint64_t lastSequence = 0u;
    for (std::uint32_t index = 0u; index < 33u; ++index)
    {
        selection.RequestClickPick(index, index);
        Graphics::RenderFrameInput input{};
        input.Camera = OrthoCameraInput();
        harness.InvokeViewportHook(0u, input, capture);
        harness.InvokeFrameHook(0u, capture, pacing);
        ASSERT_TRUE(input.HasPendingPick);
        ASSERT_NE(input.Pick.Sequence, 0u);
        ASSERT_TRUE(selectionSystem.ConsumePick().has_value());
        if (index == 0u)
            firstSequence = input.Pick.Sequence;
        lastSequence = input.Pick.Sequence;
    }
    ASSERT_NE(firstSequence, 0u);
    ASSERT_NE(lastSequence, 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 32u);

    PublishHit(selectionSystem, target, firstSequence);
    selectionSystem.PublishNoHit();
    harness.InvokeFrameHook(1u, capture, pacing);
    EXPECT_FALSE(selection.IsSelected(target));
    EXPECT_EQ(selection.InFlightPickCount(), 32u);
    EXPECT_EQ(
        harness.Interaction.LastRefinedPrimitiveGeneration(),
        0u);

    const Runtime::WorldHandle secondWorld =
        harness.Worlds.CreateWorld("Second interaction world");
    ASSERT_TRUE(
        harness.Worlds.RequestSetActiveWorld(secondWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Interaction.ResolveEntityByStableId(
        ECSC::StableId{0x205u, 0xB0u});
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_EQ(
        harness.Interaction.LastRefinedPrimitiveGeneration(),
        1u);

    PublishHit(selectionSystem, target, lastSequence);
    harness.InvokeFrameHook(1u, capture, pacing);
    EXPECT_FALSE(HasSelectedTag(firstScene, target));
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(
        harness.Interaction.LastRefinedPrimitiveGeneration(),
        1u);
}

TEST(SceneInteractionModule,
     DocumentEpochResetRejectsLateCorrelatedReadback)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start(true).has_value());

    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle target = MakeSelectable(scene);
    Graphics::SelectionSystem& selectionSystem =
        harness.Renderer->GetSelectionSystem();
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};

    selection.RequestClickPick(9u, 10u);
    Graphics::RenderFrameInput input{};
    input.Camera = OrthoCameraInput();
    harness.InvokeViewportHook(0u, input, capture);
    harness.InvokeFrameHook(0u, capture, pacing);
    ASSERT_TRUE(input.HasPendingPick);
    ASSERT_TRUE(selectionSystem.ConsumePick().has_value());

    ASSERT_TRUE(
        harness.Document->NewSceneDocument().has_value());
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_EQ(
        harness.Interaction.LastRefinedPrimitiveGeneration(),
        1u);
    PublishHit(selectionSystem, target, input.Pick.Sequence);
    harness.InvokeFrameHook(1u, capture, pacing);
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_FALSE(
        harness.Interaction.LastRefinedPrimitive().has_value());
    EXPECT_EQ(
        harness.Interaction.LastRefinedPrimitiveGeneration(),
        1u);
}

// UI-078: the viewport hook never drives the gizmo. A press, drag and
// release over the selected entity's pivot starts no session and writes
// nothing; only the editor frontend's typed calls do.
TEST(SceneInteractionModule, ViewportMouseNeverDrivesTheGizmo)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory& history =
        *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));

    Graphics::RenderFrameInput input{};
    input.Camera = OrthoCameraInput();
    auto& window = harness.InputWindow();
    const Platform::Extent2D viewport{.Width = 800, .Height = 600};
    // The pivot projects to (400,300) and the old X handle ran to (500,300).
    window.QueueCursor(450.0, 300.0);
    window.QueueMouseButton(0, true);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);
    window.QueueCursor(550.0, 300.0);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);
    window.QueueMouseButton(0, false);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);

    EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    EXPECT_EQ(harness.Interaction.Interaction().Diagnostics().DragsStarted, 0u);
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(history.UndoCount(), 0u);
    // The click is an ordinary pick request instead.
    EXPECT_TRUE(selection.HasPendingPick());
}

// UI-078: the typed frontend calls drive the matrix core, so a group
// rotation moves both origins around the shared pivot and commits once.
TEST(SceneInteractionModule, TypedGizmoDragRotatesGroupAroundPivotAndCommitsOnce)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory& history =
        *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle right = MakeTransformSelectable(scene, glm::vec3{1.0f, 0.0f, 0.0f});
    const ECS::EntityHandle left = MakeTransformSelectable(scene, glm::vec3{-1.0f, 0.0f, 0.0f});
    for (const ECS::EntityHandle entity : {right, left})
    {
        selection.RequestClickPick(0u, 0u, Runtime::SelectionPickMode::Add);
        (void)selection.ConsumePendingPick();
        selection.ConsumeHit(scene, Runtime::SelectionController::ToStableEntityId(entity));
    }
    ASSERT_EQ(selection.SelectedCount(), 2u);

    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    Runtime::SceneInteractionModule& module = harness.Interaction;
    const Runtime::GizmoUiFrame frame = module.PrepareGizmo(kGlobal, kOrigins);
    const Runtime::GizmoUiBeginResult begun =
        module.BeginGizmoDrag(frame.Token, Runtime::GizmoMode::Rotate, kGlobal, kOrigins);
    ASSERT_TRUE(begun.Succeeded());
    // 1 rad about world Y through the pivot (0,0,0).
    ASSERT_TRUE(module.PreviewGizmoDrag(
        begun.Token, glm::rotate(glm::mat4{1.0f}, 1.0f, glm::vec3{0.0f, 1.0f, 0.0f}) * frame.Frame.Matrix)
                    .Succeeded());
    const glm::vec3 rightPosition = scene.Raw().get<Tf::Component>(right).Position;
    const glm::vec3 leftPosition = scene.Raw().get<Tf::Component>(left).Position;
    EXPECT_NEAR(rightPosition.x, std::cos(1.0f), 1.0e-4f);
    EXPECT_NEAR(rightPosition.z, -std::sin(1.0f), 1.0e-4f);
    EXPECT_NEAR(leftPosition.x, -std::cos(1.0f), 1.0e-4f);
    EXPECT_NEAR(leftPosition.z, std::sin(1.0f), 1.0e-4f);

    ASSERT_TRUE(module.CommitGizmoDrag(begun.Token).Succeeded());
    EXPECT_FALSE(module.Interaction().IsDragging());
    ASSERT_EQ(history.UndoCount(), 1u);
    ASSERT_EQ(history.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(scene.Raw().get<Tf::Component>(right).Position, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(scene.Raw().get<Tf::Component>(left).Position, glm::vec3(-1.0f, 0.0f, 0.0f));
}

// UI-078 slice 2: a frontend's viewport claim owns the frame. The viewport
// hook neither cancels (capture) nor commits (released mouse) the frontend
// session, and the claimed capture blocks new picks. Hiding the UI cancels at
// UiBegin or, for a hide during UiBuild, inside the viewport hook; a session
// begun while the UI is already hidden keeps running.
TEST(SceneInteractionModule,
     FrontendViewportClaimOwnsTheSessionAndUiHideCancelsIt)
{
    DirectHarness harness;
    harness.UiHost = std::make_unique<Runtime::EditorUiHost>();
    Runtime::EditorUiHostOwnerControl owner =
        harness.UiHost->ClaimOwnerControl();
    owner.SetOperational(true);
    ASSERT_TRUE(harness.Start(true).has_value());
    ASSERT_EQ(harness.FrameHooks[2].Phase, Runtime::FramePhase::UiBegin);
    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory& history =
        *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
    Runtime::GizmoInteraction& gizmo = harness.Interaction.Interaction();

    Graphics::RenderFrameInput input{};
    input.Camera = OrthoCameraInput();
    auto& window = harness.InputWindow();
    const Platform::Extent2D viewport{.Width = 800, .Height = 600};
    const Runtime::EditorInputCaptureSnapshot merged{
        .CapturedKeyboard = true,
        .CapturedMouse = true,
    };
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};

    BeginTranslatePreview(gizmo, scene, entity, 1.0f);
    harness.UiHost->RequestViewportInput({.CaptureViewportInput = true});
    // Mouse up with an active session: no commit.
    harness.InvokeViewportHook(0u, input, merged, viewport);
    EXPECT_TRUE(gizmo.IsDragging());
    // A fresh click under the claim: no cancel on capture.
    window.QueueCursor(450.0, 300.0);
    window.QueueMouseButton(0, true);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, merged, viewport);
    EXPECT_TRUE(gizmo.IsDragging());
    EXPECT_FALSE(selection.HasPendingPick());
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position.x, 1.0f);
    EXPECT_EQ(history.UndoCount(), 0u);

    // Hide via the host command, observed by the next UiBegin.
    (void)harness.UiHost->ApplyVisibilityCommand(
        {Runtime::EditorUiVisibilityCommandKind::Hide});
    EXPECT_FALSE(
        harness.UiHost->ViewportInputRequest().CaptureViewportInput);
    harness.InvokeFrameHook(2u, capture, pacing);
    EXPECT_FALSE(gizmo.IsDragging());
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_TRUE(selection.IsSelected(entity));

    // Only the visible -> hidden transition cancels.
    BeginTranslatePreview(gizmo, scene, entity, 1.0f);
    harness.InvokeFrameHook(2u, capture, pacing);
    EXPECT_TRUE(gizmo.IsDragging());
    EXPECT_EQ(gizmo.DragCancel(scene).Status, Runtime::GizmoStatus::Ok);

    // A hide during UiBuild cancels in the viewport hook, before the flush.
    window.QueueMouseButton(0, false);
    window.PollEvents();
    (void)harness.UiHost->ApplyVisibilityCommand(
        {Runtime::EditorUiVisibilityCommandKind::Show});
    harness.InvokeFrameHook(2u, capture, pacing);
    BeginTranslatePreview(gizmo, scene, entity, 2.0f);
    (void)harness.UiHost->ApplyVisibilityCommand(
        {Runtime::EditorUiVisibilityCommandKind::Hide});
    harness.InvokeViewportHook(0u, input, {}, viewport);
    EXPECT_FALSE(gizmo.IsDragging());
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(history.UndoCount(), 0u);
}

// UI-078 slice 2: native focus loss cancels at event delivery, also when the
// focus returns within the same event batch; a focus gain alone does not.
TEST(SceneInteractionModule,
     WindowFocusLossCancelsPreviewWithoutHistory)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory& history =
        *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
    Runtime::GizmoInteraction& gizmo = harness.Interaction.Interaction();

    BeginTranslatePreview(gizmo, scene, entity, 1.0f);
    harness.Events.Publish(Platform::WindowFocusEvent{.Focused = true});
    (void)harness.Events.Pump();
    EXPECT_TRUE(gizmo.IsDragging());

    harness.Events.Publish(Platform::WindowFocusEvent{.Focused = false});
    harness.Events.Publish(Platform::WindowFocusEvent{.Focused = true});
    (void)harness.Events.Pump();
    EXPECT_FALSE(gizmo.IsDragging());
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_EQ(gizmo.Diagnostics().DragsCancelled, 1u);
    EXPECT_TRUE(selection.IsSelected(entity));
}

// UI-078 slice 2: world switch and New/Close/Load during a matrix preview
// cancel through the existing world-bound reset, before the outgoing registry
// goes away; the incoming registry is untouched and a new session can begin.
TEST(SceneInteractionModule,
     WorldAndDocumentChangesCancelMatrixPreviewWithoutHistory)
{
    ScopedScenePath saved{"intrinsic-ui078-preview.scene.json"};
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::EditorCommandHistory& history =
        *harness.Services.Find<Runtime::EditorCommandHistory>();
    Runtime::GizmoInteraction& gizmo = harness.Interaction.Interaction();
    ECS::Scene::Registry& first = *harness.Worlds.Get(harness.InitialWorld);
    ASSERT_TRUE(harness.Document->SaveSceneToPath(saved.Path.string()).has_value());

    using DocumentOp = bool (*)(Runtime::SceneDocumentModule&, const std::string&);
    const DocumentOp operations[] = {
        [](Runtime::SceneDocumentModule& document, const std::string&)
        { return document.NewSceneDocument().has_value(); },
        [](Runtime::SceneDocumentModule& document, const std::string&)
        { return document.CloseSceneDocument().has_value(); },
        [](Runtime::SceneDocumentModule& document, const std::string& path)
        { return document.LoadSceneFromPath(path).has_value(); },
    };
    for (const DocumentOp operation : operations)
    {
        BeginTranslatePreview(gizmo, first, MakeTransformSelectable(first), 1.0f);
        ASSERT_TRUE(operation(*harness.Document, saved.Path.string()));
        EXPECT_FALSE(gizmo.IsDragging());
        EXPECT_EQ(history.UndoCount(), 0u);
    }

    const ECS::EntityHandle entity = MakeTransformSelectable(first);
    const Runtime::WorldHandle secondWorld = harness.Worlds.CreateWorld("Second");
    ECS::Scene::Registry& second = *harness.Worlds.Get(secondWorld);
    const ECS::EntityHandle other =
        MakeTransformSelectable(second, glm::vec3{5.0f, 0.0f, 0.0f});
    BeginTranslatePreview(gizmo, first, entity, 1.0f);
    ASSERT_TRUE(harness.Worlds.RequestSetActiveWorld(secondWorld).has_value());
    (void)harness.Worlds.ApplyMaintenance(harness.Events, harness.Jobs);
    (void)harness.Events.Pump();
    EXPECT_FALSE(gizmo.IsDragging());
    EXPECT_EQ(first.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(second.Raw().get<Tf::Component>(other).Position,
              glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_EQ(history.UndoCount(), 0u);
    BeginTranslatePreview(gizmo, second, other, 1.0f);
    EXPECT_EQ(gizmo.DragCancel(second).Status, Runtime::GizmoStatus::Ok);
}

// UI-078 slice 3b: the frontend model names why nothing can be manipulated,
// and Begin refuses for the same reasons without starting a session.
TEST(SceneInteractionModule, GizmoUiFrameReportsWhyNothingCanBeManipulated)
{
    using Runtime::GizmoUiUnavailable;
    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    constexpr auto kTranslate = Runtime::GizmoMode::Translate;
    {
        DirectHarness harness; // No SceneDocumentModule, so no history.
        harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
        ASSERT_TRUE(harness.Start().has_value());
        (void)BindMainCamera(harness);
        ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
        ASSERT_TRUE(harness.Services.Find<Runtime::SelectionController>()->SetSelectedEntity(
            scene, MakeTransformSelectable(scene)));
        const Runtime::GizmoUiFrame frame = harness.Interaction.PrepareGizmo(kGlobal, kOrigins);
        EXPECT_EQ(frame.Unavailable, GizmoUiUnavailable::NoHistory);
        EXPECT_EQ(harness.Interaction.BeginGizmoDrag(frame.Token, kTranslate, kGlobal, kOrigins).Unavailable,
                  GizmoUiUnavailable::NoHistory);
        EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    }

    DirectHarness harness;
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection = *harness.Services.Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    Runtime::SceneInteractionModule& module = harness.Interaction;

    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).Unavailable, GizmoUiUnavailable::NoEntitySelection);
    EXPECT_EQ(module.BeginGizmoDrag(module.PrepareGizmo(kGlobal, kOrigins).Token, kTranslate, kGlobal, kOrigins)
                  .Unavailable,
              GizmoUiUnavailable::NoEntitySelection);

    // A selected entity without a Transform has no frame.
    ASSERT_TRUE(selection.SetSelectedEntity(scene, MakeSelectable(scene)));
    Runtime::GizmoUiFrame frame = module.PrepareGizmo(kGlobal, kOrigins);
    EXPECT_EQ(frame.Unavailable, GizmoUiUnavailable::InvalidFrame);
    EXPECT_EQ(frame.Frame.Result.Status, Runtime::GizmoStatus::InvalidEntity);

    const ECS::EntityHandle entity = MakeTransformSelectable(scene, glm::vec3{1.0f, 2.0f, 3.0f});
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).Unavailable, GizmoUiUnavailable::NoCamera);
    // A controller still bound to another world is not this world's camera.
    harness.Cameras->ResetForWorld(harness.Worlds.CreateWorld("Other"));
    harness.Cameras->Register(Runtime::CameraControllerSlot::Main,
                              Runtime::CreateCameraController(Core::Config::CameraControllerKind::FreeLook));
    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).Unavailable, GizmoUiUnavailable::NoCamera);

    (void)BindMainCamera(harness);
    frame = module.PrepareGizmo(kGlobal, kOrigins);
    ASSERT_TRUE(frame.Available());
    EXPECT_FALSE(frame.Dragging);
    EXPECT_EQ(frame.Token.World, harness.InitialWorld);
    EXPECT_EQ(frame.Token.Session, 0u);
    EXPECT_EQ(glm::vec3(frame.GizmoMatrix[3]), glm::vec3(1.0f, 2.0f, 3.0f));

    // A primitive selection target has no entity gizmo.
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Face;
    Graphics::RenderFrameInput input{};
    harness.InvokeViewportHook(0u, input);
    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).Unavailable, GizmoUiUnavailable::NoEntitySelection);
    EXPECT_EQ(module.BeginGizmoDrag(frame.Token, kTranslate, kGlobal, kOrigins).Unavailable,
              GizmoUiUnavailable::NoEntitySelection);
    EXPECT_FALSE(module.Interaction().IsDragging());

    harness.Stop();
    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).Unavailable, GizmoUiUnavailable::NoBinding);
    EXPECT_EQ(module.BeginGizmoDrag(frame.Token, kTranslate, kGlobal, kOrigins).Unavailable,
              GizmoUiUnavailable::NoBinding);
}

// UI-078 slice 3b: before Begin the frame is ComputeFrame for the requested
// options; during a drag it is the frozen session (mode, frame, accepted Gt)
// whatever options or selection the frontend shows. The camera is the Main
// controller's current view for the scene rectangle, read without Update.
TEST(SceneInteractionModule, GizmoUiFrameIsComputeFrameBeforeBeginAndTheFrozenSessionDuringADrag)
{
    DirectHarness harness;
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection = *harness.Services.Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    Runtime::SceneInteractionModule& module = harness.Interaction;
    Runtime::GizmoInteraction& gizmo = module.Interaction();
    const ECS::EntityHandle a = MakeTransformSelectable(scene, glm::vec3{1.0f, 0.0f, 0.0f});
    const ECS::EntityHandle b = MakeTransformSelectable(scene, glm::vec3{-1.0f, 0.0f, 2.0f});
    scene.Raw().get<Tf::Component>(a).Rotation = glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f});
    for (const ECS::EntityHandle entity : {a, b})
    {
        selection.RequestClickPick(0u, 0u, Runtime::SelectionPickMode::Add);
        (void)selection.ConsumePendingPick();
        selection.ConsumeHit(scene, Runtime::SelectionController::ToStableEntityId(entity));
    }
    ASSERT_EQ(selection.SelectedCount(), 2u);

    Runtime::ICameraController& camera = BindMainCamera(harness);
    // Hold W: an Update would move the free-look camera.
    harness.InputWindow().QueueKey(Platform::Input::Key::W, true);
    harness.InputWindow().PollEvents();
    const Graphics::CameraViewInput expected = camera.GetView({.Width = 800, .Height = 600});

    const ECS::EntityHandle selected[] = {a, b};
    const Runtime::GizmoFrame computed = gizmo.ComputeFrame(
        scene, selected, Runtime::GizmoOrientation::Local, Runtime::GizmoPivotMode::BoundsCenters);
    ASSERT_TRUE(computed.Available());
    const Runtime::GizmoUiFrame before =
        module.PrepareGizmo(Runtime::GizmoOrientation::Local, Runtime::GizmoPivotMode::BoundsCenters);
    ASSERT_TRUE(before.Available());
    EXPECT_FALSE(before.Dragging);
    EXPECT_EQ(before.Frame.Matrix, computed.Matrix);
    EXPECT_EQ(before.Frame.Pivot, computed.Pivot);
    EXPECT_EQ(before.Frame.Primary, computed.Primary);
    EXPECT_EQ(before.Frame.ActualOrientation, Runtime::GizmoOrientation::Local);
    EXPECT_EQ(before.GizmoMatrix, computed.Matrix);
    EXPECT_EQ(before.View, expected.View);
    EXPECT_EQ(before.Projection, expected.Projection);
    EXPECT_FALSE(before.Orthographic);
    ExpectRect(before.SceneRect, {.X = 0.0f, .Y = 0.0f, .Width = 800.0f, .Height = 600.0f});
    EXPECT_EQ(camera.GetView({.Width = 800, .Height = 600}).View, expected.View);

    const Runtime::GizmoUiBeginResult begun = module.BeginGizmoDrag(
        before.Token, Runtime::GizmoMode::Rotate, Runtime::GizmoOrientation::Local,
        Runtime::GizmoPivotMode::BoundsCenters);
    ASSERT_TRUE(begun.Succeeded());
    EXPECT_NE(begun.Token, before.Token);
    EXPECT_EQ(begun.Token.World, before.Token.World);
    EXPECT_EQ(begun.Token.InteractionEpoch, before.Token.InteractionEpoch);
    const glm::mat4 moved = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}) * before.Frame.Matrix;
    ASSERT_TRUE(module.PreviewGizmoDrag(begun.Token, moved).Succeeded());

    ASSERT_TRUE(selection.SetSelectedEntity(scene, a));
    const Runtime::GizmoUiFrame during =
        module.PrepareGizmo(Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::WorldOrigins);
    ASSERT_TRUE(during.Available());
    EXPECT_TRUE(during.Dragging);
    EXPECT_EQ(during.Token, begun.Token);
    EXPECT_EQ(during.SessionMode, Runtime::GizmoMode::Rotate);
    EXPECT_EQ(during.Frame.Matrix, gizmo.SessionFrame().Matrix);
    EXPECT_EQ(during.Frame.Matrix, computed.Matrix);
    EXPECT_EQ(during.Frame.ActualOrientation, Runtime::GizmoOrientation::Local);
    EXPECT_EQ(during.GizmoMatrix, moved);

    harness.Cameras->Replace(Runtime::CameraControllerSlot::Main,
                             Runtime::CreateCameraController(Core::Config::CameraControllerKind::TopDown));
    const Runtime::GizmoUiFrame ortho =
        module.PrepareGizmo(Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::WorldOrigins);
    EXPECT_TRUE(ortho.Orthographic);
    EXPECT_EQ(ortho.Projection,
              harness.Cameras->Resolve(Runtime::CameraControllerSlot::Main)
                  .GetView({.Width = 800, .Height = 600})
                  .Projection);
    EXPECT_EQ(module.CancelGizmoDrag(begun.Token).Status, Runtime::GizmoStatus::Ok);
}

// UI-078 slice 3b: the scene rectangle is this UI frame's current claim (not
// the presented one of the previous frame), else the whole client area,
// resolved to framebuffer pixels like the engine and mapped back to window
// (ImGui logical) coordinates; the projection uses the pixel extent.
TEST(SceneInteractionModule, GizmoUiSceneRectIsTheCurrentClaimMappedBackFromFramebufferPixels)
{
    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    {
        DirectHarness harness;
        harness.UiHost = std::make_unique<Runtime::EditorUiHost>();
        Runtime::EditorUiHostOwnerControl owner = harness.UiHost->ClaimOwnerControl();
        owner.SetOperational(true);
        harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
        ASSERT_TRUE(harness.Start(true).has_value());
        ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
        ASSERT_TRUE(harness.Services.Find<Runtime::SelectionController>()->SetSelectedEntity(
            scene, MakeTransformSelectable(scene)));
        Runtime::ICameraController& camera = BindMainCamera(harness);
        Runtime::SceneInteractionModule& module = harness.Interaction;

        // An offset split pane claimed during this frame's layout.
        harness.UiHost->SetSceneViewport({.X = 100.0f, .Y = 50.0f, .Width = 400.0f, .Height = 200.0f});
        Runtime::GizmoUiFrame frame = module.PrepareGizmo(kGlobal, kOrigins);
        ASSERT_TRUE(frame.Available());
        ExpectRect(frame.SceneRect, {.X = 100.0f, .Y = 50.0f, .Width = 400.0f, .Height = 200.0f});
        EXPECT_EQ(frame.Projection, camera.GetView({.Width = 400, .Height = 200}).Projection);
        EXPECT_NE(frame.Projection, camera.GetView({.Width = 800, .Height = 600}).Projection);

        // Next frame before layout: the old claim is only presented now.
        (void)owner.DrawFrameContributions();
        ASSERT_TRUE(harness.UiHost->PresentedSceneViewport().has_value());
        frame = module.PrepareGizmo(kGlobal, kOrigins);
        ExpectRect(frame.SceneRect, {.X = 0.0f, .Y = 0.0f, .Width = 800.0f, .Height = 600.0f});
        EXPECT_EQ(frame.Projection, camera.GetView({.Width = 800, .Height = 600}).Projection);

        // A hidden editor's claim never applies.
        harness.UiHost->SetSceneViewport({.X = 100.0f, .Y = 50.0f, .Width = 400.0f, .Height = 200.0f});
        (void)harness.UiHost->ApplyVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Hide});
        ExpectRect(module.PrepareGizmo(kGlobal, kOrigins).SceneRect,
                   {.X = 0.0f, .Y = 0.0f, .Width = 800.0f, .Height = 600.0f});
    }

    DirectHarness harness;
    harness.Window = std::make_unique<HiDpiWindow>();
    harness.UiHost = std::make_unique<Runtime::EditorUiHost>();
    Runtime::EditorUiHostOwnerControl owner = harness.UiHost->ClaimOwnerControl();
    owner.SetOperational(true);
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    ASSERT_TRUE(harness.Services.Find<Runtime::SelectionController>()->SetSelectedEntity(
        scene, MakeTransformSelectable(scene)));
    Runtime::ICameraController& camera = BindMainCamera(harness);

    Runtime::GizmoUiFrame frame = harness.Interaction.PrepareGizmo(kGlobal, kOrigins);
    ExpectRect(frame.SceneRect, {.X = 0.0f, .Y = 0.0f, .Width = 400.0f, .Height = 300.0f});
    EXPECT_EQ(frame.Projection, camera.GetView({.Width = 800, .Height = 600}).Projection);

    // 2x scale; pixel edges round (200.6 -> 201, 600.6 -> 601) and map back.
    harness.UiHost->SetSceneViewport({.X = 100.3f, .Y = 20.0f, .Width = 200.0f, .Height = 250.0f});
    frame = harness.Interaction.PrepareGizmo(kGlobal, kOrigins);
    ExpectRect(frame.SceneRect, {.X = 100.5f, .Y = 20.0f, .Width = 200.0f, .Height = 250.0f});
    EXPECT_EQ(frame.Projection, camera.GetView({.Width = 400, .Height = 500}).Projection);
}

// UI-078 slice 3b: many previews then one commit record exactly one undo
// entry of the last accepted state; a rejected Gt between them changes
// nothing; cancel restores without history.
TEST(SceneInteractionModule, GizmoUiDragCommitsTheLastAcceptedStateAsOneUndoAndCancelLeavesNone)
{
    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    DirectHarness harness;
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::EditorCommandHistory& history = *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(harness.Services.Find<Runtime::SelectionController>()->SetSelectedEntity(scene, entity));
    (void)BindMainCamera(harness);
    Runtime::SceneInteractionModule& module = harness.Interaction;
    const auto positionX = [&] { return scene.Raw().get<Tf::Component>(entity).Position.x; };
    const auto translateX = [](const float x, const glm::mat4& g0)
    { return glm::translate(glm::mat4{1.0f}, glm::vec3{x, 0.0f, 0.0f}) * g0; };

    const Runtime::GizmoUiFrame frame = module.PrepareGizmo(kGlobal, kOrigins);
    ASSERT_TRUE(frame.Available());
    const glm::mat4 g0 = frame.Frame.Matrix;
    const Runtime::GizmoUiBeginResult begun =
        module.BeginGizmoDrag(frame.Token, Runtime::GizmoMode::Translate, kGlobal, kOrigins);
    ASSERT_TRUE(begun.Succeeded());
    for (int step = 1; step <= 10; ++step)
        ASSERT_TRUE(module.PreviewGizmoDrag(begun.Token, translateX(static_cast<float>(step) / 10.0f, g0))
                        .Succeeded());
    glm::mat4 perspective = translateX(4.0f, g0);
    perspective[0][3] = 0.5f;
    EXPECT_EQ(module.PreviewGizmoDrag(begun.Token, perspective).Status, Runtime::GizmoStatus::NonTrsResult);
    EXPECT_FLOAT_EQ(positionX(), 1.0f);
    EXPECT_EQ(module.PrepareGizmo(kGlobal, kOrigins).GizmoMatrix, translateX(1.0f, g0));
    EXPECT_EQ(history.UndoCount(), 0u);

    // Release after the rejected tick commits the last accepted state, once.
    EXPECT_TRUE(module.CommitGizmoDrag(begun.Token).Succeeded());
    EXPECT_FALSE(module.Interaction().IsDragging());
    EXPECT_FLOAT_EQ(positionX(), 1.0f);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_NE(module.PrepareGizmo(kGlobal, kOrigins).Token, begun.Token);
    EXPECT_EQ(module.CommitGizmoDrag(begun.Token).Status, Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(history.UndoCount(), 1u);
    // Neither the finished session's token nor the idle token prepared
    // before it starts a new session.
    for (const Runtime::GizmoUiToken& old : {begun.Token, frame.Token})
    {
        EXPECT_EQ(module.BeginGizmoDrag(old, Runtime::GizmoMode::Translate, kGlobal, kOrigins).Result.Status,
                  Runtime::GizmoStatus::StaleSession);
        EXPECT_FALSE(module.Interaction().IsDragging());
    }
    EXPECT_FLOAT_EQ(positionX(), 1.0f);
    ASSERT_EQ(history.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);

    const Runtime::GizmoUiBeginResult again = module.BeginGizmoDrag(
        module.PrepareGizmo(kGlobal, kOrigins).Token, Runtime::GizmoMode::Translate, kGlobal, kOrigins);
    ASSERT_TRUE(again.Succeeded());
    EXPECT_NE(again.Token, begun.Token);
    ASSERT_TRUE(module.PreviewGizmoDrag(again.Token, translateX(2.0f, g0)).Succeeded());
    EXPECT_FLOAT_EQ(positionX(), 2.0f);
    EXPECT_EQ(module.CancelGizmoDrag(again.Token).Status, Runtime::GizmoStatus::Ok);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_FALSE(module.Interaction().IsDragging());
}

// UI-078 slice 3b: after a lifecycle cancel (UI hide, focus loss, document
// replacement, world switch) the frontend's token is stale: Preview, Commit,
// Cancel and Begin write, record and start nothing, a later session never
// accepts it, and an idle token prepared before another session (or a world
// switch) cannot begin.
TEST(SceneInteractionModule, StaleGizmoUiTokensAreRejectedWithoutWrites)
{
    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    constexpr auto kTranslate = Runtime::GizmoMode::Translate;
    DirectHarness harness;
    harness.UiHost = std::make_unique<Runtime::EditorUiHost>();
    Runtime::EditorUiHostOwnerControl owner = harness.UiHost->ClaimOwnerControl();
    owner.SetOperational(true);
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    ASSERT_EQ(harness.FrameHooks[2].Phase, Runtime::FramePhase::UiBegin);
    Runtime::SelectionController& selection = *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory& history = *harness.Services.Find<Runtime::EditorCommandHistory>();
    Runtime::SceneInteractionModule& module = harness.Interaction;
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
    (void)BindMainCamera(harness);
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    const auto positionX = [&] { return scene.Raw().get<Tf::Component>(entity).Position.x; };

    // Begins a session and accepts a move of +1 along X.
    const auto beginMoved = [&]
    {
        const Runtime::GizmoUiFrame frame = module.PrepareGizmo(kGlobal, kOrigins);
        const Runtime::GizmoUiBeginResult begun = module.BeginGizmoDrag(frame.Token, kTranslate, kGlobal, kOrigins);
        EXPECT_TRUE(begun.Succeeded());
        EXPECT_TRUE(module.PreviewGizmoDrag(
            begun.Token, glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}) * frame.Frame.Matrix)
                        .Succeeded());
        return begun.Token;
    };
    const glm::mat4 farAway = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f});
    const auto expectStale = [&](const Runtime::GizmoUiToken& token)
    {
        EXPECT_EQ(module.PreviewGizmoDrag(token, farAway).Status, Runtime::GizmoStatus::StaleSession);
        EXPECT_EQ(module.CommitGizmoDrag(token).Status, Runtime::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(module.CancelGizmoDrag(token).Status, Runtime::GizmoStatus::StaleSession);
        EXPECT_EQ(module.BeginGizmoDrag(token, kTranslate, kGlobal, kOrigins).Result.Status,
                  Runtime::GizmoStatus::StaleSession);
        EXPECT_FALSE(module.Interaction().IsDragging());
        EXPECT_EQ(history.UndoCount(), 0u);
    };

    // UI hide, observed at UiBegin.
    Runtime::GizmoUiToken token = beginMoved();
    (void)harness.UiHost->ApplyVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Hide});
    harness.InvokeFrameHook(2u, capture, pacing);
    EXPECT_FALSE(module.Interaction().IsDragging());
    expectStale(token);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);
    (void)harness.UiHost->ApplyVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Show});
    harness.InvokeFrameHook(2u, capture, pacing);

    // Native focus loss.
    token = beginMoved();
    harness.Events.Publish(Platform::WindowFocusEvent{.Focused = false});
    (void)harness.Events.Pump();
    expectStale(token);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);

    // A later session on the same binding never accepts the older token.
    const Runtime::GizmoUiToken fresh = beginMoved();
    EXPECT_EQ(fresh.InteractionEpoch, token.InteractionEpoch);
    EXPECT_EQ(module.PreviewGizmoDrag(token, farAway).Status, Runtime::GizmoStatus::StaleSession);
    EXPECT_EQ(module.CommitGizmoDrag(token).Status, Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(module.CancelGizmoDrag(token).Status, Runtime::GizmoStatus::StaleSession);
    EXPECT_TRUE(module.Interaction().IsDragging());
    EXPECT_FLOAT_EQ(positionX(), 1.0f);
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_EQ(module.CancelGizmoDrag(fresh).Status, Runtime::GizmoStatus::Ok);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);

    // An idle token prepared before another session started and ended.
    const Runtime::GizmoUiToken idle = module.PrepareGizmo(kGlobal, kOrigins).Token;
    EXPECT_EQ(module.CancelGizmoDrag(beginMoved()).Status, Runtime::GizmoStatus::Ok);
    expectStale(idle);
    EXPECT_FLOAT_EQ(positionX(), 0.0f);

    // Document replacement.
    token = beginMoved();
    ASSERT_TRUE(harness.Document->NewSceneDocument().has_value());
    EXPECT_FALSE(module.Interaction().IsDragging());
    expectStale(token);

    // World switch; a token prepared before it cannot begin either.
    ECS::Scene::Registry& first = *harness.Worlds.Get(harness.Worlds.ActiveWorld());
    entity = MakeTransformSelectable(first);
    ASSERT_TRUE(selection.SetSelectedEntity(first, entity));
    const auto firstX = [&] { return first.Raw().get<Tf::Component>(entity).Position.x; };
    const Runtime::GizmoUiToken prepared = module.PrepareGizmo(kGlobal, kOrigins).Token;
    token = beginMoved();
    EXPECT_FLOAT_EQ(firstX(), 1.0f);
    const Runtime::WorldHandle second = harness.Worlds.CreateWorld("Second");
    ASSERT_TRUE(harness.Worlds.RequestSetActiveWorld(second).has_value());
    (void)harness.Worlds.ApplyMaintenance(harness.Events, harness.Jobs);
    (void)harness.Events.Pump();
    expectStale(token);
    EXPECT_FLOAT_EQ(firstX(), 0.0f);
    EXPECT_EQ(module.BeginGizmoDrag(prepared, kTranslate, kGlobal, kOrigins).Result.Status,
              Runtime::GizmoStatus::StaleSession);
    EXPECT_FALSE(module.Interaction().IsDragging());
}

// UI-078 slice 3b: selecting, preparing frames and running frames (cursor
// over the gizmo, no button) never starts a session or writes a transform.
TEST(SceneInteractionModule, SelectionAloneDoesNotStartAGizmoSession)
{
    DirectHarness harness;
    harness.UiHost = std::make_unique<Runtime::EditorUiHost>();
    Runtime::EditorUiHostOwnerControl owner = harness.UiHost->ClaimOwnerControl();
    owner.SetOperational(true);
    harness.Cameras = std::make_unique<Runtime::CameraControllerRegistry>();
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::EditorCommandHistory& history = *harness.Services.Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene = *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity = MakeTransformSelectable(scene, glm::vec3{0.5f, 0.0f, 0.0f});
    ASSERT_TRUE(harness.Services.Find<Runtime::SelectionController>()->SetSelectedEntity(scene, entity));
    (void)BindMainCamera(harness);

    Graphics::RenderFrameInput input{};
    input.Camera = OrthoCameraInput();
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    harness.InputWindow().QueueCursor(450.0, 300.0);
    harness.InputWindow().PollEvents();
    for (int frame = 0; frame < 3; ++frame)
    {
        harness.InvokeFrameHook(2u, capture, pacing);
        const Runtime::GizmoUiFrame ui = harness.Interaction.PrepareGizmo(
            Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::WorldOrigins);
        EXPECT_TRUE(ui.Available());
        EXPECT_FALSE(ui.Dragging);
        EXPECT_EQ(ui.Token.Session, 0u);
        harness.InvokeViewportHook(0u, input, {}, Platform::Extent2D{.Width = 800, .Height = 600});
    }
    EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    EXPECT_EQ(harness.Interaction.Interaction().Diagnostics().DragsStarted, 0u);
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3(0.5f, 0.0f, 0.0f));
    EXPECT_EQ(history.UndoCount(), 0u);
}

TEST(SceneInteractionModule,
     ShutdownAnnouncementReleasesDocumentParticipant)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.ProvideBuiltins().has_value());
    harness.Document =
        std::make_unique<Runtime::SceneDocumentModule>();
    Runtime::EngineSetup setup = harness.MakeSetup();
    ASSERT_TRUE(
        harness.Interaction.OnRegister(setup)
            .has_value());

    Runtime::SceneReplacementParticipantHandle
        sameName{};
    const Runtime::KernelEventSubscription probe =
        setup.Subscribe<Runtime::RuntimeShutdownAnnounced>(
            [&harness, &sameName](
                const Runtime::RuntimeShutdownAnnounced&)
            {
                auto registered =
                    harness.Document
                        ->RegisterReplacementParticipant(
                            Runtime::
                                SceneReplacementParticipantDesc{
                                    .Name =
                                        "Runtime.SceneInteractionModule",
                                    .BeforeReplace = {},
                                    .AfterReplace = {},
                                });
                if (registered.has_value())
                    sameName = *registered;
            });
    ASSERT_TRUE(probe.IsValid());
    ASSERT_TRUE(
        harness.Document->OnRegister(setup)
            .has_value());
    harness.Services.BeginResolution();
    ASSERT_TRUE(
        harness.ResolveDocument().has_value());
    ASSERT_TRUE(
        harness.ResolveInteraction().has_value());
    harness.Services.Lock();
    harness.Started = true;

    // Delivery order is deliberate: interaction registered first, this
    // probe second, and the document provider third. The interaction
    // announcement callback must release its strong participant before the
    // provider quiesces, allowing the exact same name to be registered in
    // the intervening live-provider callback.
    harness.Announce();
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneInteractionModule>(),
        &harness.Interaction);
    ASSERT_TRUE(sameName.IsValid());
    EXPECT_TRUE(
        harness.Document
            ->UnregisterReplacementParticipant(sameName)
            .has_value());
    harness.Events.Unsubscribe(probe);
}

TEST(SceneInteractionModule,
     DocumentReplacementClearsExactlyOnceAndKeepsSequenceMonotonic)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services
             .Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);

    const ECS::EntityHandle entity =
        MakeSelectable(scene);
    ASSERT_TRUE(
        selection.SetSelectedEntity(scene, entity));
    selection.RequestClickPick(3u, 4u);
    const auto before =
        selection.ConsumePendingPick();
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(selection.InFlightPickCount(), 1u);
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        0u);

    ASSERT_TRUE(
        harness.Document->NewSceneDocument()
            .has_value());

    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        1u);
    selection.RequestClickPick(5u, 6u);
    const auto after =
        selection.ConsumePendingPick();
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->Sequence, before->Sequence);
}

TEST(SceneInteractionModule,
     CloseAndLoadClearOneCohortAndRebuildLookup)
{
    ScopedScenePath saved{
        "intrinsic-runtime-188-interaction.scene.json"};
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services
             .Find<Runtime::SelectionController>();
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);

    const ECS::EntityHandle entity =
        MakeSelectable(scene);
    const ECSC::StableId durable{
        0x188u, 0xC105Eu};
    scene.Raw().emplace<ECSC::StableId>(
        entity, durable);
    ASSERT_TRUE(
        harness.Document
            ->SaveSceneToPath(saved.Path.string())
            .has_value());
    ASSERT_TRUE(
        selection.SetSelectedEntity(scene, entity));
    selection.RequestClickPick(7u, 8u);
    const auto before =
        selection.ConsumePendingPick();
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(
        harness.Interaction
            .ResolveEntityByStableId(durable)
            .has_value());

    ASSERT_TRUE(
        harness.Document->CloseSceneDocument()
            .has_value());
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_FALSE(
        harness.Interaction
            .ResolveEntityByStableId(durable)
            .has_value());
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        1u);

    ASSERT_TRUE(
        harness.Document
            ->LoadSceneFromPath(saved.Path.string())
            .has_value());
    const auto loaded =
        harness.Interaction
            .ResolveEntityByStableId(durable);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(scene.IsValid(*loaded));
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        2u);

    selection.RequestClickPick(9u, 10u);
    const auto after =
        selection.ConsumePendingPick();
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->Sequence, before->Sequence);
}

TEST(SceneInteractionModule,
     ActiveWorldMismatchClearsWithoutResurrectionAndIgnoresInactiveRetirement)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    Runtime::SelectionController& selection =
        *harness.Services
             .Find<Runtime::SelectionController>();
    ECS::Scene::Registry& firstScene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle firstEntity =
        MakeSelectable(firstScene);
    const ECSC::StableId durable{
        0x188u, 0xA001u};
    firstScene.Raw().emplace<ECSC::StableId>(
        firstEntity, durable);
    ASSERT_TRUE(
        selection.SetSelectedEntity(
            firstScene, firstEntity));
    selection.RequestClickPick(1u, 1u);
    const auto firstSequence =
        selection.ConsumePendingPick();
    ASSERT_TRUE(firstSequence.has_value());

    const Runtime::WorldHandle secondWorld =
        harness.Worlds.CreateWorld("Second");
    ASSERT_TRUE(
        harness.Worlds
            .RequestSetActiveWorld(secondWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);

    // No event pump has run. The exact module API validates the
    // WorldRegistry binding directly and clears before lookup.
    EXPECT_FALSE(
        harness.Interaction
            .ResolveEntityByStableId(durable)
            .has_value());
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_EQ(
        harness.Interaction
            .LastRefinedPrimitiveGeneration(),
        1u);

    ECS::Scene::Registry& secondScene =
        *harness.Worlds.Get(secondWorld);
    const ECS::EntityHandle secondEntity =
        MakeSelectable(secondScene);
    ASSERT_TRUE(
        selection.SetSelectedEntity(
            secondScene, secondEntity));
    selection.RequestClickPick(2u, 2u);
    const auto secondSequence =
        selection.ConsumePendingPick();
    ASSERT_TRUE(secondSequence.has_value());
    EXPECT_GT(
        secondSequence->Sequence,
        firstSequence->Sequence);

    ASSERT_TRUE(
        harness.Worlds
            .RequestSetActiveWorld(
                harness.InitialWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Interaction
        .ResolveEntityByStableId(durable);
    EXPECT_EQ(selection.SelectedCount(), 0u);

    ASSERT_TRUE(
        harness.Worlds
            .RequestSetActiveWorld(secondWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Interaction
        .ResolveEntityByStableId(durable);
    EXPECT_EQ(selection.SelectedCount(), 0u);

    ASSERT_TRUE(
        selection.SetSelectedEntity(
            secondScene, secondEntity));
    const Runtime::WorldHandle neverActive =
        harness.Worlds.CreateWorld("Never active");
    ASSERT_TRUE(
        harness.Worlds
            .RequestDestroyWorld(neverActive)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Events.Pump();
    EXPECT_TRUE(selection.IsSelected(secondEntity));

    ASSERT_TRUE(
        harness.Worlds
            .RequestDestroyWorld(
                harness.InitialWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Events.Pump();
    EXPECT_TRUE(selection.IsSelected(secondEntity));
}

TEST(SceneInteractionModule,
     OptionalOmissionAndComposedOperationalRun)
{
    {
        auto application =
            std::make_unique<
                ExitAfterOneFrameApplication>();
        ExitAfterOneFrameApplication* const app =
            application.get();
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
        engine.Initialize();

        EXPECT_EQ(
            engine.Services()
                .Find<Runtime::SceneInteractionModule>(),
            nullptr);
        EXPECT_EQ(
            engine.Services()
                .Find<Runtime::SelectionController>(),
            nullptr);
        ASSERT_FALSE(engine.GetWindow().ShouldClose());
        engine.Run();
        EXPECT_EQ(app->VariableTicks, 1u);
        engine.Shutdown();
    }

    {
        auto application =
            std::make_unique<
                ExitAfterOneFrameApplication>();
        ExitAfterOneFrameApplication* const app =
            application.get();
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
        engine.EmplaceModule<
            Runtime::SceneInteractionModule>();
        engine.Initialize();

        ASSERT_NE(
            engine.Services()
                .Find<Runtime::SceneInteractionModule>(),
            nullptr);
        ASSERT_FALSE(engine.GetWindow().ShouldClose());
        engine.Run();
        EXPECT_EQ(app->VariableTicks, 1u);
        engine.Shutdown();
    }
}

TEST(SceneInteractionModule,
     ShutdownReinitializeStartsEmptyWithRecycledBootHandle)
{
    auto application =
        std::make_unique<ExitAfterOneFrameApplication>();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
    engine.EmplaceModule<
        Runtime::SceneInteractionModule>();
    engine.Initialize();

    const Runtime::WorldHandle firstWorld =
        engine.ActiveWorld();
    Runtime::SelectionController& firstSelection =
        *engine.Services()
             .Find<Runtime::SelectionController>();
    ECS::Scene::Registry& firstScene =
        *engine.Worlds().Get(firstWorld);
    ASSERT_TRUE(firstSelection.SetSelectedEntity(
        firstScene, MakeSelectable(firstScene)));
    engine.Shutdown();

    engine.Initialize();
    EXPECT_EQ(engine.ActiveWorld(), firstWorld);
    Runtime::SelectionController* const selection =
        engine.Services()
            .Find<Runtime::SelectionController>();
    Runtime::SceneInteractionModule* const interaction =
        engine.Services()
            .Find<Runtime::SceneInteractionModule>();
    ASSERT_NE(selection, nullptr);
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(selection->SelectedCount(), 0u);
    EXPECT_EQ(selection->InFlightPickCount(), 0u);
    EXPECT_EQ(
        interaction->LastRefinedPrimitiveGeneration(),
        0u);
    engine.Shutdown();
}

// UI-078 slice 3b: Shutdown/Initialize recreates the interaction state on
// the recycled boot WorldHandle; gizmo tokens from before (idle, finished and
// still-running sessions) start, preview, commit and cancel nothing after it,
// also once a new session reaches the same session generation.
TEST(SceneInteractionModule, GizmoUiTokensFromBeforeShutdownInitializeAreStale)
{
    constexpr auto kGlobal = Runtime::GizmoOrientation::Global;
    constexpr auto kOrigins = Runtime::GizmoPivotMode::WorldOrigins;
    constexpr auto kTranslate = Runtime::GizmoMode::Translate;
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<ExitAfterOneFrameApplication>());
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    const glm::mat4 moved = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});

    std::vector<Runtime::GizmoUiToken> old{};
    {
        auto& module = *engine.Services().Find<Runtime::SceneInteractionModule>();
        ECS::Scene::Registry& scene = *engine.Worlds().Get(engine.ActiveWorld());
        ASSERT_TRUE(engine.Services().Find<Runtime::SelectionController>()->SetSelectedEntity(
            scene, MakeTransformSelectable(scene)));
        old.push_back(module.PrepareGizmo(kGlobal, kOrigins).Token);
        const Runtime::GizmoUiBeginResult finished = module.BeginGizmoDrag(old.back(), kTranslate, kGlobal, kOrigins);
        ASSERT_TRUE(finished.Succeeded());
        ASSERT_TRUE(module.PreviewGizmoDrag(finished.Token, moved).Succeeded());
        ASSERT_EQ(module.CancelGizmoDrag(finished.Token).Status, Runtime::GizmoStatus::Ok);
        old.push_back(finished.Token);
        const Runtime::GizmoUiBeginResult running = module.BeginGizmoDrag(
            module.PrepareGizmo(kGlobal, kOrigins).Token, kTranslate, kGlobal, kOrigins);
        ASSERT_TRUE(running.Succeeded());
        ASSERT_TRUE(module.PreviewGizmoDrag(running.Token, moved).Succeeded());
        old.push_back(running.Token);
    }
    const Runtime::WorldHandle bootWorld = engine.ActiveWorld();
    engine.Shutdown();
    engine.Initialize();
    ASSERT_EQ(engine.ActiveWorld(), bootWorld);

    auto& module = *engine.Services().Find<Runtime::SceneInteractionModule>();
    Runtime::EditorCommandHistory& history = *engine.Services().Find<Runtime::EditorCommandHistory>();
    ECS::Scene::Registry& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const ECS::EntityHandle entity = MakeTransformSelectable(scene);
    ASSERT_TRUE(engine.Services().Find<Runtime::SelectionController>()->SetSelectedEntity(scene, entity));
    for (const Runtime::GizmoUiToken& token : old)
    {
        EXPECT_EQ(module.BeginGizmoDrag(token, kTranslate, kGlobal, kOrigins).Result.Status,
                  Runtime::GizmoStatus::StaleSession);
        EXPECT_FALSE(module.Interaction().IsDragging());
    }

    // A new session reaches the generation the finished one had.
    const Runtime::GizmoUiBeginResult fresh =
        module.BeginGizmoDrag(module.PrepareGizmo(kGlobal, kOrigins).Token, kTranslate, kGlobal, kOrigins);
    ASSERT_TRUE(fresh.Succeeded());
    for (const Runtime::GizmoUiToken& token : old)
    {
        EXPECT_EQ(module.PreviewGizmoDrag(token, moved).Status, Runtime::GizmoStatus::StaleSession);
        EXPECT_EQ(module.CommitGizmoDrag(token).Status, Runtime::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(module.CancelGizmoDrag(token).Status, Runtime::GizmoStatus::StaleSession);
        EXPECT_TRUE(module.Interaction().IsDragging());
    }
    EXPECT_EQ(scene.Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_EQ(module.CancelGizmoDrag(fresh.Token).Status, Runtime::GizmoStatus::Ok);
    engine.Shutdown();
}

TEST(SceneInteractionModule, PrimitiveClicksReachSharedSelectionAndRejectChangedTopology)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());
    auto& selection = *harness.Services.Find<Runtime::SelectionController>();
    auto& scene = *harness.Worlds.Get(harness.InitialWorld);
    namespace GS = ECS::Components::GeometrySources;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    const auto entity = MakeSelectable(scene);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Vertex;
    auto& window = harness.InputWindow();
    auto issue = [&](bool shift, bool control) {
        window.QueueMouseButton(0, false);
        window.PollEvents();
        window.QueueKey(Platform::Input::Key::LeftShift, shift);
        window.QueueKey(Platform::Input::Key::LeftControl, control);
        window.QueueCursor(12, 12);
        window.QueueMouseButton(0, true);
        window.PollEvents();
        Graphics::RenderFrameInput input{};
        harness.InvokeViewportHook(0, input, capture);
        harness.InvokeFrameHook(0, capture, pacing);
        EXPECT_TRUE(input.HasPendingPick);
        (void)harness.Renderer->GetSelectionSystem().ConsumePick();
        return input.Pick.Sequence;
    };
    auto complete = [&](std::uint64_t sequence, std::uint32_t vertex) {
        harness.Renderer->GetSelectionSystem().PublishPickResult({
            .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Point, vertex),
            .StableEntityId = id, .Hit = true, .Sequence = sequence});
        harness.InvokeFrameHook(1, capture, pacing);
    };
    complete(issue(false, false), 2);
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{2}));
    complete(issue(true, false), 0);
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{2, 0}));
    complete(issue(false, true), 2);
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{0}));
    selection.ClearPrimitives();
    const auto first = issue(true, false), second = issue(true, false);
    harness.Renderer->GetSelectionSystem().PublishPickResult({
        .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Point, 0),
        .StableEntityId = id, .Hit = true, .Sequence = second});
    complete(first, 1);
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{1, 0}));
    const auto pending = issue(true, false);
    scene.Raw().get<GS::Halfedges>(entity).Properties.Get<std::uint32_t>("h:to_vertex")[0] = 2;
    complete(pending, 1);
    EXPECT_EQ(selection.InFlightPickCount(), 0);
    EXPECT_TRUE(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices.empty());
    harness.InvokeFrameHook(0, capture, pacing);
    EXPECT_TRUE(selection.PrimitiveSnapshots(scene).empty());
}

// GRAPHICS-156 (ADR 0030 decision 5): while an entity shows uncommitted GPU positions (a
// position ring front observed by extraction), a primitive pick resolves to the entity only;
// once the front is gone refinement resumes on the CPU geometry.
TEST(SceneInteractionModule, PrimitiveRefinementIsOffWhileUncommittedPositionsAreShown)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());
    auto& selection = *harness.Services.Find<Runtime::SelectionController>();
    auto& scene = *harness.Worlds.Get(harness.InitialWorld);
    namespace GS = ECS::Components::GeometrySources;
    namespace G = Extrinsic::Graphics::Components;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    const auto entity = MakeSelectable(scene);
    scene.Raw().emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    scene.Raw().emplace<G::RenderSurface>(entity);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    const auto renderId = Runtime::StableEntityLookup::ToRenderId(entity);

    // A fake residency: `v:position` has a ring front while `front` is set.
    std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> front{
        Runtime::RenderExtractionCache::GpuPropertyFront{.Buffer = Extrinsic::RHI::BufferHandle{9u, 1u},
                                                         .Address = 0x9000u, .Bytes = 36u, .Count = 3u, .Stamp = 1u}};
    harness.Extraction.SetGpuPropertyObserver(
        [&front](Runtime::WorldHandle, entt::entity, const Runtime::GeometryPropertyRef& ref)
            -> std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> {
            return ref.ValueKind == Geometry::PropertyValueKind::Vec3 ? front : std::nullopt;
        });
    // Production order per frame: the BeforeExtraction hook issues the pick, extraction
    // then builds the frame that renders it, and the Maintenance hook consumes readbacks
    // after a later frame's extraction. The pick below is issued on the very first
    // preview frame: no extraction has seen the front yet.
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Vertex;
    auto& window = harness.InputWindow();
    auto extract = [&]() { (void)harness.Extraction.ExtractAndSubmit(scene, *harness.Renderer); };
    auto issue = [&]() {
        window.QueueMouseButton(0, false);
        window.PollEvents();
        window.QueueCursor(12, 12);
        window.QueueMouseButton(0, true);
        window.PollEvents();
        Graphics::RenderFrameInput input{};
        harness.InvokeViewportHook(0, input, capture);
        harness.InvokeFrameHook(0, capture, pacing);
        EXPECT_TRUE(input.HasPendingPick);
        (void)harness.Renderer->GetSelectionSystem().ConsumePick();
        extract();
        return input.Pick.Sequence;
    };
    auto complete = [&](std::uint64_t sequence, std::uint32_t vertex) {
        harness.Renderer->GetSelectionSystem().PublishPickResult({
            .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Point, vertex),
            .StableEntityId = id, .Hit = true, .Sequence = sequence});
        harness.InvokeFrameHook(1, capture, pacing);
    };

    // Uncommitted positions: the vertex hint is not refined against the CPU geometry, so
    // the vertex pick edits nothing; an entity pick still selects the entity.
    EXPECT_FALSE(harness.Extraction.ShowsUncommittedPositions(renderId)) << "no extraction yet";
    EXPECT_TRUE(harness.Extraction.ObservesUncommittedPositions(scene, harness.Worlds.ActiveWorld(), renderId));
    complete(issue(), 2);
    ASSERT_TRUE(harness.Extraction.ShowsUncommittedPositions(renderId));
    EXPECT_TRUE(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices.empty());
    EXPECT_FALSE(selection.IsSelected(entity));
    EXPECT_FALSE(harness.Interaction.LastRefinedPrimitive().has_value());
    EXPECT_EQ(harness.Interaction.LastRefinedPrimitiveGeneration(), 1u);
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Entity;
    complete(issue(), 2);
    EXPECT_TRUE(selection.IsSelected(entity));
    EXPECT_TRUE(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices.empty());
    EXPECT_FALSE(harness.Interaction.LastRefinedPrimitive().has_value());
    EXPECT_EQ(harness.Interaction.LastRefinedPrimitiveGeneration(), 2u);

    // The front is gone (Discard) on the frame that issues the next vertex pick: it
    // resolves the vertex again although the last extraction still showed the preview.
    front.reset();
    ASSERT_TRUE(harness.Extraction.ShowsUncommittedPositions(renderId));
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Vertex;
    complete(issue(), 2);
    ASSERT_FALSE(harness.Extraction.ShowsUncommittedPositions(renderId));
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{2}));
    ASSERT_TRUE(harness.Interaction.LastRefinedPrimitive().has_value());
    harness.Extraction.SetGpuPropertyObserver({});
    harness.Extraction.Shutdown(*harness.Renderer);
}

// GRAPHICS-156: a primitive pick's pixels were rendered from one preview state; when that
// state changes before the readback (Discard before the readback landed, or a preview that
// started meanwhile) the CPU geometry does not describe the pick, so the pick is discarded
// instead of refined against the restored (or superseded) positions.
TEST(SceneInteractionModule, APreviewTransitionBetweenPickAndReadbackDiscardsThePrimitivePick)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());
    auto& selection = *harness.Services.Find<Runtime::SelectionController>();
    auto& scene = *harness.Worlds.Get(harness.InitialWorld);
    namespace GS = ECS::Components::GeometrySources;
    namespace G = Extrinsic::Graphics::Components;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    const auto entity = MakeSelectable(scene);
    scene.Raw().emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    scene.Raw().emplace<G::RenderSurface>(entity);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> front{
        Runtime::RenderExtractionCache::GpuPropertyFront{.Buffer = Extrinsic::RHI::BufferHandle{9u, 1u},
                                                         .Address = 0x9000u, .Bytes = 36u, .Count = 3u, .Stamp = 1u}};
    harness.Extraction.SetGpuPropertyObserver(
        [&front](Runtime::WorldHandle, entt::entity, const Runtime::GeometryPropertyRef& ref)
            -> std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> {
            return ref.ValueKind == Geometry::PropertyValueKind::Vec3 ? front : std::nullopt;
        });
    // Production order: the BeforeExtraction hook issues the pick, then the frame's
    // extraction runs; readbacks are consumed by the Maintenance hook of a later frame.
    auto extract = [&]() { (void)harness.Extraction.ExtractAndSubmit(scene, *harness.Renderer); };
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    harness.SelectionSettings.Target = Runtime::SelectionTarget::Vertex;
    auto& window = harness.InputWindow();
    auto issue = [&]() {
        window.QueueMouseButton(0, false);
        window.PollEvents();
        window.QueueCursor(12, 12);
        window.QueueMouseButton(0, true);
        window.PollEvents();
        Graphics::RenderFrameInput input{};
        harness.InvokeViewportHook(0, input, capture);
        harness.InvokeFrameHook(0, capture, pacing);
        EXPECT_TRUE(input.HasPendingPick);
        (void)harness.Renderer->GetSelectionSystem().ConsumePick();
        extract();
        return input.Pick.Sequence;
    };
    auto complete = [&](std::uint64_t sequence, std::uint32_t vertex) {
        harness.Renderer->GetSelectionSystem().PublishPickResult({
            .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Point, vertex),
            .StableEntityId = id, .Hit = true, .Sequence = sequence});
        harness.InvokeFrameHook(1, capture, pacing);
    };

    // Picked on the first preview frame (no extraction has seen the front yet), Discard
    // on a later frame before the readback: discarded, not refined against the restored
    // CPU positions.
    const auto duringPreview = issue();
    ASSERT_TRUE(harness.Extraction.ShowsUncommittedPositions(Runtime::StableEntityLookup::ToRenderId(entity)));
    front.reset();
    extract();
    ASSERT_FALSE(harness.Extraction.ShowsUncommittedPositions(Runtime::StableEntityLookup::ToRenderId(entity)));
    complete(duringPreview, 2);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_TRUE(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices.empty());
    EXPECT_FALSE(selection.IsSelected(entity));
    EXPECT_FALSE(harness.Interaction.LastRefinedPrimitive().has_value());
    EXPECT_EQ(harness.Interaction.LastRefinedPrimitiveGeneration(), 0u);

    // Picked without a preview, a preview starts on a later frame before the readback:
    // discarded too.
    const auto beforePreview = issue();
    front = Runtime::RenderExtractionCache::GpuPropertyFront{.Buffer = Extrinsic::RHI::BufferHandle{9u, 1u},
                                                             .Address = 0x9000u, .Bytes = 36u, .Count = 3u, .Stamp = 2u};
    extract();
    complete(beforePreview, 2);
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_TRUE(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices.empty());
    EXPECT_EQ(harness.Interaction.LastRefinedPrimitiveGeneration(), 0u);

    // Steady state without a preview: the same pick refines.
    front.reset();
    complete(issue(), 2);
    EXPECT_EQ(selection.ReadPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex).Indices,
              (std::vector<std::uint32_t>{2}));
    harness.Extraction.SetGpuPropertyObserver({});
    harness.Extraction.Shutdown(*harness.Renderer);
}

// GRAPHICS-156: selected-primitive highlights are built from the CPU positions, so while
// an entity shows uncommitted GPU positions the module submits none for it.
TEST(SceneInteractionModule, PrimitiveHighlightsAreSuppressedWhileUncommittedPositionsAreShown)
{
    DirectHarness harness;
    harness.InitializeRendererForHooks();
    ASSERT_TRUE(harness.Start().has_value());
    auto& selection = *harness.Services.Find<Runtime::SelectionController>();
    auto& scene = *harness.Worlds.Get(harness.InitialWorld);
    namespace GS = ECS::Components::GeometrySources;
    namespace G = Extrinsic::Graphics::Components;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    const auto entity = MakeSelectable(scene);
    scene.Raw().emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    scene.Raw().emplace<G::RenderSurface>(entity);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    const std::array<std::uint32_t, 2> vertices{0u, 2u};
    ASSERT_TRUE(selection.EditPrimitives(scene, id, Runtime::GeometryElementDomain::MeshVertex,
                                         Runtime::PrimitiveSelectionEdit::Replace, vertices).Usable());
    std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> front{};
    harness.Extraction.SetGpuPropertyObserver(
        [&front](Runtime::WorldHandle, entt::entity, const Runtime::GeometryPropertyRef& ref)
            -> std::optional<Runtime::RenderExtractionCache::GpuPropertyFront> {
            return ref.ValueKind == Geometry::PropertyValueKind::Vec3 ? front : std::nullopt;
        });
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    // Production order: the BeforeExtraction hook submits the interaction snapshot, then
    // the frame's extraction hands it to the renderer. The first preview frame (and the
    // first frame after Discard) must already be right, before any extraction saw the
    // change.
    auto highlightPoints = [&]() {
        harness.InvokeFrameHook(0, capture, pacing);
        (void)harness.Extraction.ExtractAndSubmit(scene, *harness.Renderer, nullptr, 0u, harness.Worlds.ActiveWorld());
        return harness.Renderer->ExtractRenderWorld(Graphics::RenderFrameInput{}).DebugPrimitives.Points.size();
    };
    EXPECT_EQ(highlightPoints(), 2u) << "control: the two selected vertices are highlighted";
    front = Runtime::RenderExtractionCache::GpuPropertyFront{.Buffer = Extrinsic::RHI::BufferHandle{9u, 1u},
                                                             .Address = 0x9000u, .Bytes = 36u, .Count = 3u, .Stamp = 1u};
    EXPECT_EQ(highlightPoints(), 0u) << "no highlight on the first frame that shows uncommitted positions";
    EXPECT_EQ(highlightPoints(), 0u);
    front.reset();
    EXPECT_EQ(highlightPoints(), 2u) << "the highlight returns on the first frame after Discard";
    harness.Extraction.SetGpuPropertyObserver({});
    harness.Extraction.Shutdown(*harness.Renderer);
}

// METHOD-047: with an editor pane beside the scene, a click is picked only
// inside the scene rectangle and in rectangle-local framebuffer pixels.
TEST(SceneInteractionModule, ClickPickUsesSceneRectangleLocalPixels)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    auto& selection = *harness.Services.Find<Runtime::SelectionController>();
    auto& window = harness.InputWindow();
    const Platform::Extent2D framebuffer = window.GetFramebufferExtent();
    ASSERT_EQ(framebuffer.Width, window.GetWindowExtent().Width);
    const Core::Offset2D origin{.X = framebuffer.Width / 2, .Y = 0};
    const Platform::Extent2D scene{.Width = framebuffer.Width - origin.X, .Height = framebuffer.Height};

    auto click = [&](const double x, const double y) {
        window.QueueMouseButton(0, false);
        window.PollEvents();
        window.QueueCursor(x, y);
        window.QueueMouseButton(0, true);
        window.PollEvents();
        Graphics::RenderFrameInput input{};
        harness.InvokeViewportHook(0u, input, {}, scene, origin, framebuffer);
    };

    click(static_cast<double>(origin.X) - 10.0, 40.0);
    EXPECT_FALSE(selection.HasPendingPick()) << "clicks over the atlas pane must not pick";

    click(static_cast<double>(origin.X) + 25.0, 40.0);
    const auto pick = selection.PeekPendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->PixelX, 25u);
    EXPECT_EQ(pick->PixelY, 40u);
}
