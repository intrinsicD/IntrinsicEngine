#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    using namespace std::chrono_literals;

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 1u)
        {
            if (Core::Tasks::Scheduler::IsInitialized())
                Core::Tasks::Scheduler::Shutdown();
            Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Core::Tasks::Scheduler::WaitForAll();
            Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    class WorkerRelease final
    {
    public:
        explicit WorkerRelease(std::atomic_bool& release)
            : Release(release)
        {
        }

        ~WorkerRelease()
        {
            Release.store(true, std::memory_order_release);
        }

        std::atomic_bool& Release;
    };

    [[nodiscard]] bool WaitUntil(
        const std::atomic_bool& flag,
        const std::chrono::milliseconds timeout = 2s)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + timeout;
        while (!flag.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(1ms);
        }
        return flag.load(std::memory_order_acquire);
    }

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

    class RunOnceApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit RunOnceApplication(
            const bool expectAssets,
            std::string importPath = {})
            : ExpectAssets(expectAssets),
              ImportPath(std::move(importPath))
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            ++InitializeCalls;
            ObserveServices(engine);
            if (ExpectAssets && !ImportPath.empty())
            {
                Runtime::AssetImportPipeline* const pipeline =
                    engine.Services()
                        .Find<Runtime::AssetImportPipeline>();
                ASSERT_NE(pipeline, nullptr);
                auto imported =
                    pipeline->ImportAssetFromPath(
                        Runtime::RuntimeAssetImportRequest{
                            .Path = ImportPath,
                            .PayloadKind =
                                Assets::AssetPayloadKind::Mesh,
                        });
                InitializeImportErrors.push_back(
                    imported.has_value()
                        ? Core::ErrorCode::Success
                        : imported.error());
            }
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++VariableTicks;
            ObserveServices(engine);
            if (ExpectAssets && !ImportPath.empty())
            {
                Runtime::AssetImportPipeline* const pipeline =
                    engine.Services()
                        .Find<Runtime::AssetImportPipeline>();
                ASSERT_NE(pipeline, nullptr);
                auto imported =
                    pipeline->ImportAssetFromPath(
                        Runtime::RuntimeAssetImportRequest{
                            .Path = ImportPath,
                            .PayloadKind =
                                Assets::AssetPayloadKind::Mesh,
                        });
                RuntimeImportSucceeded.push_back(
                    imported.has_value());
            }
            engine.RequestExit();
        }

        void Shutdown() override { ++ShutdownCalls; }

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
        std::string ImportPath{};
        std::uint32_t InitializeCalls{0u};
        std::uint32_t VariableTicks{0u};
        std::uint32_t ShutdownCalls{0u};
        std::uint32_t MatchingServiceObservations{0u};
        std::vector<Core::ErrorCode> InitializeImportErrors{};
        std::vector<bool> RuntimeImportSucceeded{};
    };

    class FixedFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit FixedFrameApplication(
            const std::uint32_t frameCount)
            : FrameCount(frameCount)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++ObservedFrames;
            if (ObservedFrames >= FrameCount)
                engine.RequestExit();
        }
        void Shutdown() override {}

        std::uint32_t FrameCount{1u};
        std::uint32_t ObservedFrames{0u};
    };

    struct ShutdownOrderObservation
    {
        bool Seen{false};
        bool StreamingWithdrawn{false};
        bool AssetServicesStillPublished{false};
    };

    class ShutdownOrderProbeModule final :
        public Runtime::IRuntimeModule
    {
    public:
        explicit ShutdownOrderProbeModule(
            ShutdownOrderObservation& observation)
            : Observation(observation)
        {
        }

        [[nodiscard]] std::string_view Name()
            const noexcept override
        {
            // AssetWorkflow < AsuShutdownOrderProbe < AsyncWork.
            return "Runtime.AsuShutdownOrderProbe";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext& context) override
        {
            Observation.Seen = true;
            Observation.StreamingWithdrawn =
                context.Services
                    .Find<Runtime::StreamingExecutor>() ==
                nullptr;
            Observation.AssetServicesStillPublished =
                context.Services.Find<Assets::AssetService>() !=
                    nullptr &&
                context.Services
                        .Find<Runtime::AssetImportPipeline>() !=
                    nullptr &&
                context.Services
                        .Find<Graphics::GpuAssetCache>() !=
                    nullptr &&
                context.Services
                        .Find<Core::IAssetFrameHooks>() !=
                    nullptr;
        }

    private:
        ShutdownOrderObservation& Observation;
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
            const auto registerTextureBake =
                [this, &setup]() -> Core::Result
                {
                    const Core::Result result =
                        TextureBake.OnRegister(setup);
                    TextureBakeRegistered = result.has_value();
                    return result;
                };

            if (assetFirst)
            {
                if (Core::Result result = registerAsset();
                    !result.has_value())
                {
                    return result;
                }
                if (Core::Result result = registerDocument();
                    !result.has_value())
                {
                    return result;
                }
                return registerTextureBake();
            }
            if (Core::Result result = registerDocument();
                !result.has_value())
            {
                return result;
            }
            if (Core::Result result = registerAsset();
                !result.has_value())
            {
                return result;
            }
            return registerTextureBake();
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
            if (!result.has_value())
                return result;
            const Core::Result textureResult =
                TextureBake.OnResolve(setup);
            TextureBakeResolved = textureResult.has_value();
            if (!textureResult.has_value())
                return textureResult;
            Services.Lock();
            return Core::Ok();
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
            if (Announced ||
                (!AssetRegistered && !TextureBakeRegistered))
                return;
            Initialized = false;
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
                    ++GpuIdleWaits;
                    Device.WaitIdle();
                });
        }

        void Stop()
        {
            if (!AssetRegistered &&
                !TextureBakeRegistered &&
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
            if (TextureBakeRegistered)
                TextureBake.OnShutdown(context);
            if (AssetRegistered)
                Asset.OnShutdown(context);
            if (DocumentRegistered)
                Document.OnShutdown(context);
            Services.Reset();
            Streaming.reset();
            Selection.reset();
            AssetRegistered = false;
            AssetResolved = false;
            TextureBakeRegistered = false;
            TextureBakeResolved = false;
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
        Runtime::TextureBakeModule TextureBake{};
        Runtime::SceneDocumentModule Document{};
        std::unique_ptr<Runtime::StreamingExecutor> Streaming{};
        std::unique_ptr<Runtime::SelectionController> Selection{};
        Runtime::WorldHandle InitialWorld{};
        std::uint64_t GpuIdleWaits{0u};
        bool AssetRegistered{false};
        bool AssetResolved{false};
        bool TextureBakeRegistered{false};
        bool TextureBakeResolved{false};
        bool DocumentRegistered{false};
        bool Announced{false};
    };

    [[nodiscard]] std::size_t EntityCount(
        const Extrinsic::ECS::Scene::Registry& scene)
    {
        std::size_t count = 0u;
        for ([[maybe_unused]] const entt::entity entity :
             scene.Raw().view<entt::entity>())
        {
            ++count;
        }
        return count;
    }

    [[nodiscard]] ECS::EntityHandle MakeProceduralRenderable(
        ECS::Scene::Registry& scene)
    {
        namespace Components = ECS::Components;
        namespace Render = Graphics::Components;

        const ECS::EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<Components::Transform::WorldMatrix>(entity)
            .Matrix = glm::mat4{1.0f};
        raw.emplace<Render::RenderSurface>(entity);
        raw.emplace<Components::ProceduralGeometryRef>(entity);
        return entity;
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest
    MakeBakeRequest(
        const std::uint64_t key,
        const Runtime::WorldHandle world,
        const std::uint64_t bindingEpoch,
        const ECS::EntityHandle entity)
    {
        const std::array<float, 9u> positions{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, static_cast<float>(key), 0.0f,
        };
        const std::array<std::uint32_t, 3u> indices{0u, 1u, 2u};
        const std::array<float, 6u> texcoords{
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
        };
        const std::array<float, 9u> normals{
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
        };
        const auto identity =
            Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(
                Runtime::RuntimeObjectSpaceNormalBakeIdentityInput{
                    .PackedPositionBytes =
                        std::as_bytes(std::span{positions}),
                    .SurfaceIndexBytes =
                        std::as_bytes(std::span{indices}),
                    .ResolvedTexcoordBytes =
                        std::as_bytes(std::span{texcoords}),
                    .ResolvedNormalBytes =
                        std::as_bytes(std::span{normals}),
                    .VertexCount = 3u,
                    .SurfaceIndexCount = 3u,
                    .Options =
                        Graphics::ObjectSpaceNormalTextureBakeOptions{
                            .Width = 64u,
                            .Height = 64u,
                            .PaddingTexels = 2u,
                        },
                });
        EXPECT_TRUE(identity.Succeeded());
        return Runtime::RuntimeObjectSpaceNormalBakeRequest{
            .Identity = identity.Identity,
            .Target =
                Runtime::RuntimeObjectSpaceNormalBakeTarget{
                    .World = world,
                    .BindingEpoch = bindingEpoch,
                    .Entity = entity,
                    .StableEntityId =
                        Runtime::StableEntityLookup::ToRenderId(entity),
                    .Semantic =
                        Runtime::ProgressiveSlotSemantic::Normal,
                },
        };
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
        ASSERT_TRUE(
            harness.Asset.OnRegister(setup).has_value());
        harness.AssetRegistered = true;
        harness.Services.BeginResolution();
        ASSERT_TRUE(
            harness.Document.OnResolve(setup).has_value());
        ASSERT_TRUE(harness.Services
            .Withdraw<Runtime::EditorCommandHistory>(
                *history)
            .has_value());

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
     DocumentParticipantConflictRollsBackEveryPublication)
{
    DirectHarness harness;
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.ProvideBuiltins().has_value());
    Runtime::EngineSetup setup = harness.MakeSetup();
    ASSERT_TRUE(
        harness.Document.OnRegister(setup).has_value());
    harness.DocumentRegistered = true;
    ASSERT_TRUE(
        harness.Asset.OnRegister(setup).has_value());
    harness.AssetRegistered = true;

    auto conflict =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowModule",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    ASSERT_TRUE(conflict.has_value());

    harness.Services.BeginResolution();
    ASSERT_TRUE(
        harness.Document.OnResolve(setup).has_value());
    const Core::Result result =
        harness.Asset.OnResolve(setup);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(
        result.error(), Core::ErrorCode::InvalidState);
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
    EXPECT_EQ(
        harness.Jobs.ShutdownGpuQueueParticipants(),
        0u);
    EXPECT_TRUE(
        harness.Document
            .UnregisterReplacementParticipant(*conflict)
            .has_value());
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
    Graphics::GpuAssetCache* const cache =
        harness.Services.Find<Graphics::GpuAssetCache>();
    Assets::AssetService* const assets =
        harness.Services.Find<Assets::AssetService>();
    Runtime::TextureBakeService* const textureBake =
        harness.Services.Find<Runtime::TextureBakeService>();
    ASSERT_NE(pipeline, nullptr);
    ASSERT_NE(hooks, nullptr);
    ASSERT_NE(cache, nullptr);
    ASSERT_NE(assets, nullptr);
    ASSERT_NE(textureBake, nullptr);
    TempObjFile initialMesh{
        "runtime183-binding-initial.obj"};
    TempObjFile afterNewMesh{
        "runtime183-binding-after-new.obj"};
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

    Runtime::RuntimeObjectSpaceNormalBakeQueue* bakeQueue =
        nullptr;
    Runtime::WorldHandle bakeWorld{};
    std::uint64_t bakeBindingEpoch = 0u;
    const RHI::IDevice* bakeDevice = nullptr;
    const auto queueProbe =
        pipeline->RegisterPostImportProcessor(
            Runtime::RuntimePostImportProcessorDesc{
                .DebugName =
                    "RUNTIME-183 document replacement queue probe",
                .PayloadKind =
                    Assets::AssetPayloadKind::Mesh,
                .Process =
                    [&bakeQueue,
                     &bakeWorld,
                     &bakeBindingEpoch,
                     &bakeDevice](
                        const Runtime::
                            RuntimePostImportProcessorContext&,
                        Runtime::
                            RuntimePostImportProcessorServices&
                                services)
                        -> Core::Result
                    {
                        bakeQueue =
                            services.ObjectSpaceNormalBakeQueue;
                        bakeWorld = services.World;
                        bakeBindingEpoch =
                            services.ObjectSpaceNormalBakeBindingEpoch;
                        bakeDevice =
                            services.ObjectSpaceNormalBakeDevice;
                        return bakeQueue != nullptr
                            ? Core::Ok()
                            : Core::Err(
                                  Core::ErrorCode::
                                      InvalidState);
                    },
            });
    ASSERT_TRUE(queueProbe.IsValid());

    auto* const initialScene =
        harness.Worlds.Get(harness.InitialWorld);
    ASSERT_NE(initialScene, nullptr);
    ECS::EntityHandle expectedOutgoing =
        ECS::InvalidEntityHandle;
    std::string afterReplacementPath{};
    std::vector<Runtime::SceneReplacementKind> beforeKinds;
    std::vector<Runtime::SceneReplacementKind> afterKinds;
    std::vector<bool> beforeSawOutgoingLive;
    std::vector<bool> beforeSawExtractionCleared;
    std::vector<bool> beforeSawBakeCleared;
    std::vector<bool> afterSawOutgoingDestroyed;

    auto observer =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                // Sort after both asset and texture-bake owners so these
                // observations see their synchronous Before/After work.
                .Name = "Runtime.TextureBakeObserver",
                .BeforeReplace =
                    [&](const Runtime::
                            SceneReplacementContext& context)
                    {
                        beforeKinds.push_back(context.Kind);
                        beforeSawOutgoingLive.push_back(
                            context.Registry.IsValid(
                                expectedOutgoing));
                        beforeSawExtractionCleared.push_back(
                            harness.Extraction
                                .GetTrackedRenderableCount() ==
                            0u);
                        beforeSawBakeCleared.push_back(
                            bakeQueue != nullptr &&
                            bakeQueue->PendingCount() == 0u);
                    },
                .AfterReplace =
                    [&](const Runtime::
                            SceneReplacementContext& context)
                    {
                        afterKinds.push_back(context.Kind);
                        afterSawOutgoingDestroyed.push_back(
                            !context.Registry.IsValid(
                                expectedOutgoing));
                        auto rebound =
                            pipeline->ImportAssetFromPath(
                                Runtime::
                                    RuntimeAssetImportRequest{
                                        .Path =
                                            afterReplacementPath,
                                        .PayloadKind =
                                            Assets::
                                                AssetPayloadKind::
                                                    Mesh,
                                    });
                        EXPECT_TRUE(rebound.has_value())
                            << static_cast<int>(
                                   rebound.error());
                    },
            });
    ASSERT_TRUE(observer.has_value());

    std::uint64_t bakeKey = 100u;
    const auto prepareReplacement =
        [&]()
        {
            expectedOutgoing =
                MakeProceduralRenderable(*initialScene);
            const auto stats =
                harness.Extraction.ExtractAndSubmit(
                    *initialScene,
                    *harness.Renderer,
                    cache);
            EXPECT_GT(stats.CandidateRenderableCount, 0u);
            EXPECT_GT(
                harness.Extraction
                    .GetTrackedRenderableCount(),
                0u);
            ASSERT_NE(bakeQueue, nullptr);
            const auto scheduled =
                bakeQueue->Schedule(
                    MakeBakeRequest(
                        ++bakeKey,
                        bakeWorld,
                        bakeBindingEpoch,
                        expectedOutgoing),
                    bakeDevice != nullptr &&
                        bakeDevice->IsOperational());
            ASSERT_TRUE(scheduled.Succeeded());
            ASSERT_EQ(bakeQueue->PendingCount(), 1u);
            ASSERT_TRUE(
                initialScene->IsValid(expectedOutgoing));
        };

    auto first = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = initialMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(first.has_value())
        << static_cast<int>(first.error());
    ASSERT_GT(EntityCount(*initialScene), 0u);

    prepareReplacement();
    afterReplacementPath = afterNewMesh.Path.string();
    ASSERT_TRUE(
        harness.Document.NewSceneDocument().has_value());
    ASSERT_EQ(beforeKinds.size(), 1u);
    ASSERT_EQ(afterKinds.size(), 1u);
    EXPECT_EQ(
        beforeKinds.back(),
        Runtime::SceneReplacementKind::New);
    EXPECT_EQ(
        afterKinds.back(),
        Runtime::SceneReplacementKind::New);
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
    prepareReplacement();
    afterReplacementPath = afterLoadMesh.Path.string();
    ASSERT_TRUE(
        harness.Document
            .LoadSceneFromPath(savedScene.Path.string())
            .has_value());
    ASSERT_EQ(beforeKinds.size(), 2u);
    ASSERT_EQ(afterKinds.size(), 2u);
    EXPECT_EQ(
        beforeKinds.back(),
        Runtime::SceneReplacementKind::Load);
    EXPECT_EQ(
        afterKinds.back(),
        Runtime::SceneReplacementKind::Load);

    expectedOutgoing =
        MakeProceduralRenderable(*initialScene);
    const auto trackedBeforeInvalid =
        harness.Extraction.ExtractAndSubmit(
            *initialScene, *harness.Renderer, cache);
    ASSERT_GT(
        trackedBeforeInvalid.CandidateRenderableCount,
        0u);
    ASSERT_GT(
        harness.Extraction.GetTrackedRenderableCount(),
        0u);
    const auto pendingBeforeInvalid =
        bakeQueue->Schedule(
            MakeBakeRequest(
                ++bakeKey,
                bakeWorld,
                bakeBindingEpoch,
                expectedOutgoing),
            bakeDevice != nullptr &&
                bakeDevice->IsOperational());
    ASSERT_TRUE(pendingBeforeInvalid.Succeeded());
    ASSERT_EQ(bakeQueue->PendingCount(), 1u);
    ASSERT_TRUE(initialScene->IsValid(expectedOutgoing));
    const std::size_t callbackCountBeforeInvalid =
        beforeKinds.size();
    const auto invalidLoad =
        harness.Document.LoadSceneFromPath(
            invalidScene.Path.string());
    ASSERT_FALSE(invalidLoad.has_value());
    EXPECT_EQ(
        invalidLoad.error(), Core::ErrorCode::InvalidFormat);
    EXPECT_EQ(
        beforeKinds.size(), callbackCountBeforeInvalid);
    EXPECT_EQ(
        afterKinds.size(), callbackCountBeforeInvalid);
    EXPECT_TRUE(initialScene->IsValid(expectedOutgoing));
    EXPECT_GT(
        harness.Extraction.GetTrackedRenderableCount(),
        0u);
    EXPECT_EQ(bakeQueue->PendingCount(), 1u);

    auto afterInvalidLoad = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = afterInvalidLoadMesh.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(afterInvalidLoad.has_value())
        << static_cast<int>(afterInvalidLoad.error());

    const auto trackedBeforeClose =
        harness.Extraction.ExtractAndSubmit(
            *initialScene, *harness.Renderer, cache);
    ASSERT_GT(
        trackedBeforeClose.CandidateRenderableCount,
        0u);
    afterReplacementPath = closeMesh.Path.string();
    ASSERT_TRUE(
        harness.Document.CloseSceneDocument().has_value());
    ASSERT_EQ(beforeKinds.size(), 3u);
    ASSERT_EQ(afterKinds.size(), 3u);
    EXPECT_EQ(
        beforeKinds.back(),
        Runtime::SceneReplacementKind::Close);
    EXPECT_EQ(
        afterKinds.back(),
        Runtime::SceneReplacementKind::Close);
    EXPECT_EQ(
        beforeSawOutgoingLive,
        (std::vector<bool>{true, true, true}));
    EXPECT_EQ(
        beforeSawExtractionCleared,
        (std::vector<bool>{true, true, true}));
    EXPECT_EQ(
        beforeSawBakeCleared,
        (std::vector<bool>{true, true, true}));
    EXPECT_EQ(
        afterSawOutgoingDestroyed,
        (std::vector<bool>{true, true, true}));

    Assets::AssetTexture2DPayload retainedPayload{};
    retainedPayload.Metadata.Width = 1u;
    retainedPayload.Metadata.Height = 1u;
    retainedPayload.Metadata.Components = 1u;
    retainedPayload.Metadata.PixelFormat =
        Assets::AssetTexturePixelFormat::R8Unorm;
    retainedPayload.Metadata.ColorSpace =
        Assets::AssetTextureColorSpace::Linear;
    retainedPayload.Metadata.SourceKind =
        Assets::AssetTextureSourceKind::Generated;
    retainedPayload.PixelBytes = {std::byte{0x7f}};
    auto retainedTexture = assets->Load<Assets::AssetTexture2DPayload>(
        "generated://runtime190/inactive-world-retained",
        [retainedPayload](
            std::string_view,
            Assets::AssetId) -> Core::Expected<Assets::AssetTexture2DPayload>
        {
            return retainedPayload;
        });
    ASSERT_TRUE(retainedTexture.has_value());
    const ECS::EntityHandle retainedEntity = initialScene->Create();
    initialScene->Raw().emplace<Runtime::BakedPropertyTextures>(
        retainedEntity,
        Runtime::BakedPropertyTextures{
            .Records = {
                Runtime::BakedPropertyTextureRecord{
                    .OutputName = "inactive-world-retained",
                    .Texture = *retainedTexture,
                    .State = Runtime::BakedPropertyTextureState::Ready,
                    .Diagnostic = "ready",
                },
            },
        });
    const Runtime::TextureBakeProducerContext initialProducer =
        textureBake->ProducerContext();
    ASSERT_EQ(initialProducer.World, harness.InitialWorld);
    ASSERT_FALSE(initialProducer.Lifetime.expired());

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

    (void)harness.Events.Pump();
    const Runtime::TextureBakeProducerContext awayProducer =
        textureBake->ProducerContext();
    EXPECT_EQ(awayProducer.World, away);
    EXPECT_NE(awayProducer.BindingEpoch, initialProducer.BindingEpoch);
    EXPECT_TRUE(initialProducer.Lifetime.expired());
    EXPECT_FALSE(awayProducer.Lifetime.expired());
    EXPECT_TRUE(assets->IsAlive(*retainedTexture));

    ASSERT_TRUE(
        harness.Worlds.RequestSetActiveWorld(harness.InitialWorld).has_value());
    EXPECT_EQ(
        harness.Worlds.ApplyMaintenance(
            harness.Events, harness.Jobs)
            .AppliedActiveWorldChanges,
        1u);
    (void)harness.Events.Pump();
    const Runtime::TextureBakeProducerContext restoredProducer =
        textureBake->ProducerContext();
    EXPECT_EQ(restoredProducer.World, harness.InitialWorld);
    EXPECT_NE(restoredProducer.BindingEpoch, awayProducer.BindingEpoch);
    EXPECT_TRUE(awayProducer.Lifetime.expired());
    EXPECT_FALSE(restoredProducer.Lifetime.expired());
    const Runtime::TextureBakeSnapshot restored = textureBake->Snapshot(
        Runtime::StableEntityLookup::ToRenderId(retainedEntity));
    ASSERT_EQ(restored.Textures.size(), 1u);
    EXPECT_EQ(
        restored.Textures.front().State,
        Runtime::BakedPropertyTextureState::Ready);
    EXPECT_TRUE(assets->IsAlive(*retainedTexture));

    ASSERT_TRUE(harness.Worlds.RequestSetActiveWorld(away).has_value());
    EXPECT_EQ(
        harness.Worlds.ApplyMaintenance(
            harness.Events, harness.Jobs)
            .AppliedActiveWorldChanges,
        1u);
    (void)harness.Events.Pump();

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
    EXPECT_FALSE(assets->IsAlive(*retainedTexture));

    pipeline->UnregisterPostImportProcessor(queueProbe);
    EXPECT_TRUE(
        harness.Document
            .UnregisterReplacementParticipant(*observer)
            .has_value());
}

TEST(AssetWorkflowModule,
     AnnouncementDetachesImportsAndParticipantBeforeGpuBoundary)
{
    DirectHarness harness;
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.ProvideBuiltins().has_value());
    harness.Streaming =
        std::make_unique<Runtime::StreamingExecutor>();
    harness.Selection =
        std::make_unique<Runtime::SelectionController>();
    ASSERT_TRUE(
        harness.Services
            .Provide<Runtime::StreamingExecutor>(
                *harness.Streaming, "Test.Async")
            .has_value());
    ASSERT_TRUE(
        harness.Services
            .Provide<Runtime::SelectionController>(
                *harness.Selection, "Test.Interaction")
            .has_value());

    Runtime::EngineSetup setup = harness.MakeSetup();
    ASSERT_TRUE(harness.Asset.OnRegister(setup).has_value());
    harness.AssetRegistered = true;
    ASSERT_TRUE(
        harness.TextureBake.OnRegister(setup).has_value());
    harness.TextureBakeRegistered = true;

    bool releaseProbeCalled = false;
    bool releasedNameWasReusable = false;
    bool recycledHandleReleased = false;
    Runtime::SceneReplacementParticipantHandle recycledHandle{};
    const auto releaseProbe =
        harness.Events.Subscribe<
            Runtime::RuntimeShutdownAnnounced>(
            [&](const Runtime::RuntimeShutdownAnnounced&)
            {
                releaseProbeCalled = true;
                auto recycled =
                    harness.Document
                        .RegisterReplacementParticipant(
                            Runtime::
                                SceneReplacementParticipantDesc{
                                    .Name =
                                        "Runtime.AssetWorkflowModule",
                                    .BeforeReplace = {},
                                    .AfterReplace = {},
                                });
                releasedNameWasReusable =
                    recycled.has_value();
                if (recycled.has_value())
                {
                    recycledHandle = *recycled;
                    recycledHandleReleased =
                        harness.Document
                            .UnregisterReplacementParticipant(
                                *recycled)
                            .has_value();
                }
            });
    ASSERT_TRUE(releaseProbe.IsValid());

    // Subscribe the document owner after the probe. AssetWorkflow and the
    // texture-bake module release their state before the exact-name probe,
    // followed by document quiescence.
    ASSERT_TRUE(
        harness.Document.OnRegister(setup).has_value());
    harness.DocumentRegistered = true;
    harness.Services.BeginResolution();
    ASSERT_TRUE(
        harness.Document.OnResolve(setup).has_value());
    ASSERT_TRUE(harness.Asset.OnResolve(setup).has_value());
    harness.AssetResolved = true;
    ASSERT_TRUE(
        harness.TextureBake.OnResolve(setup).has_value());
    harness.TextureBakeResolved = true;
    harness.Services.Lock();
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
    EXPECT_TRUE(releaseProbeCalled);
    EXPECT_TRUE(releasedNameWasReusable);
    EXPECT_TRUE(recycledHandleReleased);
    harness.Events.Unsubscribe(releaseProbe);

    EXPECT_EQ(harness.QuiesceGpuParticipants(), 2u);
    EXPECT_EQ(harness.QuiesceGpuParticipants(), 0u);

    Runtime::RuntimeModuleShutdownContext context{
        .Commands = harness.Commands,
        .Events = harness.Events,
        .Jobs = harness.Jobs,
        .Worlds = harness.Worlds,
        .Services = harness.Services,
    };
    harness.TextureBake.OnShutdown(context);
    harness.TextureBakeRegistered = false;
    harness.TextureBakeResolved = false;
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

    harness.Stop();

    // Rebuild the next boot manually and recycle the same slot index before
    // the current AssetWorkflow participant claims it. Participant
    // generations must remain monotonic across the document module's per-boot
    // state reset so the old complete handle cannot become valid again.
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.ProvideBuiltins().has_value());
    Runtime::EngineSetup nextSetup = harness.MakeSetup();
    ASSERT_TRUE(
        harness.Asset.OnRegister(nextSetup).has_value());
    harness.AssetRegistered = true;
    ASSERT_TRUE(
        harness.TextureBake.OnRegister(nextSetup).has_value());
    harness.TextureBakeRegistered = true;
    ASSERT_TRUE(
        harness.Document.OnRegister(nextSetup).has_value());
    harness.DocumentRegistered = true;
    harness.Services.BeginResolution();
    ASSERT_TRUE(
        harness.Document.OnResolve(nextSetup).has_value());

    auto generationSeedA =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowGenerationSeedA",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    auto generationSeedB =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowGenerationSeedB",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    ASSERT_TRUE(generationSeedA.has_value());
    ASSERT_TRUE(generationSeedB.has_value());
    const Runtime::SceneReplacementParticipantHandle* const reusedSeed =
        generationSeedA->Index == recycledHandle.Index
        ? &*generationSeedA
        : (generationSeedB->Index == recycledHandle.Index
               ? &*generationSeedB
               : nullptr);
    ASSERT_NE(reusedSeed, nullptr);
    EXPECT_NE(
        reusedSeed->Generation,
        recycledHandle.Generation);
    ASSERT_TRUE(
        harness.Document
            .UnregisterReplacementParticipant(
                *generationSeedA)
            .has_value());
    ASSERT_TRUE(
        harness.Document
            .UnregisterReplacementParticipant(
                *generationSeedB)
            .has_value());

    ASSERT_TRUE(
        harness.Asset.OnResolve(nextSetup).has_value());
    harness.AssetResolved = true;
    ASSERT_TRUE(
        harness.TextureBake.OnResolve(nextSetup).has_value());
    harness.TextureBakeResolved = true;
    harness.Services.Lock();
    harness.Initialized = true;

    // The two owners consume the recycled slots with new module-lifetime
    // generations. A stale first-boot teardown must not detach this boot's
    // AssetWorkflow participant, and its callback must still run.
    const auto duplicateCurrentName =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowModule",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    EXPECT_FALSE(duplicateCurrentName.has_value());

    Graphics::GpuAssetCache* const cache =
        harness.Services.Find<Graphics::GpuAssetCache>();
    ASSERT_NE(cache, nullptr);
    ECS::Scene::Registry* const scene =
        harness.Worlds.Get(harness.InitialWorld);
    ASSERT_NE(scene, nullptr);
    (void)MakeProceduralRenderable(*scene);
    const auto extracted =
        harness.Extraction.ExtractAndSubmit(
            *scene, *harness.Renderer, cache);
    ASSERT_GT(extracted.CandidateRenderableCount, 0u);
    ASSERT_GT(
        harness.Extraction.GetTrackedRenderableCount(),
        0u);

    bool observerCalled = false;
    bool observerSawAssetCallback = false;
    auto observer =
        harness.Document.RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Runtime.AssetWorkflowObserver",
                .BeforeReplace =
                    [&](const Runtime::
                            SceneReplacementContext&)
                    {
                        observerCalled = true;
                        observerSawAssetCallback =
                            harness.Extraction
                                .GetTrackedRenderableCount() ==
                            0u;
                    },
                .AfterReplace = {},
            });
    ASSERT_TRUE(observer.has_value());

    const Core::Result staleDetach =
        harness.Document.UnregisterReplacementParticipant(
            recycledHandle);
    EXPECT_FALSE(staleDetach.has_value());
    ASSERT_TRUE(
        harness.Document.NewSceneDocument().has_value());
    EXPECT_TRUE(observerCalled);
    EXPECT_TRUE(observerSawAssetCallback);
    EXPECT_TRUE(
        harness.Document
            .UnregisterReplacementParticipant(*observer)
            .has_value());
}

TEST(AssetWorkflowModule,
     BakeParticipantRegistersOncePerBootAndCleansBeforeOwnedState)
{
    DirectHarness harness;
    Runtime::AssetImportPipeline* persistentPipeline =
        nullptr;

    for (std::uint32_t boot = 0u; boot < 2u; ++boot)
    {
        SCOPED_TRACE(boot);
        ASSERT_TRUE(harness.Start().has_value());
        harness.Initialized = true;

        Runtime::AssetImportPipeline* const pipeline =
            harness.Services.Find<
                Runtime::AssetImportPipeline>();
        ASSERT_NE(pipeline, nullptr);
        if (persistentPipeline == nullptr)
            persistentPipeline = pipeline;
        EXPECT_EQ(pipeline, persistentPipeline);

        // The texture-bake module registers one legacy normal-bake
        // participant plus one generalized property-raster participant.
        EXPECT_EQ(
            harness.Jobs
                .DrainGpuQueueCompletedTransfers(),
            2u);

        Runtime::RuntimeObjectSpaceNormalBakeQueue* queue =
            nullptr;
        bool processorCalled = false;
        bool operational = false;
        const std::uint64_t bakeKey =
            static_cast<std::uint64_t>(boot) + 1u;
        const auto processor =
            pipeline->RegisterPostImportProcessor(
                Runtime::RuntimePostImportProcessorDesc{
                    .DebugName =
                        "RUNTIME-183 composed bake probe",
                    .PayloadKind =
                        Assets::AssetPayloadKind::Mesh,
                        .Process =
                        [&](const Runtime::
                                RuntimePostImportProcessorContext&
                                    context,
                            Runtime::
                                RuntimePostImportProcessorServices&
                                    services)
                            -> Core::Result
                        {
                            processorCalled = true;
                            queue =
                                services
                                    .ObjectSpaceNormalBakeQueue;
                            operational =
                                services.ObjectSpaceNormalBakeDevice !=
                                    nullptr &&
                                services.ObjectSpaceNormalBakeDevice->
                                    IsOperational();
                            if (queue == nullptr)
                            {
                                return Core::Err(
                                    Core::ErrorCode::
                                        InvalidState);
                            }
                            const auto scheduled =
                                queue->Schedule(
                                    MakeBakeRequest(
                                        bakeKey,
                                        services.World,
                                        services
                                            .ObjectSpaceNormalBakeBindingEpoch,
                                        context.Entity),
                                    operational);
                            return scheduled.Succeeded()
                                ? Core::Ok()
                                : Core::Err(
                                      Core::ErrorCode::
                                          InvalidState);
                        },
                });
        ASSERT_TRUE(processor.IsValid());

        TempObjFile mesh{
            boot == 0u
                ? "runtime183-bake-boot-0.obj"
                : "runtime183-bake-boot-1.obj"};
        auto imported =
            pipeline->ImportAssetFromPath(
                Runtime::RuntimeAssetImportRequest{
                    .Path = mesh.Path.string(),
                    .PayloadKind =
                        Assets::AssetPayloadKind::Mesh,
                });
        ASSERT_TRUE(imported.has_value())
            << static_cast<int>(imported.error());
        ASSERT_TRUE(processorCalled);
        ASSERT_TRUE(operational);
        ASSERT_NE(queue, nullptr);
        EXPECT_EQ(queue->PendingCount(), 1u);
        EXPECT_TRUE(harness.Jobs.HasGpuQueueWork());

        const std::uint64_t idleBefore =
            harness.GpuIdleWaits;
        EXPECT_EQ(harness.QuiesceGpuParticipants(), 2u);
        EXPECT_EQ(
            harness.GpuIdleWaits, idleBefore + 1u);
        EXPECT_EQ(queue->PendingCount(), 0u);
        EXPECT_FALSE(harness.Jobs.HasGpuQueueWork());
        EXPECT_EQ(harness.QuiesceGpuParticipants(), 0u);
        EXPECT_EQ(
            harness.GpuIdleWaits, idleBefore + 1u);

        pipeline->UnregisterPostImportProcessor(processor);
        harness.Stop();
        // The queue is module-state owned and no longer addressable after
        // shutdown; quiescence above proves it was cleared before teardown.
        EXPECT_EQ(
            harness.Services.Find<
                Runtime::AssetImportPipeline>(),
            nullptr);
    }
}

TEST(AssetWorkflowModule,
     BlockedImportIsSafeInBothOrdinaryOwnerShutdownOrders)
{
    enum class ShutdownOrder
    {
        AsyncThenAsset,
        AssetThenAsync,
    };

    SchedulerScope scheduler;
    for (const ShutdownOrder order :
         {ShutdownOrder::AsyncThenAsset,
          ShutdownOrder::AssetThenAsync})
    {
        SCOPED_TRACE(
            order == ShutdownOrder::AsyncThenAsset
                ? "direct Async-before-Asset"
                : "direct Asset-before-Async");

        DirectHarness harness;
        Runtime::AsyncWorkModule async;

        harness.Services.BeginRegistration();
        ASSERT_TRUE(harness.ProvideBuiltins().has_value());
        harness.Selection =
            std::make_unique<Runtime::SelectionController>();
        ASSERT_TRUE(
            harness.Services
                .Provide<Runtime::SelectionController>(
                    *harness.Selection,
                    "Test.Interaction")
                .has_value());

        Runtime::EngineSetup setup = harness.MakeSetup();
        ASSERT_TRUE(
            harness.Asset.OnRegister(setup).has_value());
        harness.AssetRegistered = true;
        ASSERT_TRUE(async.OnRegister(setup).has_value());
        ASSERT_TRUE(
            harness.Document.OnRegister(setup).has_value());
        harness.DocumentRegistered = true;
        ASSERT_TRUE(
            harness.TextureBake.OnRegister(setup).has_value());
        harness.TextureBakeRegistered = true;

        harness.Services.BeginResolution();
        ASSERT_TRUE(
            harness.Asset.OnResolve(setup).has_value());
        harness.AssetResolved = true;
        ASSERT_TRUE(async.OnResolve(setup).has_value());
        ASSERT_TRUE(
            harness.Document.OnResolve(setup).has_value());
        ASSERT_TRUE(
            harness.TextureBake.OnResolve(setup).has_value());
        harness.TextureBakeResolved = true;
        harness.Services.Lock();
        harness.Initialized = true;

        Runtime::AssetImportPipeline* const pipeline =
            harness.Services.Find<
                Runtime::AssetImportPipeline>();
        Runtime::StreamingExecutor* const streaming =
            harness.Services.Find<
                Runtime::StreamingExecutor>();
        Runtime::EditorCommandHistory* const history =
            harness.Services.Find<
                Runtime::EditorCommandHistory>();
        ASSERT_NE(pipeline, nullptr);
        ASSERT_NE(streaming, nullptr);
        ASSERT_NE(history, nullptr);

        std::atomic_bool workerStarted{false};
        std::atomic_bool releaseWorker{false};
        WorkerRelease releaseOnExit{releaseWorker};
        pipeline->
            SetQueuedGeometryImportBeforeDecodeHookForTest(
                [&](const Runtime::
                        RuntimeAssetImportRequest&)
                {
                    workerStarted.store(
                        true, std::memory_order_release);
                    while (!releaseWorker.load(
                        std::memory_order_acquire))
                    {
                        std::this_thread::sleep_for(1ms);
                    }
                });

        TempObjFile mesh{
            order == ShutdownOrder::AsyncThenAsset
                ? "runtime183-shutdown-async-first.obj"
                : "runtime183-shutdown-asset-first.obj"};
        auto queued =
            pipeline->QueueGeometryImport(
                Runtime::RuntimeAssetImportRequest{
                    .Path = mesh.Path.string(),
                    .PayloadKind =
                        Assets::AssetPayloadKind::Mesh,
                });
        ASSERT_TRUE(queued.has_value())
            << static_cast<int>(queued.error());
        streaming->PumpBackground(1u);
        ASSERT_TRUE(WaitUntil(workerStarted));

        harness.Announce();
        const auto announced =
            pipeline->GetAssetImportQueueSnapshot();
        ASSERT_EQ(announced.Entries.size(), 1u);
        EXPECT_EQ(announced.ActiveCount, 0u);
        EXPECT_EQ(announced.TerminalCount, 1u);
        EXPECT_EQ(
            announced.Entries[0].TerminalStatus,
            Runtime::
                RuntimeAssetImportQueueTerminalStatus::
                    Cancelled);
        EXPECT_EQ(
            harness.QuiesceGpuParticipants(), 2u);

        Runtime::RuntimeModuleShutdownContext context{
            .Commands = harness.Commands,
            .Events = harness.Events,
            .Jobs = harness.Jobs,
            .Worlds = harness.Worlds,
            .Services = harness.Services,
        };
        if (order == ShutdownOrder::AsyncThenAsset)
        {
            releaseWorker.store(
                true, std::memory_order_release);
            async.OnShutdown(context);
            harness.TextureBake.OnShutdown(context);
            harness.Asset.OnShutdown(context);
        }
        else
        {
            // The worker still owns a callback capturing the persistent
            // pipeline while per-boot asset/cache state is withdrawn.
            harness.TextureBake.OnShutdown(context);
            harness.Asset.OnShutdown(context);
            releaseWorker.store(
                true, std::memory_order_release);
            async.OnShutdown(context);
        }
        harness.TextureBakeRegistered = false;
        harness.TextureBakeResolved = false;
        harness.AssetRegistered = false;
        harness.AssetResolved = false;

        const auto drained =
            pipeline->GetAssetImportQueueSnapshot();
        ASSERT_EQ(drained.Entries.size(), 1u);
        EXPECT_EQ(drained.ActiveCount, 0u);
        EXPECT_EQ(drained.TerminalCount, 1u);
        EXPECT_EQ(
            drained.Entries[0].TerminalStatus,
            Runtime::
                RuntimeAssetImportQueueTerminalStatus::
                    Cancelled);
        EXPECT_EQ(
            EntityCount(
                *harness.Worlds.Get(
                    harness.Worlds.ActiveWorld())),
            0u);
        EXPECT_EQ(history->Snapshot().Revision, 0u);
        EXPECT_TRUE(
            harness.Selection
                ->SelectedStableIds()
                .empty());
        EXPECT_EQ(
            harness.Services.Find<
                Assets::AssetService>(),
            nullptr);
        EXPECT_EQ(
            harness.Services.Find<
                Runtime::StreamingExecutor>(),
            nullptr);

        pipeline->
            SetQueuedGeometryImportBeforeDecodeHookForTest(
                {});
        harness.Document.OnShutdown(context);
        harness.DocumentRegistered = false;
        harness.Services.Reset();
        harness.Announced = false;
    }
}

TEST(AssetWorkflowModule,
     RenderExtractionRetirementSurvivesEitherOptionalOwnerOmission)
{
    enum class OmittedOwner
    {
        AssetWorkflow,
        AsyncWork,
    };

    for (const OmittedOwner omitted :
         {OmittedOwner::AssetWorkflow,
          OmittedOwner::AsyncWork})
    {
        SCOPED_TRACE(
            omitted == OmittedOwner::AssetWorkflow
                ? "AssetWorkflow omitted"
                : "AsyncWork omitted");
        auto application =
            std::make_unique<FixedFrameApplication>(8u);
        FixedFrameApplication* const observed =
            application.get();
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
        if (omitted == OmittedOwner::AssetWorkflow)
        {
            engine.EmplaceModule<Runtime::AsyncWorkModule>();
        }
        else
        {
            engine.EmplaceModule<
                Runtime::AssetWorkflowModule>();
            engine.EmplaceModule<
                Runtime::SceneDocumentModule>();
        }
        engine.Initialize();

        EXPECT_EQ(
            engine.Services()
                .Find<Core::IAssetFrameHooks>() == nullptr,
            omitted == OmittedOwner::AssetWorkflow);
        EXPECT_EQ(
            engine.Services()
                    .Find<Core::IStreamingFrameHooks>() ==
                nullptr,
            omitted == OmittedOwner::AsyncWork);

        Runtime::RenderExtractionCache* const extraction =
            engine.Services()
                .Find<Runtime::RenderExtractionCache>();
        ASSERT_NE(extraction, nullptr);
        ECS::Scene::Registry* const scene =
            engine.Worlds().Get(engine.ActiveWorld());
        ASSERT_NE(scene, nullptr);
        const ECS::EntityHandle entity =
            MakeProceduralRenderable(*scene);
        auto stats =
            extraction->ExtractAndSubmit(
                *scene, engine.GetRenderer(), nullptr);
        ASSERT_EQ(stats.ProceduralGeometryUploads, 1u);
        scene->Destroy(entity);
        stats =
            extraction->ExtractAndSubmit(
                *scene, engine.GetRenderer(), nullptr);
        ASSERT_EQ(stats.ProceduralGeometryReleases, 1u);
        ASSERT_EQ(
            engine.GetRenderer()
                .GetGpuWorld()
                .GetLiveGeometryCount(),
            1u);

        engine.Run();

        EXPECT_EQ(observed->ObservedFrames, 8u);
        EXPECT_EQ(
            engine.GetRenderer()
                .GetGpuWorld()
                .GetLiveGeometryCount(),
            0u);
        engine.Shutdown();
    }
}

TEST(AssetWorkflowModule,
     RealEngineReverseShutdownIsAsyncBeforeAssetWorkflow)
{
    auto application =
        std::make_unique<RunOnceApplication>(
            /*expectAssets=*/true);
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
    ShutdownOrderObservation observation{};
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<ShutdownOrderProbeModule>(
        observation);

    engine.Initialize();
    engine.Run();
    engine.Shutdown();

    EXPECT_TRUE(observation.Seen);
    EXPECT_TRUE(observation.StreamingWithdrawn);
    EXPECT_TRUE(
        observation.AssetServicesStillPublished);
}

TEST(AssetWorkflowModule,
     OmittedAssetWorkflowPlatformDropFailsClosedWithoutStreamingOrSceneMutation)
{
    TempObjFile mesh{"runtime183-omitted-drop.obj"};
    auto application =
        std::make_unique<FixedFrameApplication>(2u);
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.Initialize();

    EXPECT_EQ(
        engine.Services().Find<Assets::AssetService>(),
        nullptr);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::AssetImportPipeline>(),
        nullptr);
    EXPECT_EQ(
        engine.Services().Find<Graphics::GpuAssetCache>(),
        nullptr);
    EXPECT_EQ(
        engine.Services().Find<Core::IAssetFrameHooks>(),
        nullptr);

    Runtime::StreamingExecutor* const streaming =
        engine.Services().Find<Runtime::StreamingExecutor>();
    ASSERT_NE(streaming, nullptr);
    ECS::Scene::Registry* const scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto beforeStreaming =
        streaming->GetDiagnostics();
    const std::size_t beforeEntities =
        EntityCount(*scene);

    engine.DispatchPlatformEventForTest(
        Extrinsic::Platform::WindowDropEvent{
            .Paths = {mesh.Path.string()},
        });
    engine.Run();

    const auto afterStreaming =
        streaming->GetDiagnostics();
    EXPECT_EQ(
        afterStreaming.SlotCount,
        beforeStreaming.SlotCount);
    EXPECT_EQ(
        afterStreaming.ActiveSlotCount,
        beforeStreaming.ActiveSlotCount);
    EXPECT_EQ(
        afterStreaming.ReadyTaskCount,
        beforeStreaming.ReadyTaskCount);
    EXPECT_EQ(
        afterStreaming.ReadyForApplyCount,
        beforeStreaming.ReadyForApplyCount);
    EXPECT_EQ(EntityCount(*scene), beforeEntities);
    engine.Shutdown();
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
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));

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

    TempObjFile initializedGateMesh{
        "runtime183-engine-initialize-gate.obj"};
    auto application =
        std::make_unique<RunOnceApplication>(
            /*expectAssets=*/true,
            initializedGateMesh.Path.string());
    RunOnceApplication* const observed =
        application.get();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
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
    EXPECT_EQ(
        observed->InitializeImportErrors,
        (std::vector<Core::ErrorCode>{
            Core::ErrorCode::InvalidState,
            Core::ErrorCode::InvalidState}));
    EXPECT_EQ(
        observed->RuntimeImportSucceeded,
        (std::vector<bool>{true, true}));
}
