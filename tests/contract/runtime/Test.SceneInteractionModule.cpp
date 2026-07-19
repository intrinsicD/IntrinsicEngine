#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectionReadback;
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

    class ExitAfterOneFrameApplication final
        : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override
        {
            ++InitializeCalls;
        }
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(
            Runtime::Engine& engine, double, double) override
        {
            ++VariableTicks;
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override
        {
            ++ShutdownCalls;
        }

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
                [](Runtime::SimSystemDesc) {},
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
                Runtime::FramePhase::AfterCommandDrain};
            Runtime::RuntimeFrameHook Hook{};
        };

        void InitializeRendererForHooks()
        {
            if (RendererInitialized)
                return;
            Renderer->Initialize(Device);
            RendererInitialized = true;
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
                .Phase = FrameHooks.at(index).Phase,
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
        harness.Services
            .Find<Runtime::SelectionReadbackState>(),
        nullptr);
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
                .Phase = record.Phase,
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
        Runtime::Engine engine(
            HeadlessConfig(), std::move(application));
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
        Runtime::Engine engine(
            HeadlessConfig(), std::move(application));
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
    Runtime::Engine engine(
        HeadlessConfig(), std::move(application));
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
