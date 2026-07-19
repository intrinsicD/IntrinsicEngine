#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    class TempObjFile final
    {
    public:
        explicit TempObjFile(const std::string_view name)
            : Path(std::filesystem::temp_directory_path() /
                   std::string{name})
        {
            std::ofstream out(
                Path, std::ios::binary | std::ios::trunc);
            out << "v 0 0 0\n"
                   "v 1 0 0\n"
                   "v 0 1 0\n"
                   "f 1 2 3\n";
        }

        ~TempObjFile()
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path;
    };

    class TempSceneFile final
    {
    public:
        TempSceneFile(
            const std::string_view name,
            const std::string_view contents)
            : Path(std::filesystem::temp_directory_path() /
                   std::string{name})
        {
            std::ofstream out(
                Path, std::ios::binary | std::ios::trunc);
            out << contents;
        }

        ~TempSceneFile()
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path;
    };

    struct NoopAssetHooks final : Core::IAssetFrameHooks
    {
        void TickAssets() override {}
    };

    class RunOnceApplication final : public Runtime::IApplication
    {
    public:
        explicit RunOnceApplication(const bool expectAssets)
            : ExpectAssets(expectAssets)
        {
        }

        void OnInitialize(Runtime::Engine& engine) override
        {
            ++InitializeCalls;
            ObserveServices(engine);
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(
            Runtime::Engine& engine,
            double,
            double) override
        {
            ++VariableTicks;
            ObserveServices(engine);
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override
        {
            ++ShutdownCalls;
        }

        void ObserveServices(Runtime::Engine& engine)
        {
            const bool exactAssetsPresent =
                engine.Services()
                    .Find<Assets::AssetService>() != nullptr &&
                engine.Services()
                    .Find<Runtime::AssetImportPipeline>() !=
                    nullptr &&
                engine.Services()
                    .Find<Graphics::GpuAssetCache>() !=
                    nullptr &&
                engine.Services()
                    .Find<Core::IAssetFrameHooks>() !=
                    nullptr;
            if (exactAssetsPresent == ExpectAssets)
                ++MatchingServiceObservations;
        }

        bool ExpectAssets{false};
        std::uint32_t InitializeCalls{0u};
        std::uint32_t VariableTicks{0u};
        std::uint32_t ShutdownCalls{0u};
        std::uint32_t MatchingServiceObservations{0u};
    };

    [[nodiscard]] Core::Config::EngineConfig
    HeadlessConfig()
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
            : Renderer(Graphics::CreateRenderer())
        {
            InitialWorld =
                Worlds.CreateWorld("AssetWorkflow");
            Renderer->Initialize(Device);
        }

        ~DirectHarness()
        {
            Stop();
            Services.Reset();
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        [[nodiscard]] Runtime::EngineSetup MakeSetup()
        {
            return Runtime::EngineSetup{
                Commands,
                Events,
                Jobs,
                Worlds,
                Services,
                [](Runtime::SimSystemDesc) {},
                [](Runtime::FramePhase,
                   Runtime::RuntimeFrameHook) {},
                Runtime::RuntimeRenderRecipeActivationKernel{
                    .ActiveConfig = &Config,
                },
                {},
                &Initialized,
            };
        }

        [[nodiscard]] Core::Result ProvideBuiltins(
            const bool provideDevice = true,
            const bool provideRenderer = true,
            const bool provideExtraction = true)
        {
            if (provideDevice)
            {
                if (Core::Result provided =
                        Services.Provide<RHI::IDevice>(
                            Device, "Test.Device");
                    !provided.has_value())
                {
                    return provided;
                }
            }
            if (provideRenderer)
            {
                if (Core::Result provided =
                        Services.Provide<Graphics::IRenderer>(
                            *Renderer, "Test.Renderer");
                    !provided.has_value())
                {
                    return provided;
                }
            }
            if (provideExtraction)
            {
                if (Core::Result provided =
                        Services.Provide<
                            Runtime::RenderExtractionCache>(
                                Extraction,
                                "Test.Extraction");
                    !provided.has_value())
                {
                    return provided;
                }
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Result RegisterModules(
            const bool assetFirst = true,
            const bool provideStreaming = false,
            const bool provideSelection = false)
        {
            Services.BeginRegistration();
            if (Core::Result builtins = ProvideBuiltins();
                !builtins.has_value())
            {
                return builtins;
            }
            if (provideStreaming)
            {
                Streaming =
                    std::make_unique<Runtime::StreamingExecutor>();
                if (Core::Result provided =
                        Services.Provide<
                            Runtime::StreamingExecutor>(
                                *Streaming, "Test.Async");
                    !provided.has_value())
                {
                    return provided;
                }
            }
            if (provideSelection)
            {
                Selection =
                    std::make_unique<
                        Runtime::SelectionController>();
                if (Core::Result provided =
                        Services.Provide<
                            Runtime::SelectionController>(
                                *Selection,
                                "Test.Interaction");
                    !provided.has_value())
                {
                    return provided;
                }
            }

            Runtime::EngineSetup setup = MakeSetup();
            const auto registerAsset =
                [this, &setup]() -> Core::Result
                {
                    const Core::Result result =
                        Asset.OnRegister(setup);
                    AssetRegistered = result.has_value();
                    return result;
                };
            const auto registerDocument =
                [this, &setup]() -> Core::Result
                {
                    const Core::Result result =
                        Document.OnRegister(setup);
                    DocumentRegistered =
                        result.has_value();
                    return result;
                };

            if (assetFirst)
            {
                if (Core::Result result = registerAsset();
                    !result.has_value())
                {
                    return result;
                }
                return registerDocument();
            }
            if (Core::Result result = registerDocument();
                !result.has_value())
            {
                return result;
            }
            return registerAsset();
        }

        [[nodiscard]] Core::Result ResolveModules()
        {
            Services.BeginResolution();
            Runtime::EngineSetup setup = MakeSetup();
            if (Core::Result result =
                    Document.OnResolve(setup);
                !result.has_value())
            {
                return result;
            }
            const Core::Result result =
                Asset.OnResolve(setup);
            AssetResolved = result.has_value();
            if (result.has_value())
                Services.Lock();
            return result;
        }

        [[nodiscard]] Core::Result Start(
            const bool assetFirst = true,
            const bool provideStreaming = false,
            const bool provideSelection = false)
        {
            if (Core::Result registered =
                    RegisterModules(
                        assetFirst,
                        provideStreaming,
                        provideSelection);
                !registered.has_value())
            {
                return registered;
            }
            return ResolveModules();
        }

        void Announce()
        {
            if (Announced || !AssetRegistered)
                return;
            Events.Publish(
                Runtime::RuntimeShutdownAnnounced{});
            (void)Events.Pump();
            Announced = true;
        }

        [[nodiscard]] std::uint64_t QuiesceGpuParticipants()
        {
            return Jobs.ShutdownGpuQueueParticipants(
                [this]
                {
                    Device.WaitIdle();
                });
        }

        void Stop()
        {
            if (!AssetRegistered &&
                !DocumentRegistered)
            {
                return;
            }

            Announce();
            if (Streaming)
                Streaming->ShutdownAndDrain();
            (void)QuiesceGpuParticipants();
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            if (AssetRegistered)
                Asset.OnShutdown(context);
            if (DocumentRegistered)
                Document.OnShutdown(context);
            Services.Reset();
            Streaming.reset();
            Selection.reset();
            AssetRegistered = false;
            AssetResolved = false;
            DocumentRegistered = false;
            Announced = false;
            Initialized = false;
        }

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
        Runtime::AssetWorkflowModule Asset{};
        Runtime::SceneDocumentModule Document{};
        std::unique_ptr<Runtime::StreamingExecutor> Streaming{};
        std::unique_ptr<Runtime::SelectionController> Selection{};
        Runtime::WorldHandle InitialWorld{};
        bool AssetRegistered{false};
        bool AssetResolved{false};
        bool DocumentRegistered{false};
        bool Announced{false};
    };

    [[nodiscard]] std::size_t EntityCount(
        const Extrinsic::ECS::Scene::Registry& scene)
    {
        return scene.Raw().storage<entt::entity>()->size();
    }
}

TEST(AssetWorkflowModule,
     PublishesExactServicesGatesInitializationAndReinitializes)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(
        /*assetFirst=*/true,
        /*provideStreaming=*/false,
        /*provideSelection=*/false).has_value());

    Assets::AssetService* const firstAssets =
        harness.Services.Find<Assets::AssetService>();
    Runtime::AssetImportPipeline* const firstPipeline =
        harness.Services.Find<Runtime::AssetImportPipeline>();
    Graphics::GpuAssetCache* const firstCache =
        harness.Services.Find<Graphics::GpuAssetCache>();
    Core::IAssetFrameHooks* const firstHooks =
        harness.Services.Find<Core::IAssetFrameHooks>();
    ASSERT_NE(firstAssets, nullptr);
    ASSERT_NE(firstPipeline, nullptr);
    ASSERT_NE(firstCache, nullptr);
    ASSERT_NE(firstHooks, nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::StreamingExecutor>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::SelectionController>(),
        nullptr);

    TempObjFile mesh{"runtime183-direct-gate.obj"};
    auto beforeInitialized =
        firstPipeline->ImportAssetFromPath(
            Runtime::RuntimeAssetImportRequest{
                .Path = mesh.Path.string(),
                .PayloadKind =
                    Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_FALSE(beforeInitialized.has_value());
    EXPECT_EQ(
        beforeInitialized.error(),
        Core::ErrorCode::InvalidState);

    harness.Initialized = true;
    auto imported =
        firstPipeline->ImportAssetFromPath(
            Runtime::RuntimeAssetImportRequest{
                .Path = mesh.Path.string(),
                .PayloadKind =
                    Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    const auto queued =
        firstPipeline->QueueGeometryImport(
            Runtime::RuntimeAssetImportRequest{
                .Path = mesh.Path.string(),
                .PayloadKind =
                    Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_FALSE(queued.has_value());
    EXPECT_EQ(
        queued.error(), Core::ErrorCode::InvalidState);

    harness.Stop();
    EXPECT_EQ(
        harness.Services.Find<Assets::AssetService>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::AssetImportPipeline>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Graphics::GpuAssetCache>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Core::IAssetFrameHooks>(),
        nullptr);

    ASSERT_TRUE(harness.Start(
        /*assetFirst=*/false).has_value());
    EXPECT_EQ(
        harness.Services.Find<Runtime::AssetImportPipeline>(),
        firstPipeline);
    EXPECT_NE(
        harness.Services.Find<Assets::AssetService>(),
        nullptr);
    EXPECT_NE(
        harness.Services.Find<Graphics::GpuAssetCache>(),
        nullptr);
}

TEST(AssetWorkflowModule,
     RegistrationConflictsLeaveOnlyTheExactExistingProvider)
{
    enum class Conflict
    {
        AssetService,
        Pipeline,
        Cache,
        Hooks,
    };

    for (const Conflict conflict :
         {Conflict::AssetService,
          Conflict::Pipeline,
          Conflict::Cache,
          Conflict::Hooks})
    {
        DirectHarness harness;
        harness.Services.BeginRegistration();
        ASSERT_TRUE(harness.ProvideBuiltins().has_value());

        Assets::AssetService existingAssets{};
        Runtime::AssetImportPipeline existingPipeline{};
        Graphics::GpuAssetCache existingCache{
            harness.Renderer->GetBufferManager(),
            harness.Renderer->GetTextureManager(),
            harness.Renderer->GetSamplerManager(),
            harness.Device.GetTransferQueue()};
        NoopAssetHooks existingHooks{};

        switch (conflict)
        {
        case Conflict::AssetService:
            ASSERT_TRUE(harness.Services
                .Provide<Assets::AssetService>(
                    existingAssets, "Conflict")
                .has_value());
            break;
        case Conflict::Pipeline:
            ASSERT_TRUE(harness.Services
                .Provide<Runtime::AssetImportPipeline>(
                    existingPipeline, "Conflict")
                .has_value());
            break;
        case Conflict::Cache:
            ASSERT_TRUE(harness.Services
                .Provide<Graphics::GpuAssetCache>(
                    existingCache, "Conflict")
                .has_value());
            break;
        case Conflict::Hooks:
            ASSERT_TRUE(harness.Services
                .Provide<Core::IAssetFrameHooks>(
                    existingHooks, "Conflict")
                .has_value());
            break;
        }

        Runtime::EngineSetup setup = harness.MakeSetup();
        const Core::Result result =
            harness.Asset.OnRegister(setup);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error(), Core::ErrorCode::InvalidState);
        EXPECT_EQ(
            harness.Services.Find<Assets::AssetService>(),
            conflict == Conflict::AssetService
                ? &existingAssets
                : nullptr);
        EXPECT_EQ(
            harness.Services.Find<Runtime::AssetImportPipeline>(),
            conflict == Conflict::Pipeline
                ? &existingPipeline
                : nullptr);
        EXPECT_EQ(
            harness.Services.Find<Graphics::GpuAssetCache>(),
            conflict == Conflict::Cache
                ? &existingCache
                : nullptr);
        EXPECT_EQ(
            harness.Services.Find<Core::IAssetFrameHooks>(),
            conflict == Conflict::Hooks
                ? &existingHooks
                : nullptr);
    }
}

TEST(AssetWorkflowModule,
     MissingDeviceFailsRegistrationWithoutPartialPublication)
{
    DirectHarness harness;
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.ProvideBuiltins(
        /*provideDevice=*/false).has_value());
    Runtime::EngineSetup setup = harness.MakeSetup();

    const Core::Result result =
        harness.Asset.OnRegister(setup);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(
        result.error(),
        Core::ErrorCode::ResourceNotFound);
    EXPECT_TRUE(harness.Services.HasBootErrors());
    EXPECT_EQ(
        harness.Services.Find<Assets::AssetService>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::AssetImportPipeline>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Graphics::GpuAssetCache>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Core::IAssetFrameHooks>(),
        nullptr);
}

TEST(AssetWorkflowModule,
     MissingDocumentOrHistoryRollsBackEveryPublication)
{
    {
        DirectHarness harness;
        harness.Services.BeginRegistration();
        ASSERT_TRUE(harness.ProvideBuiltins().has_value());
        Runtime::EngineSetup setup = harness.MakeSetup();
        ASSERT_TRUE(
            harness.Asset.OnRegister(setup).has_value());
        harness.AssetRegistered = true;
        harness.Services.BeginResolution();

        const Core::Result result =
            harness.Asset.OnResolve(setup);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error(),
            Core::ErrorCode::ResourceNotFound);
        EXPECT_TRUE(harness.Services.HasBootErrors());
        EXPECT_EQ(
            harness.Services.Find<Assets::AssetService>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Runtime::AssetImportPipeline>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Graphics::GpuAssetCache>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Core::IAssetFrameHooks>(),
            nullptr);
        EXPECT_EQ(
            harness.Jobs.ShutdownGpuQueueParticipants(),
            0u);
    }

    {
        DirectHarness harness;
        harness.Services.BeginRegistration();
        ASSERT_TRUE(harness.ProvideBuiltins().has_value());
        Runtime::EngineSetup setup = harness.MakeSetup();
        ASSERT_TRUE(
            harness.Document.OnRegister(setup).has_value());
        harness.DocumentRegistered = true;
        Runtime::EditorCommandHistory* const history =
            harness.Services.Find<
                Runtime::EditorCommandHistory>();
        ASSERT_NE(history, nullptr);
        ASSERT_TRUE(harness.Services
            .Withdraw<Runtime::EditorCommandHistory>(
                *history)
            .has_value());
        ASSERT_TRUE(
            harness.Asset.OnRegister(setup).has_value());
        harness.AssetRegistered = true;
        harness.Services.BeginResolution();
        ASSERT_TRUE(
            harness.Document.OnResolve(setup).has_value());

        const Core::Result result =
            harness.Asset.OnResolve(setup);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error(),
            Core::ErrorCode::ResourceNotFound);
        EXPECT_TRUE(harness.Services.HasBootErrors());
        EXPECT_EQ(
            harness.Services.Find<Assets::AssetService>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Runtime::AssetImportPipeline>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Graphics::GpuAssetCache>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<Core::IAssetFrameHooks>(),
            nullptr);
        EXPECT_EQ(
            harness.Jobs.ShutdownGpuQueueParticipants(),
            0u);
    }
}

TEST(AssetWorkflowModule,
     DocumentAndWorldReplacementRebindBeforeSceneUse)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    harness.Initialized = true;
    Runtime::AssetImportPipeline* const pipeline =
        harness.Services.Find<Runtime::AssetImportPipeline>();
    Core::IAssetFrameHooks* const hooks =
        harness.Services.Find<Core::IAssetFrameHooks>();
    ASSERT_NE(pipeline, nullptr);
    ASSERT_NE(hooks, nullptr);
    TempObjFile initialMesh{
        "runtime183-binding-initial.obj"};
    TempObjFile documentMesh{
        "runtime183-binding-document.obj"};
    TempObjFile beforeLoadMesh{
        "runtime183-binding-before-load.obj"};
    TempObjFile afterLoadMesh{
        "runtime183-binding-after-load.obj"};
    TempObjFile afterInvalidLoadMesh{
        "runtime183-binding-after-invalid-load.obj"};
    TempObjFile closeMesh{
        "runtime183-binding-close.obj"};
    TempObjFile awayMesh{
        "runtime183-binding-away.obj"};
    TempSceneFile savedScene{
        "runtime183-binding-load.scene.json", ""};
    TempSceneFile invalidScene{
        "runtime183-binding-invalid.scene.json", "not json"};

    auto first = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = initialMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(first.has_value())
        << static_cast<int>(first.error());
    auto* initialScene =
        harness.Worlds.Get(harness.InitialWorld);
    ASSERT_NE(initialScene, nullptr);
    ASSERT_GT(EntityCount(*initialScene), 0u);

    ASSERT_TRUE(
        harness.Document.NewSceneDocument().has_value());
    EXPECT_EQ(EntityCount(*initialScene), 0u);
    auto afterDocument = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = documentMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(afterDocument.has_value())
        << static_cast<int>(afterDocument.error());
    const std::size_t savedEntityCount =
        EntityCount(*initialScene);
    ASSERT_GT(savedEntityCount, 0u);
    ASSERT_TRUE(
        harness.Document
            .SaveSceneToPath(savedScene.Path.string())
            .has_value());

    auto beforeLoad = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = beforeLoadMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(beforeLoad.has_value())
        << static_cast<int>(beforeLoad.error());
    ASSERT_GT(EntityCount(*initialScene), savedEntityCount);
    ASSERT_TRUE(
        harness.Document
            .LoadSceneFromPath(savedScene.Path.string())
            .has_value());
    EXPECT_EQ(EntityCount(*initialScene), savedEntityCount);

    auto afterLoad = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = afterLoadMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(afterLoad.has_value())
        << static_cast<int>(afterLoad.error());
    const std::size_t beforeInvalidLoad =
        EntityCount(*initialScene);
    const auto invalidLoad =
        harness.Document.LoadSceneFromPath(
            invalidScene.Path.string());
    ASSERT_FALSE(invalidLoad.has_value());
    EXPECT_EQ(
        invalidLoad.error(), Core::ErrorCode::InvalidFormat);
    EXPECT_EQ(EntityCount(*initialScene), beforeInvalidLoad);

    auto afterInvalidLoad = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = afterInvalidLoadMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(afterInvalidLoad.has_value())
        << static_cast<int>(afterInvalidLoad.error());

    ASSERT_TRUE(
        harness.Document.CloseSceneDocument().has_value());
    EXPECT_EQ(EntityCount(*initialScene), 0u);
    auto afterClose = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = closeMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(afterClose.has_value())
        << static_cast<int>(afterClose.error());

    const Runtime::WorldHandle away =
        harness.Worlds.CreateWorld("Away");
    ASSERT_TRUE(
        harness.Worlds.RequestSetActiveWorld(away)
            .has_value());
    EXPECT_EQ(
        harness.Worlds.ApplyMaintenance(
            harness.Events, harness.Jobs)
            .AppliedActiveWorldChanges,
        1u);

    // Do not pump the delayed ActiveWorldChanged event. Direct import must
    // reject the stale target, while the asset hook directly validates and
    // rebinds before its next maintenance tick.
    auto stale = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = awayMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error(), Core::ErrorCode::InvalidState);

    hooks->TickAssets();
    auto rebound = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = awayMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(rebound.has_value())
        << static_cast<int>(rebound.error());
    auto* awayScene = harness.Worlds.Get(away);
    ASSERT_NE(awayScene, nullptr);
    EXPECT_GT(EntityCount(*awayScene), 0u);

    ASSERT_TRUE(
        harness.Worlds.RequestDestroyWorld(
            harness.InitialWorld)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Events.Pump();
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    EXPECT_FALSE(
        harness.Worlds.Contains(harness.InitialWorld));
}

TEST(AssetWorkflowModule,
     AnnouncementDetachesImportsAndParticipantBeforeGpuBoundary)
{
    DirectHarness harness;
    ASSERT_TRUE(harness.Start(
        /*assetFirst=*/false,
        /*provideStreaming=*/true,
        /*provideSelection=*/true).has_value());
    harness.Initialized = true;

    Runtime::AssetImportPipeline* const pipeline =
        harness.Services.Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::StreamingExecutor>(),
        harness.Streaming.get());
    EXPECT_EQ(
        harness.Services.Find<Runtime::SelectionController>(),
        harness.Selection.get());
    harness.Announce();

    TempObjFile mesh{"runtime183-announced.obj"};
    auto afterAnnouncement =
        pipeline->ImportAssetFromPath(
            Runtime::RuntimeAssetImportRequest{
                .Path = mesh.Path.string(),
                .PayloadKind =
                    Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_FALSE(afterAnnouncement.has_value());
    EXPECT_EQ(
        afterAnnouncement.error(),
        Core::ErrorCode::InvalidState);

    auto recycled =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowModule",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    ASSERT_TRUE(recycled.has_value());
    EXPECT_TRUE(harness.Document
        .UnregisterReplacementParticipant(*recycled)
        .has_value());

    EXPECT_EQ(harness.QuiesceGpuParticipants(), 1u);
    EXPECT_EQ(harness.QuiesceGpuParticipants(), 0u);

    Runtime::RuntimeModuleShutdownContext context{
        .Commands = harness.Commands,
        .Events = harness.Events,
        .Jobs = harness.Jobs,
        .Worlds = harness.Worlds,
        .Services = harness.Services,
    };
    harness.Asset.OnShutdown(context);
    harness.AssetRegistered = false;
    EXPECT_EQ(
        harness.Services.Find<Assets::AssetService>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Runtime::AssetImportPipeline>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Graphics::GpuAssetCache>(),
        nullptr);
    EXPECT_EQ(
        harness.Services.Find<Core::IAssetFrameHooks>(),
        nullptr);
}

TEST(AssetWorkflowModule,
     RealEngineRunSupportsCompositionOmissionAndReinitialize)
{
    {
        auto application =
            std::make_unique<RunOnceApplication>(
                /*expectAssets=*/false);
        RunOnceApplication* const observed =
            application.get();
        Runtime::Engine engine(
            HeadlessConfig(), std::move(application));

        engine.Initialize();
        engine.Run();
        engine.Shutdown();

        EXPECT_EQ(observed->InitializeCalls, 1u);
        EXPECT_EQ(observed->VariableTicks, 1u);
        EXPECT_EQ(observed->ShutdownCalls, 1u);
        EXPECT_EQ(
            observed->MatchingServiceObservations,
            2u);
    }

    auto application =
        std::make_unique<RunOnceApplication>(
            /*expectAssets=*/true);
    RunOnceApplication* const observed =
        application.get();
    Runtime::Engine engine(
        HeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();

    engine.Initialize();
    Runtime::AssetImportPipeline* const firstPipeline =
        engine.Services()
            .Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(firstPipeline, nullptr);
    engine.Run();
    engine.Shutdown();

    engine.Initialize();
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::AssetImportPipeline>(),
        firstPipeline);
    engine.Run();
    engine.Shutdown();

    EXPECT_EQ(observed->InitializeCalls, 2u);
    EXPECT_EQ(observed->VariableTicks, 2u);
    EXPECT_EQ(observed->ShutdownCalls, 2u);
    EXPECT_EQ(
        observed->MatchingServiceObservations,
        4u);
}
