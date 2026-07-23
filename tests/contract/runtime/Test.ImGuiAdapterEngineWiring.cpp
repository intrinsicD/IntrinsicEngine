// RUNTIME-090 Slice B / GRAPHICS-079 Slice B — contract coverage that the
// runtime-side Dear ImGui adapter is wired into `Engine` and the produced
// overlay is handed to the renderer consumer: the engine constructs and
// initializes the adapter (after the Window and Renderer exist), owns the
// overlay system it produces into, exposes the editor hook, brackets each
// variable tick with the adapter so exactly one `ImGuiOverlayFrame` is produced
// per engine frame, and attaches the shared overlay to the renderer.
//
// The default CPU gate runs the GLFW platform backend with no display, where
// `Engine::Run()` executes zero frames (the window reports `ShouldClose()`).
// The static-wiring assertions therefore run everywhere; the live per-frame
// assertions are gated on a real window being available (backend-agnostic
// `GetWindow().ShouldClose()` probe) and are exercised under a virtual display
// (e.g. `xvfb-run`) or any display-providing lane, mirroring the window-loop
// skip pattern used by the graphics GPU-smoke integration tests. The adapter's
// one-frame-per-BeginFrame/EndFrame guarantee itself is pinned by the Slice A
// `Test.ImGuiAdapter.cpp` contract. `imgui.h` is included only to synthesize a
// real panel draw in the editor-hook case.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;

using Extrinsic::Runtime::Engine;

namespace Core = Extrinsic::Core;
namespace Graphics = Extrinsic::Graphics;
namespace Platform = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    struct GLFWwindow;
    extern "C" void glfwSetWindowShouldClose(GLFWwindow*, int) __attribute__((weak));

    [[nodiscard]] bool RequestNativeWindowClose(Platform::IWindow& window)
    {
        if (window.GetNativeHandle() == nullptr || glfwSetWindowShouldClose == nullptr)
            return false;

        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(window.GetNativeHandle()), 1);
        return true;
    }

    // Stub application that drives a bounded run: it counts variable ticks and
    // calls `Engine::RequestExit()` once `TargetFrames` ticks have run, so
    // `Engine::Run()` executes exactly `TargetFrames` full frames. The editor
    // draw is registered by the test (not the app) so the adapter's editor-hook
    // cadence is what is under test.
    class BoundedRunApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit BoundedRunApplication(const std::uint32_t targetFrames)
            : m_TargetFrames(targetFrames)
        {
        }

        void Resolve() override {}
        void Simulate(double /*fixedDt*/) override {}
        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
            ++m_VariableTicks;
            if (m_VariableTicks >= m_TargetFrames)
                engine.RequestExit();
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_TargetFrames{0u};
        std::uint32_t m_VariableTicks{0u};
    };

    [[nodiscard]] bool IsWorkerRunningStatus(
        const Runtime::DerivedJobStatus status) noexcept
    {
        return status == Runtime::DerivedJobStatus::Running;
    }

    class SlowDerivedJobApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        static constexpr std::uint32_t kMaxFrames = 512u;
        static constexpr auto kWorkerSleep = std::chrono::milliseconds(250);
        static constexpr auto kVariableTickDelay = std::chrono::milliseconds(1);

        void Resolve() override
        {
            auto& engine = Kernel();
            Jobs         = engine.Services().Find<Runtime::DerivedJobRegistry>();
            ASSERT_NE(Jobs, nullptr);

            Runtime::DerivedJobDesc desc{
                .Key = Runtime::DerivedJobKey{
                    .EntityId = 141u,
                    .Domain = Runtime::ProgressiveGeometryDomain::Point,
                    .OutputSemantic =
                        Runtime::ProgressiveSlotSemantic::PointColor,
                    .OutputName = "runtime_141_slow_job_probe",
                },
                .Name = "RUNTIME-141 slow editor method job",
                .Scope = engine.ActiveWorld(),
                .Execute =
                    [this]() -> Runtime::DerivedJobWorkerResult
                    {
                        WorkerRuns.fetch_add(1u, std::memory_order_relaxed);
                        std::this_thread::sleep_for(kWorkerSleep);
                        return Runtime::DerivedJobOutput{
                            .PayloadToken = 141u,
                            .NormalizedProgress = 1.0f,
                            .ProgressDeterminate = true,
                            .Diagnostic = "slow probe complete",
                        };
                    },
                .ApplyOnMainThread =
                    [this](Runtime::DerivedJobApplyContext& context)
                        -> Core::Result
                    {
                        EXPECT_EQ(context.Output.PayloadToken, 141u);
                        ApplyRuns.fetch_add(1u, std::memory_order_relaxed);
                        return Core::Ok();
                    },
            };
            Handle = Jobs->Submit(std::move(desc));
        }

        void Simulate(double) override {}

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++VariableTicks;

            if (PreviousTickEnteredRenderWithRunningJob)
            {
                const Runtime::RuntimeFramePacingDiagnostics& pacing =
                    engine.GetLastFramePacingDiagnostics();
                if (pacing.Valid &&
                    pacing.RendererBeganFrame &&
                    pacing.RendererCompletedFrame)
                {
                    ObservedRenderAdvanceWhileWorkerRunning = true;
                }
            }

            const Runtime::DerivedJobStatus status = CurrentStatus(engine);
            const bool workerRunning = IsWorkerRunningStatus(status);
            if (workerRunning)
            {
                ++VariableTicksWhileWorkerRunning;
            }
            PreviousTickEnteredRenderWithRunningJob = workerRunning;

            if (ApplyRuns.load(std::memory_order_relaxed) > 0u ||
                VariableTicks >= kMaxFrames)
            {
                engine.RequestExit();
                return;
            }

            std::this_thread::sleep_for(kVariableTickDelay);
        }

        void Shutdown() override {}

        Runtime::DerivedJobHandle Handle{};
        Runtime::DerivedJobRegistry* Jobs{};
        std::atomic<std::uint32_t> WorkerRuns{0u};
        std::atomic<std::uint32_t> ApplyRuns{0u};
        std::uint32_t VariableTicks{0u};
        std::uint32_t VariableTicksWhileWorkerRunning{0u};
        bool ObservedRenderAdvanceWhileWorkerRunning{false};

    private:
        [[nodiscard]] Runtime::DerivedJobStatus CurrentStatus(
            Engine&) const
        {
            if (Jobs == nullptr)
                return Runtime::DerivedJobStatus::Cancelled;
            const Runtime::DerivedJobQueueSnapshot snapshot =
                Jobs->SnapshotAll();
            for (const Runtime::DerivedJobSnapshot& entry : snapshot.Entries)
            {
                if (entry.Handle == Handle)
                {
                    return entry.Status;
                }
            }
            return Runtime::DerivedJobStatus::Cancelled;
        }

        bool PreviousTickEnteredRenderWithRunningJob{false};
    };

    class RecordingCameraController final : public Runtime::ICameraController
    {
    public:
        void Seed(const Graphics::CameraViewInput& seed) noexcept override
        {
            if (seed.Valid)
                m_View = seed;
        }

        void Focus(Runtime::CameraFocusTarget target) noexcept override
        {
            m_View.Position = target.Center + glm::vec3{0.0f, 0.0f, 3.0f};
        }

        void Update(const Platform::Input::Context& input,
                    double /*deltaSeconds*/) noexcept override
        {
            ++Updates;
            if (input.IsKeyPressed(Platform::Input::Key::W))
                ++KeyboardUpdates;
            if (input.IsMouseButtonJustPressed(0))
                ++MouseClickUpdates;
        }

        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D /*viewport*/)
            const noexcept override
        {
            return m_View;
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::Fly;
        }

        std::uint32_t Updates{0u};
        std::uint32_t KeyboardUpdates{0u};
        std::uint32_t MouseClickUpdates{0u};

    private:
        Graphics::CameraViewInput m_View{Runtime::DefaultCameraControllerSeed()};
    };

    class UiCapturedInputApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine    = Kernel();
            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            auto* cameraControllers =
                engine.Services().Find<Runtime::CameraControllerRegistry>();
            ASSERT_NE(cameraControllers, nullptr);
            cameraControllers->Register(Runtime::CameraControllerSlot::Main,
                                        std::move(controller));
        }

        void Simulate(double /*fixedDt*/) override {}

        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
            ++VariableTicks;
            if (VariableTicks == 2u)
            {
                const auto& window = engine.GetWindow();
                auto& input = const_cast<Platform::Input::Context&>(
                    window.GetInput());
                input.SetKeyState(Platform::Input::Key::W, true);
                input.SetKeyState(Platform::Input::Key::LeftShift, true);
                input.SetMousePosition(32.0f, 48.0f);
                input.SetMouseButtonState(0, true);
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        RecordingCameraController* Controller{nullptr};
        std::uint32_t              VariableTicks{0u};
    };

    class CloseAfterInteractiveInputApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine    = Kernel();
            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            auto* cameraControllers =
                engine.Services().Find<Runtime::CameraControllerRegistry>();
            ASSERT_NE(cameraControllers, nullptr);
            cameraControllers->Register(Runtime::CameraControllerSlot::Main,
                                        std::move(controller));
        }

        void Simulate(double /*fixedDt*/) override {}

        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
            ++VariableTicks;

            const auto& window = engine.GetWindow();
            auto& input = const_cast<Platform::Input::Context&>(window.GetInput());
            input.SetKeyState(Platform::Input::Key::W, true);
            input.SetMousePosition(32.0f, 48.0f);
            input.SetMouseButtonState(0, true);
            input.SetMouseButtonState(1, true);

            NativeCloseRequested = RequestNativeWindowClose(engine.GetWindow());
            if (!NativeCloseRequested)
                engine.RequestExit();
        }

        void Shutdown() override {}

        RecordingCameraController* Controller{nullptr};
        std::uint32_t              VariableTicks{0u};
        bool                       NativeCloseRequested{false};
    };

    // Camera and reference scene are disabled so the bounded run exercises the
    // minimal frame path (no controller creation, no scene population). The
    // default 1920x1080 window is never minimized, so under a live window every
    // RunFrame reaches the variable tick the adapter brackets.
    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled         = false;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig InputRoutingConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = true;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig NullInputRoutingConfig()
    {
        Extrinsic::Core::Config::EngineConfig config = InputRoutingConfig();
        config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig NullWindowHeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config = HeadlessConfig();
        config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Extrinsic::Graphics::RenderGraphFrameStats& stats,
        const std::string& name)
    {
        for (const auto& pass : stats.CommandRecords.Passes)
        {
            if (pass.Name == name)
                return &pass;
        }
        return nullptr;
    }
}

// Static wiring (runs in every environment, including displayless CI): the
// adapter exists and is initialized once the engine is initialized, the
// engine-owned overlay system is live, and no overlay frame has been produced
// before the loop runs.
TEST(ImGuiAdapterEngineWiring, AdapterInitializedAfterEngineInitialize)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<BoundedRunApplication>(1u));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    const auto& diag = editorUi->GetDiagnostics();
    EXPECT_TRUE(editorUi->IsOperational());
    EXPECT_TRUE(diag.Initialized);
    EXPECT_EQ(diag.FramesProduced, 0u);
    EXPECT_TRUE(engine.GetRenderer().HasImGuiOverlaySystem());

    engine.Shutdown();
}

// A bounded `Engine::Run()` produces exactly one ImGuiOverlayFrame per engine
// frame (BeginFrame/EndFrame bracket each variable tick).
TEST(ImGuiAdapterEngineWiring, RunProducesOneOverlayFramePerEngineFrame)
{
    constexpr std::uint32_t kFrames = 3u;
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<BoundedRunApplication>(kFrames));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();
    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);

    if (engine.GetWindow().ShouldClose())
    {
        // No live window backend (e.g. headless CI with no display):
        // Engine::Run() would execute zero frames. The static wiring is still
        // asserted; the per-frame loop assertion needs a real window.
        EXPECT_TRUE(editorUi->IsOperational());
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame loop coverage "
                        "requires a display";
    }

    engine.Run();

    EXPECT_EQ(editorUi->GetDiagnostics().FramesProduced, kFrames);

    engine.Shutdown();
}

// The editor hook registered through the engine is invoked once per engine
// frame, and a panel draw issued by the hook flows into the produced overlay
// frame's draw lists.
TEST(ImGuiAdapterEngineWiring, EditorHookInvokedOncePerFrameAndProducesDrawLists)
{
    constexpr std::uint32_t kFrames = 3u;
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<BoundedRunApplication>(kFrames));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    std::uint32_t editorCalls = 0u;
    const Runtime::EditorUiFrameContributionHandle contribution =
        editorUi->RegisterFrameContribution(
        [&editorCalls]
        {
            ++editorCalls;
            // Explicit pos/size so the window is not auto-fitting (hidden on its
            // first appearing frame); kFrames >= 2 guarantees a measured frame.
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f));
            ImGui::Begin("RUNTIME-090 Engine Panel");
            ImGui::Text("hello engine imgui");
            ImGui::End();
        });
    ASSERT_TRUE(contribution.IsValid());

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame editor-hook "
                        "coverage requires a display";
    }

    engine.Run();

    const auto& diag = editorUi->GetDiagnostics();
    EXPECT_EQ(editorCalls, kFrames);
    EXPECT_EQ(diag.EditorCallbackInvocations, kFrames);
    EXPECT_EQ(diag.FramesProduced, kFrames);
    EXPECT_GE(diag.LastDrawListCount, 1u);
    EXPECT_GT(diag.LastVertexCount, 0u);
    EXPECT_GT(diag.LastIndexCount, 0u);
    EXPECT_FALSE(diag.LastFrameUsedUserTexture); // a text panel only uses the font atlas

    // GRAPHICS-079 Slice B: Engine hands the same overlay system to the
    // renderer consumer. On the Null device the explicit ImGui route is present
    // and fail-closed as SkippedNonOperational; the direct attachment observer
    // above catches a missing producer↔consumer handoff before later slices
    // make the route operational.
    const auto& stats = engine.GetRenderer().GetLastRenderGraphStats();
    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring, FramePacingDiagnosticsPopulateOnNullBackend)
{
    constexpr std::uint32_t kFrames = 2u;
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(),
                                               std::make_unique<BoundedRunApplication>(kFrames));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    std::uint32_t editorCalls = 0u;
    const Runtime::EditorUiFrameContributionHandle contribution =
        editorUi->RegisterFrameContribution(
        [&editorCalls]
        {
            ++editorCalls;
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f));
            ImGui::Begin("UI-030 Frame Pacing Panel");
            ImGui::Text("frame pacing diagnostics");
            ImGui::End();
        });
    ASSERT_TRUE(contribution.IsValid());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    const Runtime::RuntimeFramePacingDiagnostics& pacing =
        engine.GetLastFramePacingDiagnostics();
    const auto& imgui = editorUi->GetDiagnostics();
    const Graphics::RenderGraphFrameStats& graph =
        engine.GetRenderer().GetLastRenderGraphStats();

    EXPECT_EQ(editorCalls, kFrames);
    EXPECT_EQ(imgui.FramesProduced, kFrames);
    EXPECT_TRUE(pacing.Valid);
    EXPECT_TRUE(pacing.PlatformContinueFrame);
    EXPECT_TRUE(pacing.RendererBeganFrame);
    EXPECT_TRUE(pacing.RendererCompletedFrame);
    EXPECT_EQ(pacing.FrameIndex, kFrames - 1u);
    EXPECT_EQ(pacing.ImGuiEditorCallbackMicros,
              imgui.LastEditorCallbackMicros);
    EXPECT_EQ(pacing.ImGuiDrawDataCopyMicros,
              imgui.LastDrawDataCopyMicros);
    EXPECT_EQ(pacing.ImGuiDrawListCount, imgui.LastDrawListCount);
    EXPECT_EQ(pacing.ImGuiVertexCount, imgui.LastVertexCount);
    EXPECT_EQ(pacing.ImGuiIndexCount, imgui.LastIndexCount);
    EXPECT_EQ(pacing.ImGuiCommandCount, imgui.LastCommandCount);
    EXPECT_EQ(pacing.ImGuiFontAtlasCopyCount, imgui.FontAtlasCopyCount);
    EXPECT_EQ(pacing.ImGuiFontAtlasReuseCount, imgui.FontAtlasReuseCount);
    EXPECT_EQ(pacing.ImGuiFontAtlasCopied, imgui.LastFrameFontAtlasCopied);
    EXPECT_EQ(pacing.ImGuiFrameUsedUserTexture,
              imgui.LastFrameUsedUserTexture);
    EXPECT_EQ(pacing.ImGuiFontAtlasByteCount, imgui.LastFontAtlasByteCount);
    EXPECT_EQ(pacing.ImGuiFontAtlasCopyBytes,
              imgui.LastFrameFontAtlasCopyBytes);
    EXPECT_EQ(pacing.ImGuiVertexCopyBytes,
              imgui.LastFrameVertexCopyBytes);
    EXPECT_EQ(pacing.ImGuiIndexCopyBytes,
              imgui.LastFrameIndexCopyBytes);
    EXPECT_EQ(pacing.ImGuiCommandCopyBytes,
              imgui.LastFrameCommandCopyBytes);
    EXPECT_EQ(pacing.ImGuiOverlayCopyBytes,
              imgui.LastFrameOverlayCopyBytes);
    EXPECT_GE(pacing.ImGuiEndMicros, pacing.ImGuiEditorCallbackMicros);
    EXPECT_GE(pacing.ImGuiEndMicros, pacing.ImGuiDrawDataCopyMicros);
    EXPECT_GE(pacing.ImGuiDrawListCount, 1u);
    EXPECT_GT(pacing.ImGuiVertexCount, 0u);
    EXPECT_GT(pacing.ImGuiIndexCount, 0u);
    EXPECT_GE(pacing.ImGuiCommandCount, 1u);
    EXPECT_EQ(pacing.ImGuiVertexCopyBytes,
              static_cast<std::uint64_t>(pacing.ImGuiVertexCount) *
                  sizeof(Graphics::ImGuiOverlayVertex));
    EXPECT_EQ(pacing.ImGuiIndexCopyBytes,
              static_cast<std::uint64_t>(pacing.ImGuiIndexCount) *
                  sizeof(std::uint32_t));
    EXPECT_EQ(pacing.ImGuiCommandCopyBytes,
              static_cast<std::uint64_t>(pacing.ImGuiCommandCount) *
                  sizeof(Graphics::ImGuiOverlayDrawCommand));
    EXPECT_EQ(pacing.ImGuiOverlayCopyBytes,
              pacing.ImGuiFontAtlasCopyBytes +
                  pacing.ImGuiVertexCopyBytes +
                  pacing.ImGuiIndexCopyBytes +
                  pacing.ImGuiCommandCopyBytes);
    EXPECT_GT(pacing.ImGuiOverlayCopyBytes, 0u);
    EXPECT_EQ(pacing.RenderGraphCompileMicros, graph.Compile.TimeMicros);
    EXPECT_EQ(pacing.RenderGraphExecuteMicros, graph.Execute.TimeMicros);
    EXPECT_GT(imgui.LastFrameOverlayCopyBytes, 0u);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring,
     EditorCallbackStaysBoundedAndRenderAdvancesWhileDerivedJobRuns)
{
    auto app = std::make_unique<SlowDerivedJobApplication>();
    SlowDerivedJobApplication* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    std::uint32_t callbacksWhileWorkerRunning = 0u;
    std::uint64_t maxCallbackMicros = 0u;
    const Runtime::EditorUiFrameContributionHandle contribution =
        editorUi->RegisterFrameContribution(
        [appPtr, &callbacksWhileWorkerRunning, &maxCallbackMicros]
        {
            const auto begin = std::chrono::steady_clock::now();
            if (appPtr->WorkerRuns.load(std::memory_order_relaxed) > 0u &&
                appPtr->ApplyRuns.load(std::memory_order_relaxed) == 0u)
            {
                ++callbacksWhileWorkerRunning;
            }

            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(240.0f, 96.0f));
            ImGui::Begin("RUNTIME-141 Async Job Probe");
            ImGui::TextUnformatted("async method job pending");
            ImGui::End();

            const auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - begin);
            maxCallbackMicros = std::max<std::uint64_t>(
                maxCallbackMicros,
                static_cast<std::uint64_t>(elapsed.count()));
        });
    ASSERT_TRUE(contribution.IsValid());

    ASSERT_TRUE(appPtr->Handle.IsValid());
    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    ASSERT_NE(appPtr->Jobs, nullptr);
    const Runtime::DerivedJobQueueSnapshot jobs = appPtr->Jobs->SnapshotAll();
    ASSERT_EQ(jobs.Entries.size(), 1u);
    EXPECT_EQ(jobs.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    EXPECT_EQ(appPtr->WorkerRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(appPtr->ApplyRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_GT(appPtr->VariableTicks, 1u);
    EXPECT_GE(appPtr->VariableTicksWhileWorkerRunning, 1u);
    EXPECT_TRUE(appPtr->ObservedRenderAdvanceWhileWorkerRunning);
    EXPECT_GE(callbacksWhileWorkerRunning, 1u);
    EXPECT_LT(maxCallbackMicros, 100'000u);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring, UiCaptureSuppressesRuntimeInputConsumers)
{
    auto app = std::make_unique<UiCapturedInputApplication>();
    auto* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(NullInputRoutingConfig(), std::move(app));
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.Initialize();

    Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    std::uint32_t editorFrames = 0u;
    const Runtime::EditorUiFrameContributionHandle contribution =
        editorUi->RegisterFrameContribution(
        [&editorFrames]
        {
            ++editorFrames;
            if (editorFrames == 1u)
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                ImGui::SetNextFrameWantCaptureKeyboard(true);
            }
        });
    ASSERT_TRUE(contribution.IsValid());

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; input-routing coverage "
                        "requires a live window";
    }

    engine.Run();

    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_EQ(appPtr->VariableTicks, 2u);
    EXPECT_EQ(editorFrames, 2u);
    EXPECT_EQ(editorUi->GetDiagnostics().CaptureSnapshots, 2u);

    EXPECT_EQ(appPtr->Controller->Updates, 1u);
    EXPECT_EQ(appPtr->Controller->KeyboardUpdates, 0u);
    EXPECT_EQ(appPtr->Controller->MouseClickUpdates, 0u);

    const auto selectionDiagnostics =
        engine.Services()
            .Find<Runtime::SelectionController>()
            ->GetDiagnostics();
    EXPECT_EQ(selectionDiagnostics.ClickRequestsSubmitted, 0u);
    EXPECT_EQ(selectionDiagnostics.PicksDrained, 0u);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::SelectionController>()
            ->InFlightPickCount(),
        0u);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::SceneInteractionModule>()
            ->Interaction()
            .ModifierMask(),
        0u);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring, RunNormalizesNativeCloseBeforeFirstFrame)
{
    auto app = std::make_unique<UiCapturedInputApplication>();
    auto* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(InputRoutingConfig(), std::move(app));
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.Initialize();

    if (!RequestNativeWindowClose(engine.GetWindow()))
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; native close-state coverage "
                        "requires a live GLFW window";
    }

    engine.Run();

    EXPECT_TRUE(engine.GetWindow().ShouldClose());
    EXPECT_FALSE(engine.IsRunning());
    EXPECT_EQ(appPtr->VariableTicks, 0u);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring, RunNormalizesNativeCloseAfterInteractiveInput)
{
    auto app = std::make_unique<CloseAfterInteractiveInputApplication>();
    auto* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(InputRoutingConfig(), std::move(app));
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    std::uint32_t editorFrames = 0u;
    const Runtime::EditorUiFrameContributionHandle contribution =
        editorUi->RegisterFrameContribution(
        [&editorFrames]
        {
            ++editorFrames;
            if (editorFrames == 1u)
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                ImGui::SetNextFrameWantCaptureKeyboard(true);
            }
        });
    ASSERT_TRUE(contribution.IsValid());

    if (engine.GetWindow().ShouldClose() ||
        engine.GetWindow().GetNativeHandle() == nullptr ||
        glfwSetWindowShouldClose == nullptr)
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; native close-state coverage "
                        "requires a live GLFW window";
    }

    engine.Run();

    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_EQ(appPtr->VariableTicks, 1u);
    EXPECT_TRUE(appPtr->NativeCloseRequested);
    EXPECT_EQ(editorFrames, 1u);
    EXPECT_TRUE(engine.GetWindow().ShouldClose());
    EXPECT_FALSE(engine.IsRunning());

    engine.Shutdown();
}
