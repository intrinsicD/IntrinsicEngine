#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
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
                .Height = 32})
        {
            const Core::Config::EngineConfig config =
                HeadlessConfig();
            const Platform::Input::Context input{};
            Runtime::RuntimeViewportInputHookContext context{
                .Config = config,
                .ActiveWorldHandle =
                    Worlds.ActiveWorld(),
                .Input = input,
                .Viewport = viewport,
                .EditorCapture = capture,
                .RenderInput = renderInput,
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

        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Extrinsic::Tests::MockDevice Device{};
        std::unique_ptr<Platform::IWindow> Window{};
        std::unique_ptr<Graphics::IRenderer> Renderer{};
        Runtime::RenderExtractionCache Extraction{};
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
    EXPECT_EQ(harness.FrameHooks.size(), 2u);

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
        ASSERT_EQ(harness.FrameHooks.size(), 2u);

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
        ASSERT_EQ(harness.FrameHooks.size(), 4u);
        ASSERT_EQ(harness.ViewportHooks.size(), 1u);
        EXPECT_EQ(
            harness.FrameHooks[2].Phase,
            Runtime::FramePhase::BeforeExtraction);
        EXPECT_EQ(
            harness.FrameHooks[3].Phase,
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

        // The registrar has no unregister surface. The failed attempt's two
        // retained lambdas therefore remain in the harness, but their weak
        // state expired during rollback and both are inert.
        harness.InvokeFrameHook(0u, capture, pacing);
        harness.InvokeFrameHook(1u, capture, pacing);
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
        harness.InvokeFrameHook(2u, capture, pacing);
        harness.InvokeFrameHook(3u, capture, pacing);
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
    ASSERT_EQ(harness.FrameHooks.size(), 2u);
    EXPECT_EQ(
        harness.FrameHooks[0].Phase,
        Runtime::FramePhase::BeforeExtraction);
    EXPECT_EQ(
        harness.FrameHooks[1].Phase,
        Runtime::FramePhase::Maintenance);

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

TEST(SceneInteractionModule,
     ViewportGizmoRequiresHistoryHonorsCaptureAndCommitsOneUndoableDrag)
{
    {
        DirectHarness harness;
        ASSERT_TRUE(harness.Start().has_value());
        Runtime::SelectionController& selection =
            *harness.Services.Find<Runtime::SelectionController>();
        ECS::Scene::Registry& scene =
            *harness.Worlds.Get(harness.InitialWorld);
        const ECS::EntityHandle entity =
            MakeTransformSelectable(scene);
        ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));

        Graphics::RenderFrameInput input{};
        input.Camera = OrthoCameraInput();
        auto& window = harness.InputWindow();
        window.QueueCursor(450.0, 300.0);
        window.QueueMouseButton(0, true);
        window.PollEvents();
        harness.InvokeViewportHook(
            0u,
            input,
            {},
            Platform::Extent2D{.Width = 800, .Height = 600});
        EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    }

    DirectHarness harness;
    ASSERT_TRUE(harness.Start(true).has_value());
    Runtime::SelectionController& selection =
        *harness.Services.Find<Runtime::SelectionController>();
    Runtime::EditorCommandHistory* const history =
        harness.Services.Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ECS::Scene::Registry& scene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity =
        MakeTransformSelectable(scene);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));

    Graphics::RenderFrameInput input{};
    input.Camera = OrthoCameraInput();
    auto& window = harness.InputWindow();
    const Platform::Extent2D viewport{
        .Width = 800,
        .Height = 600,
    };
    Runtime::EditorInputCaptureSnapshot captured{
        .CapturedMouse = true,
    };

    window.QueueCursor(450.0, 300.0);
    window.QueueMouseButton(0, true);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, captured, viewport);
    EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    EXPECT_FALSE(selection.HasPendingPick());

    window.QueueMouseButton(0, false);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, captured, viewport);
    window.QueueMouseButton(0, true);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);
    ASSERT_TRUE(harness.Interaction.Interaction().IsDragging());

    window.QueueCursor(550.0, 300.0);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);
    EXPECT_GT(
        scene.Raw().get<Tf::Component>(entity).Position.x,
        0.0f);

    window.QueueMouseButton(0, false);
    window.PollEvents();
    harness.InvokeViewportHook(0u, input, {}, viewport);
    EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    ASSERT_EQ(history->UndoCount(), 1u);
    EXPECT_EQ(
        history->Undo().Status,
        Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(
        scene.Raw().get<Tf::Component>(entity).Position,
        glm::vec3{0.0f});
}

TEST(SceneInteractionModule,
     WorldSwitchCancelsDragAndPreservesGizmoTuning)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    ECS::Scene::Registry& firstScene =
        *harness.Worlds.Get(harness.InitialWorld);
    const ECS::EntityHandle entity =
        MakeTransformSelectable(
            firstScene,
            glm::vec3{1.0f, 0.0f, 0.0f});
    Runtime::GizmoInteraction& gizmo =
        harness.Interaction.Interaction();
    gizmo.Config().AxisLength = 2.5f;
    gizmo.SetMode(Runtime::GizmoMode::Scale);
    gizmo.SetOrientation(Runtime::GizmoOrientation::Local);
    const ECS::EntityHandle selected[] = {entity};
    ASSERT_TRUE(gizmo.BeginDrag(
        firstScene,
        Runtime::GizmoHitResult{
            .Hit = true,
            .Axis = Runtime::GizmoAxis::X,
            .Entity = entity,
        },
        Runtime::PickRay{
            .Origin = {2.0f, 0.0f, 5.0f},
            .Direction = {0.0f, 0.0f, -1.0f},
        },
        selected));
    ASSERT_TRUE(gizmo.DragTick(
        firstScene,
        Runtime::PickRay{
            .Origin = {3.0f, 0.0f, 5.0f},
            .Direction = {0.0f, 0.0f, -1.0f},
        }));
    EXPECT_FLOAT_EQ(
        firstScene.Raw().get<Tf::Component>(entity).Scale.x,
        2.0f);

    const Runtime::WorldHandle secondWorld =
        harness.Worlds.CreateWorld("Gizmo reset world");
    ASSERT_TRUE(
        harness.Worlds.RequestSetActiveWorld(secondWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Interaction.ResolveEntityByStableId(
        ECSC::StableId{0x205u, 0x61u});

    EXPECT_FALSE(harness.Interaction.Interaction().IsDragging());
    EXPECT_FLOAT_EQ(
        firstScene.Raw().get<Tf::Component>(entity).Scale.x,
        1.0f);
    EXPECT_EQ(
        harness.Interaction.Interaction().Mode(),
        Runtime::GizmoMode::Scale);
    EXPECT_EQ(
        harness.Interaction.Interaction().Orientation(),
        Runtime::GizmoOrientation::Local);
    EXPECT_FLOAT_EQ(
        harness.Interaction.Interaction().Config().AxisLength,
        2.5f);
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
