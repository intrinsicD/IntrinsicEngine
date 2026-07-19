// RUNTIME-168 real Sandbox default-policy composition coverage.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox;

namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace Graphics = Extrinsic::Graphics;
namespace Platform = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;
namespace Selection = Extrinsic::ECS::Components::Selection;
namespace World = Extrinsic::ECS::Components::Culling::World;

namespace
{
    struct SandboxLifecycleProbe
    {
        bool PipelineLiveAfterSandboxShutdown{false};
        bool InputRegistryLiveAfterSandboxShutdown{false};
        std::size_t SandboxShutdownCalls{0u};
    };

    class SandboxHarnessApplication final : public Runtime::IApplication
    {
    public:
        SandboxHarnessApplication(
            SandboxLifecycleProbe& probe,
            const bool repeatSandboxShutdown)
            : m_Probe(probe)
            , m_RepeatSandboxShutdown(repeatSandboxShutdown)
            , m_Sandbox(Extrinsic::Sandbox::CreateSandboxApp())
        {
        }

        void OnInitialize(Runtime::Engine& engine) override
        {
            if (m_Sandbox)
                m_Sandbox->OnInitialize(engine);
        }

        void OnSimTick(Runtime::Engine& engine, const double fixedDt) override
        {
            if (m_Sandbox)
                m_Sandbox->OnSimTick(engine, fixedDt);
        }

        void OnVariableTick(
            Runtime::Engine& engine,
            const double alpha,
            const double dt) override
        {
            if (m_Sandbox)
                m_Sandbox->OnVariableTick(engine, alpha, dt);
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine& engine) override
        {
            if (!m_Sandbox)
                return;

            m_Sandbox->OnShutdown(engine);
            ++m_Probe.SandboxShutdownCalls;

            if (m_RepeatSandboxShutdown)
            {
                m_Sandbox->OnShutdown(engine);
                ++m_Probe.SandboxShutdownCalls;
            }

            m_Probe.PipelineLiveAfterSandboxShutdown =
                engine.Services().Find<Runtime::AssetImportPipeline>() !=
                nullptr;
            m_Probe.InputRegistryLiveAfterSandboxShutdown =
                engine.Services()
                    .Find<Runtime::RuntimeInputActionRegistry>() !=
                nullptr;
        }

    private:
        SandboxLifecycleProbe& m_Probe;
        bool m_RepeatSandboxShutdown{false};
        std::unique_ptr<Runtime::IApplication> m_Sandbox{};
    };

    struct BlockedImportShutdownProbe
    {
        std::atomic_bool WorkerStarted{false};
        std::atomic_bool ReleaseWorker{false};
        std::atomic_bool GpuParticipantShutdown{false};
        std::atomic_uint32_t CompletionCalls{0u};
        Runtime::RuntimeAssetIngestHandle Operation{};
        Runtime::AssetImportPipeline* ExpectedPipeline{nullptr};
        Runtime::RuntimeInputActionRegistry* ExpectedInputRegistry{nullptr};
        std::uint32_t ObservedFrames{0u};
        bool FrameLimitExceeded{false};
        bool ExitRequestedWhileWorkerBlocked{false};
        bool WorkerBlockedBeforeSandboxShutdown{false};
        bool AnnouncementCancelledBeforeSandboxShutdown{false};
        bool GpuParticipantShutdownBeforeSandboxShutdown{false};
        bool PipelineIdentityLiveBeforeSandboxShutdown{false};
        bool InputIdentityLiveBeforeSandboxShutdown{false};
        bool PipelineIdentityLiveAfterSandboxShutdown{false};
        bool InputIdentityLiveAfterSandboxShutdown{false};
        bool SandboxUnregisteredBeforeWorkerRelease{false};
        bool ReleaseIssuedAfterSandboxShutdown{false};
    };

    class BlockedImportShutdownApplication final
        : public Runtime::IApplication
    {
    public:
        explicit BlockedImportShutdownApplication(
            std::shared_ptr<BlockedImportShutdownProbe> probe)
            : m_Probe(std::move(probe))
            , m_Sandbox(Extrinsic::Sandbox::CreateSandboxApp())
        {
        }

        void OnInitialize(Runtime::Engine& engine) override
        {
            if (m_Sandbox)
                m_Sandbox->OnInitialize(engine);
        }

        void OnSimTick(
            Runtime::Engine& engine,
            const double fixedDt) override
        {
            if (m_Sandbox)
                m_Sandbox->OnSimTick(engine, fixedDt);
        }

        void OnVariableTick(
            Runtime::Engine& engine,
            const double alpha,
            const double dt) override
        {
            if (m_Sandbox)
                m_Sandbox->OnVariableTick(engine, alpha, dt);

            ++m_Probe->ObservedFrames;
            if (m_Probe->WorkerStarted.load(std::memory_order_acquire))
            {
                m_Probe->ExitRequestedWhileWorkerBlocked =
                    !m_Probe->ReleaseWorker.load(
                        std::memory_order_acquire);
                engine.RequestExit();
                return;
            }

            if (m_Probe->ObservedFrames >= 512u)
            {
                m_Probe->FrameLimitExceeded = true;
                m_Probe->ReleaseWorker.store(
                    true,
                    std::memory_order_release);
                engine.RequestExit();
                return;
            }

            std::this_thread::yield();
        }

        void OnShutdown(Runtime::Engine& engine) override
        {
            m_Probe->WorkerBlockedBeforeSandboxShutdown =
                m_Probe->WorkerStarted.load(std::memory_order_acquire) &&
                !m_Probe->ReleaseWorker.load(std::memory_order_acquire);
            m_Probe->GpuParticipantShutdownBeforeSandboxShutdown =
                m_Probe->GpuParticipantShutdown.load(
                    std::memory_order_acquire);
            m_Probe->PipelineIdentityLiveBeforeSandboxShutdown =
                engine.Services().Find<Runtime::AssetImportPipeline>() ==
                m_Probe->ExpectedPipeline;
            m_Probe->InputIdentityLiveBeforeSandboxShutdown =
                engine.Services()
                    .Find<Runtime::RuntimeInputActionRegistry>() ==
                m_Probe->ExpectedInputRegistry;

            if (m_Probe->ExpectedPipeline != nullptr)
            {
                const Runtime::RuntimeAssetImportQueueSnapshot snapshot =
                    m_Probe->ExpectedPipeline
                        ->GetAssetImportQueueSnapshot();
                for (const Runtime::RuntimeAssetImportQueueEntry& entry :
                     snapshot.Entries)
                {
                    if (entry.Operation == m_Probe->Operation)
                    {
                        m_Probe->
                            AnnouncementCancelledBeforeSandboxShutdown =
                            entry.TerminalStatus ==
                            Runtime::
                                RuntimeAssetImportQueueTerminalStatus::
                                    Cancelled;
                        break;
                    }
                }
            }

            if (m_Sandbox)
                m_Sandbox->OnShutdown(engine);

            m_Probe->PipelineIdentityLiveAfterSandboxShutdown =
                engine.Services().Find<Runtime::AssetImportPipeline>() ==
                m_Probe->ExpectedPipeline;
            m_Probe->InputIdentityLiveAfterSandboxShutdown =
                engine.Services()
                    .Find<Runtime::RuntimeInputActionRegistry>() ==
                m_Probe->ExpectedInputRegistry;
            m_Probe->SandboxUnregisteredBeforeWorkerRelease =
                !m_Probe->ReleaseWorker.load(std::memory_order_acquire);
            m_Probe->ReleaseWorker.store(
                true,
                std::memory_order_release);
            m_Probe->ReleaseIssuedAfterSandboxShutdown = true;
        }

    private:
        std::shared_ptr<BlockedImportShutdownProbe> m_Probe{};
        std::unique_ptr<Runtime::IApplication> m_Sandbox{};
    };

    class RecordingCameraController final
        : public Runtime::ICameraController
    {
    public:
        void Seed(const Graphics::CameraViewInput& seed) noexcept override
        {
            if (seed.Valid)
                m_View = seed;
        }

        void Focus(const Runtime::CameraFocusTarget) noexcept override
        {
            ++FocusCalls;
        }

        void Update(
            const Platform::Input::Context&,
            double) noexcept override
        {
        }

        [[nodiscard]] Graphics::CameraViewInput GetView(
            Core::Extent2D) const noexcept override
        {
            return m_View;
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind()
            const noexcept override
        {
            return Core::Config::CameraControllerKind::Orbit;
        }

        std::uint32_t FocusCalls{0u};

    private:
        Graphics::CameraViewInput m_View{
            Runtime::DefaultCameraControllerSeed()};
    };

    class TempObjFile
    {
    public:
        TempObjFile(std::string name, const std::string_view body)
            : Path(std::filesystem::temp_directory_path() / std::move(name))
        {
            std::ofstream output(Path, std::ios::binary | std::ios::trunc);
            output << body;
        }

        ~TempObjFile()
        {
            std::error_code error{};
            std::filesystem::remove(Path, error);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = true;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    void ComposeSandboxProviders(
        Runtime::Engine& engine,
        const bool composeAssets,
        const bool composeCamera,
        const bool composeSelection)
    {
        engine.EmplaceModule<Runtime::AsyncWorkModule>();
        if (composeCamera)
            engine.EmplaceModule<Runtime::CameraModule>();
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        if (composeSelection)
            engine.EmplaceModule<Runtime::SceneInteractionModule>();
        if (composeAssets)
            engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    }

    [[nodiscard]] std::size_t SelectableCount(
        ECS::Scene::Registry& scene)
    {
        return scene.Raw().view<Selection::SelectableTag>().size();
    }

    [[nodiscard]] std::string ReadRepositoryTextFile(
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} / relativePath;
        std::ifstream input(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    [[nodiscard]] std::string WithoutWhitespace(std::string text)
    {
        std::erase_if(
            text,
            [](const unsigned char character)
            {
                return std::isspace(character) != 0;
            });
        return text;
    }

    [[nodiscard]] std::size_t CountOccurrences(
        const std::string_view text,
        const std::string_view needle)
    {
        std::size_t count = 0u;
        std::size_t offset = 0u;
        while ((offset = text.find(needle, offset)) !=
               std::string_view::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    [[nodiscard]] RecordingCameraController*
    ReplaceMainCameraWithRecording(Runtime::Engine& engine)
    {
        auto* const cameras =
            engine.Services().Find<Runtime::CameraControllerRegistry>();
        EXPECT_NE(cameras, nullptr);
        if (cameras == nullptr)
            return nullptr;

        auto controller =
            std::make_unique<RecordingCameraController>();
        RecordingCameraController* const recording = controller.get();
        cameras->Replace(
            Runtime::CameraControllerSlot::Main,
            std::move(controller));
        return recording;
    }

    void DispatchFocusAction(Runtime::Engine& engine)
    {
        auto* const actions =
            engine.Services().Find<Runtime::RuntimeInputActionRegistry>();
        auto* const scene =
            engine.Worlds().Get(engine.ActiveWorld());
        ASSERT_NE(actions, nullptr);
        ASSERT_NE(scene, nullptr);

        const Platform::IWindow& window = engine.GetWindow();
        Platform::Input::Context input = window.GetInput();
        input.SetKeyState(Platform::Input::Key::F, true);
        Graphics::RenderFrameInput renderInput{};
        actions->DispatchForFrame(
            engine.GetEngineConfig(),
            *scene,
            input,
            window.GetFramebufferExtent(),
            false,
            0.0,
            0u,
            renderInput);
    }
}

TEST(SandboxAppComposition, MissingAssetWorkflowLeavesRequiredPolicySetAbsent)
{
    SandboxLifecycleProbe probe{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<SandboxHarnessApplication>(
            probe,
            false));
    ComposeSandboxProviders(engine, false, true, true);

    engine.Initialize();

    EXPECT_EQ(
        engine.Services().Find<Runtime::AssetImportPipeline>(),
        nullptr);
    EXPECT_NE(
        engine.Services().Find<Runtime::RuntimeInputActionRegistry>(),
        nullptr);

    auto* const scene =
        engine.Worlds().Get(engine.ActiveWorld());
    auto* const selection =
        engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(selection, nullptr);
    const ECS::EntityHandle entity = scene->Create();
    scene->Raw().emplace<Selection::SelectableTag>(entity);
    World::Bounds bounds{};
    bounds.WorldBoundingSphere.Center =
        glm::vec3{4.0f, 2.0f, -1.0f};
    bounds.WorldBoundingSphere.Radius = 2.5f;
    scene->Raw().emplace<World::Bounds>(entity, bounds);
    ASSERT_TRUE(selection->SetSelectedEntity(*scene, entity));
    RecordingCameraController* const recording =
        ReplaceMainCameraWithRecording(engine);
    ASSERT_NE(recording, nullptr);
    DispatchFocusAction(engine);
    EXPECT_EQ(recording->FocusCalls, 0u);

    engine.Shutdown();
    EXPECT_FALSE(probe.PipelineLiveAfterSandboxShutdown);
    EXPECT_TRUE(probe.InputRegistryLiveAfterSandboxShutdown);
}

TEST(SandboxAppComposition, PipelineSelectionDoesNotRequireCamera)
{
    TempObjFile mesh(
        "runtime168_without_camera.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    SandboxLifecycleProbe probe{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<SandboxHarnessApplication>(
            probe,
            false));
    ComposeSandboxProviders(engine, true, false, true);
    engine.Initialize();

    auto* const pipeline =
        engine.Services().Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(pipeline, nullptr);
    const auto imported = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = mesh.Path.string(),
            .PayloadKind =
                Extrinsic::Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    EXPECT_EQ(
        engine.Services().Find<Runtime::CameraControllerRegistry>(),
        nullptr);
    auto* const selection =
        engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    EXPECT_EQ(selection->SelectedCount(), 1u);

    engine.Shutdown();
}

TEST(SandboxAppComposition, MaterializationAndAutofocusDoNotRequireSelection)
{
    TempObjFile mesh(
        "runtime168_without_selection.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    SandboxLifecycleProbe probe{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<SandboxHarnessApplication>(
            probe,
            false));
    ComposeSandboxProviders(engine, true, true, false);
    engine.Initialize();

    auto* const pipeline =
        engine.Services().Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(pipeline, nullptr);
    auto* const cameras =
        engine.Services().Find<Runtime::CameraControllerRegistry>();
    ASSERT_NE(cameras, nullptr);
    EXPECT_EQ(
        cameras->ResolveOrNull(Runtime::CameraControllerSlot::Main),
        nullptr);

    const auto imported = pipeline->ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = mesh.Path.string(),
            .PayloadKind =
                Extrinsic::Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    EXPECT_EQ(
        engine.Services().Find<Runtime::SelectionController>(),
        nullptr);
    EXPECT_NE(
        cameras->ResolveOrNull(Runtime::CameraControllerSlot::Main),
        nullptr);
    EXPECT_TRUE(cameras->ConsumeCameraTransition(
        Runtime::CameraControllerSlot::Main));
    auto* const scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(SelectableCount(*scene), 1u);

    RecordingCameraController* const recording =
        ReplaceMainCameraWithRecording(engine);
    ASSERT_NE(recording, nullptr);
    DispatchFocusAction(engine);
    EXPECT_EQ(recording->FocusCalls, 0u);

    engine.Shutdown();
}

TEST(SandboxAppComposition, ReinitializeAndRepeatedShutdownDoNotDuplicateState)
{
    TempObjFile firstMesh(
        "runtime168_reinitialize_first.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    TempObjFile secondMesh(
        "runtime168_reinitialize_second.obj",
        "v 0 0 0\n"
        "v 2 0 0\n"
        "v 0 2 0\n"
        "f 1 2 3\n");
    SandboxLifecycleProbe probe{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<SandboxHarnessApplication>(
            probe,
            true));
    ComposeSandboxProviders(engine, true, true, true);

    engine.Initialize();
    auto* pipeline =
        engine.Services().Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(pipeline, nullptr);
    ASSERT_TRUE(
        pipeline
            ->ImportAssetFromPath(
                Runtime::RuntimeAssetImportRequest{
                    .Path = firstMesh.Path.string(),
                    .PayloadKind =
                        Extrinsic::Assets::AssetPayloadKind::Mesh,
                })
            .has_value());
    ASSERT_EQ(
        engine.Services()
            .Find<Runtime::SelectionController>()
            ->SelectedCount(),
        1u);
    engine.Shutdown();

    EXPECT_EQ(probe.SandboxShutdownCalls, 2u);
    EXPECT_TRUE(probe.PipelineLiveAfterSandboxShutdown);
    EXPECT_TRUE(probe.InputRegistryLiveAfterSandboxShutdown);

    engine.Initialize();
    pipeline =
        engine.Services().Find<Runtime::AssetImportPipeline>();
    ASSERT_NE(pipeline, nullptr);
    RecordingCameraController* const recording =
        ReplaceMainCameraWithRecording(engine);
    ASSERT_NE(recording, nullptr);
    ASSERT_TRUE(
        pipeline
            ->ImportAssetFromPath(
                Runtime::RuntimeAssetImportRequest{
                    .Path = secondMesh.Path.string(),
                    .PayloadKind =
                        Extrinsic::Assets::AssetPayloadKind::Mesh,
                })
            .has_value());
    EXPECT_EQ(recording->FocusCalls, 1u);
    auto* const scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(SelectableCount(*scene), 1u);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::SelectionController>()
            ->SelectedCount(),
        1u);

    DispatchFocusAction(engine);
    EXPECT_EQ(recording->FocusCalls, 2u);

    engine.Shutdown();

    EXPECT_EQ(probe.SandboxShutdownCalls, 4u);
    EXPECT_TRUE(probe.PipelineLiveAfterSandboxShutdown);
    EXPECT_TRUE(probe.InputRegistryLiveAfterSandboxShutdown);
}

TEST(SandboxAppComposition,
     BlockedImportShutdownCancelsBeforeRealSandboxUnregister)
{
    TempObjFile mesh(
        "runtime168_blocked_shutdown.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    auto probe = std::make_shared<BlockedImportShutdownProbe>();
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<BlockedImportShutdownApplication>(probe));
    ComposeSandboxProviders(engine, true, true, true);
    engine.Initialize();

    probe->ExpectedPipeline =
        engine.Services().Find<Runtime::AssetImportPipeline>();
    probe->ExpectedInputRegistry =
        engine.Services().Find<Runtime::RuntimeInputActionRegistry>();
    ASSERT_NE(probe->ExpectedPipeline, nullptr);
    ASSERT_NE(probe->ExpectedInputRegistry, nullptr);

    const Runtime::RuntimeImportCompletedHandlerHandle completionProbe =
        probe->ExpectedPipeline->RegisterImportCompletedHandler(
            Runtime::RuntimeImportCompletedHandlerDesc{
                .DebugName =
                    "RUNTIME-168 blocked shutdown completion probe",
                .PayloadKind =
                    Extrinsic::Assets::AssetPayloadKind::Mesh,
                .Handle =
                    [probe](
                        const Runtime::RuntimeImportCompletedContext&,
                        Runtime::RuntimeImportCompletedServices&)
                    {
                        probe->CompletionCalls.fetch_add(
                            1u,
                            std::memory_order_acq_rel);
                        return Core::Ok();
                    },
            });
    ASSERT_TRUE(completionProbe.IsValid());

    const Runtime::GpuQueueParticipantHandle gpuParticipant =
        engine.Jobs().RegisterGpuQueueParticipant(
            Runtime::GpuQueueParticipantDesc{
                .DebugName =
                    "RUNTIME-168 shutdown ordering probe",
                .HasInFlightWork = [] { return true; },
                .ShutdownAfterDeviceIdle =
                    [probe]
                    {
                        probe->GpuParticipantShutdown.store(
                            true,
                            std::memory_order_release);
                    },
            });
    ASSERT_TRUE(gpuParticipant.IsValid());

    probe->ExpectedPipeline
        ->SetQueuedGeometryImportBeforeDecodeHookForTest(
            [probe](const Runtime::RuntimeAssetImportRequest&)
            {
                probe->WorkerStarted.store(
                    true,
                    std::memory_order_release);
                while (!probe->ReleaseWorker.load(
                    std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
            });

    auto queued = probe->ExpectedPipeline->QueueGeometryImport(
        Runtime::RuntimeAssetImportRequest{
            .Path = mesh.Path.string(),
            .PayloadKind =
                Extrinsic::Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(queued.has_value())
        << static_cast<int>(queued.error());
    probe->Operation = queued->Operation;

    engine.Run();

    ASSERT_FALSE(probe->FrameLimitExceeded);
    ASSERT_TRUE(probe->WorkerStarted.load(std::memory_order_acquire));
    ASSERT_TRUE(probe->ExitRequestedWhileWorkerBlocked);
    ASSERT_FALSE(
        probe->ReleaseWorker.load(std::memory_order_acquire));
    EXPECT_EQ(
        probe->CompletionCalls.load(std::memory_order_acquire),
        0u);

    engine.Shutdown();

    EXPECT_TRUE(probe->WorkerBlockedBeforeSandboxShutdown);
    EXPECT_TRUE(probe->AnnouncementCancelledBeforeSandboxShutdown);
    EXPECT_TRUE(probe->GpuParticipantShutdownBeforeSandboxShutdown);
    EXPECT_TRUE(probe->PipelineIdentityLiveBeforeSandboxShutdown);
    EXPECT_TRUE(probe->InputIdentityLiveBeforeSandboxShutdown);
    EXPECT_TRUE(probe->PipelineIdentityLiveAfterSandboxShutdown);
    EXPECT_TRUE(probe->InputIdentityLiveAfterSandboxShutdown);
    EXPECT_TRUE(probe->SandboxUnregisteredBeforeWorkerRelease);
    EXPECT_TRUE(probe->ReleaseIssuedAfterSandboxShutdown);
    EXPECT_TRUE(
        probe->ReleaseWorker.load(std::memory_order_acquire));
    EXPECT_EQ(
        probe->CompletionCalls.load(std::memory_order_acquire),
        0u);

    const Runtime::RuntimeAssetImportQueueSnapshot snapshot =
        probe->ExpectedPipeline->GetAssetImportQueueSnapshot();
    ASSERT_EQ(snapshot.Entries.size(), 1u);
    EXPECT_EQ(snapshot.Entries[0].Operation, probe->Operation);
    EXPECT_EQ(
        snapshot.Entries[0].TerminalStatus,
        Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);
}

TEST(SandboxAppComposition, DefaultPolicyLifecycleIsPrivateAndTransactional)
{
    const std::string runtimeCMake =
        WithoutWhitespace(
            ReadRepositoryTextFile("src/runtime/CMakeLists.txt"));
    const std::string appCMake =
        WithoutWhitespace(
            ReadRepositoryTextFile("src/app/Sandbox/CMakeLists.txt"));
    const std::string policy =
        WithoutWhitespace(ReadRepositoryTextFile(
            "src/runtime/Runtime.SandboxDefaultPolicies.cpp"));
    const std::string facade =
        WithoutWhitespace(ReadRepositoryTextFile(
            "src/runtime/Runtime.SandboxEditorFacades.cppm"));
    const std::string app =
        WithoutWhitespace(
            ReadRepositoryTextFile("src/app/Sandbox/Sandbox.cpp"));

    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path{ENGINE_ROOT_DIR} /
        "src/runtime/Runtime.SandboxDefaultPolicies.cppm"));
    EXPECT_EQ(
        runtimeCMake.find("Runtime.SandboxDefaultPolicies.cppm"),
        std::string::npos);
    const auto runtimePrivate =
        runtimeCMake.find(
            "PRIVATEFILE_SETrender_extraction_impl");
    const auto retainedPolicy =
        runtimeCMake.find("Runtime.SandboxDefaultPolicies.cpp");
    ASSERT_NE(runtimePrivate, std::string::npos);
    ASSERT_NE(retainedPolicy, std::string::npos);
    EXPECT_GT(retainedPolicy, runtimePrivate);
    EXPECT_NE(
        policy.find(
            "moduleExtrinsic.Runtime.SandboxEditorFacades;"),
        std::string::npos);
    EXPECT_EQ(
        policy.find("Extrinsic.Runtime.SandboxDefaultPolicies"),
        std::string::npos);
    EXPECT_EQ(policy.find("Engine&"), std::string::npos);
    EXPECT_EQ(
        policy.find("importExtrinsic.Runtime.Engine"),
        std::string::npos);

    EXPECT_EQ(
        CountOccurrences(
            facade,
            "MakeSandboxDefaultImportAuthoringPolicies"),
        1u);
    EXPECT_EQ(
        CountOccurrences(
            facade,
            "MakeSandboxDefaultImportCompletedHandler"),
        1u);
    EXPECT_EQ(
        CountOccurrences(
            facade,
            "MakeSandboxDefaultDirectMeshPostProcessor"),
        1u);
    EXPECT_EQ(
        CountOccurrences(
            facade,
            "MakeSandboxDefaultFocusInputAction"),
        1u);

    EXPECT_EQ(
        app.find("RuntimeSandboxDefaultPolicyRegistration"),
        std::string::npos);
    EXPECT_EQ(
        app.find("RegisterSandboxDefaultRuntimePolicies"),
        std::string::npos);
    EXPECT_NE(
        app.find("structSandboxDefaultPolicyHandles"),
        std::string::npos);

    const auto appShutdownBegin =
        app.find(
            "voidOnShutdown(Runtime::Engine&engine)override{");
    const auto appShutdownEnd =
        app.find("private:", appShutdownBegin);
    ASSERT_NE(appShutdownBegin, std::string::npos);
    ASSERT_NE(appShutdownEnd, std::string::npos);
    const std::string_view appShutdown =
        std::string_view{app}.substr(
            appShutdownBegin,
            appShutdownEnd - appShutdownBegin);
    EXPECT_NE(
        appShutdown.find(
            "UninstallSandboxDefaultPolicies(m_DefaultPolicies);"),
        std::string_view::npos);

    const auto installBegin =
        app.find(
            "[[nodiscard]]boolInstallSandboxDefaultPolicies(");
    const auto installEnd =
        app.find("returntrue;}", installBegin);
    ASSERT_NE(installBegin, std::string::npos);
    ASSERT_NE(installEnd, std::string::npos);
    const std::string_view install =
        std::string_view{app}.substr(
            installBegin,
            installEnd + std::string_view{"returntrue;}"}.size() -
                installBegin);

    EXPECT_NE(
        install.find(
            "if(!handles.IsEmpty()||pipeline==nullptr||"
            "inputActions==nullptr)"),
        std::string_view::npos);

    const auto installMesh =
        install.find(
            "pipeline->RegisterImportEntityAuthoringPolicy");
    const auto installCompleted =
        install.find(
            "pipeline->RegisterImportCompletedHandler");
    const auto installPost =
        install.find(
            "pipeline->RegisterPostImportProcessor");
    const auto installFocus =
        install.find("inputActions->Register");
    ASSERT_NE(installMesh, std::string_view::npos);
    ASSERT_NE(installCompleted, std::string_view::npos);
    ASSERT_NE(installPost, std::string_view::npos);
    ASSERT_NE(installFocus, std::string_view::npos);
    EXPECT_LT(installMesh, installCompleted);
    EXPECT_LT(installCompleted, installPost);
    EXPECT_LT(installPost, installFocus);

    EXPECT_NE(
        install.find(
            "if(!handles.ImportAuthoring[index].IsValid())"),
        std::string_view::npos);
    EXPECT_NE(
        install.find("if(!handles.ImportCompleted.IsValid())"),
        std::string_view::npos);
    EXPECT_NE(
        install.find(
            "if(!handles.DirectMeshPostProcessor.IsValid())"),
        std::string_view::npos);
    EXPECT_NE(
        install.find("if(!focusAction.IsValid())"),
        std::string_view::npos);
    EXPECT_EQ(
        CountOccurrences(
            install,
            "UninstallSandboxDefaultPolicies(handles);"),
        4u);

    const auto uninstallBegin =
        app.find("voidUninstallSandboxDefaultPolicies(");
    const auto uninstallEnd =
        app.find(
            "[[nodiscard]]boolInstallSandboxDefaultPolicies(",
            uninstallBegin);
    ASSERT_NE(uninstallBegin, std::string::npos);
    ASSERT_NE(uninstallEnd, std::string::npos);
    const std::string_view uninstall =
        std::string_view{app}.substr(
            uninstallBegin,
            uninstallEnd - uninstallBegin);

    const auto uninstallFocus =
        uninstall.find("handles.InputActions->Unregister");
    const auto uninstallPost =
        uninstall.find(
            "handles.Pipeline->UnregisterPostImportProcessor");
    const auto uninstallCompleted =
        uninstall.find(
            "handles.Pipeline->UnregisterImportCompletedHandler");
    const auto uninstallAuthoring =
        uninstall.find(
            "handles.Pipeline->"
            "UnregisterImportEntityAuthoringPolicy");
    ASSERT_NE(uninstallFocus, std::string_view::npos);
    ASSERT_NE(uninstallPost, std::string_view::npos);
    ASSERT_NE(uninstallCompleted, std::string_view::npos);
    ASSERT_NE(uninstallAuthoring, std::string_view::npos);
    EXPECT_LT(uninstallFocus, uninstallPost);
    EXPECT_LT(uninstallPost, uninstallCompleted);
    EXPECT_LT(uninstallCompleted, uninstallAuthoring);
    EXPECT_NE(
        uninstall.find(
            "for(std::size_tindex=handles.ImportAuthoring.size();"
            "index>0u;--index)"),
        std::string_view::npos);

    const auto editorTarget =
        appCMake.find(
            "intrinsic_add_module_library(ExtrinsicSandboxEditor)");
    const auto executableTarget =
        appCMake.find("add_executable(ExtrinsicSandbox)");
    ASSERT_NE(editorTarget, std::string::npos);
    ASSERT_NE(executableTarget, std::string::npos);
    ASSERT_LT(editorTarget, executableTarget);
    const std::string_view editorSources =
        std::string_view{appCMake}.substr(
            editorTarget,
            executableTarget - editorTarget);
    EXPECT_NE(
        editorSources.find("Sandbox.cppm"),
        std::string_view::npos);
    EXPECT_NE(
        editorSources.find("Sandbox.cpp"),
        std::string_view::npos);

    const auto executableSourcesBegin =
        appCMake.find(
            "target_sources(ExtrinsicSandbox",
            executableTarget);
    const auto executableSourcesEnd =
        appCMake.find(
            "target_link_libraries(ExtrinsicSandbox",
            executableSourcesBegin);
    ASSERT_NE(executableSourcesBegin, std::string::npos);
    ASSERT_NE(executableSourcesEnd, std::string::npos);
    const std::string_view executableSources =
        std::string_view{appCMake}.substr(
            executableSourcesBegin,
            executableSourcesEnd - executableSourcesBegin);
    EXPECT_NE(
        executableSources.find("main.cpp"),
        std::string_view::npos);
    EXPECT_EQ(
        executableSources.find("Sandbox.cppm"),
        std::string_view::npos);
    EXPECT_EQ(
        executableSources.find("Sandbox.cpp"),
        std::string_view::npos);
}
