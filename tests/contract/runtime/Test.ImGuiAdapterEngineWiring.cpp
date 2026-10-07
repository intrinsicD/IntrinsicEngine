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
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <imgui.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
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
        const Runtime::JobState status) noexcept
    {
        return status == Runtime::JobState::Running;
    }

    class SlowJobApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        static constexpr std::uint32_t kMaxFrames = 512u;
        static constexpr auto kWorkerSleep = std::chrono::milliseconds(250);
        static constexpr auto kVariableTickDelay = std::chrono::milliseconds(1);

        void Resolve() override
        {
            auto& engine = Kernel();
            Jobs = engine.Services().Find<Runtime::JobService>();
            ASSERT_NE(Jobs, nullptr);

            Runtime::JobDesc desc{
                .DebugName = "RUNTIME-141 slow editor method job",
                .Scope = engine.ActiveWorld(),
                .Kind = Runtime::RuntimeTaskKinds::GeometryProcess,
                .Work =
                    [this](const Runtime::JobCancellation&)
                        -> Runtime::JobResultEnvelope
                    {
                        WorkerRuns.fetch_add(1u, std::memory_order_relaxed);
                        std::this_thread::sleep_for(kWorkerSleep);
                        return Runtime::JobResultEnvelope::Make<std::uint64_t>(
                            141u);
                    },
                .PublishCompletion =
                    [this](Runtime::KernelEventBus&,
                           const Runtime::JobResultEnvelope& result) -> bool
                    {
                        const std::uint64_t* payload =
                            result.TryGet<std::uint64_t>();
                        EXPECT_NE(payload, nullptr);
                        if (payload == nullptr)
                            return false;
                        EXPECT_EQ(*payload, 141u);
                        ApplyRuns.fetch_add(1u, std::memory_order_relaxed);
                        return true;
                    },
            };
            Handle = Jobs->Submit(std::move(desc));
        }


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

            const Runtime::JobState status = CurrentStatus();
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

        Runtime::JobToken Handle{};
        Runtime::JobService* Jobs{};
        std::atomic<std::uint32_t> WorkerRuns{0u};
        std::atomic<std::uint32_t> ApplyRuns{0u};
        std::uint32_t VariableTicks{0u};
        std::uint32_t VariableTicksWhileWorkerRunning{0u};
        bool ObservedRenderAdvanceWhileWorkerRunning{false};

    private:
        [[nodiscard]] Runtime::JobState CurrentStatus() const
        {
            if (Jobs == nullptr)
                return Runtime::JobState::Invalid;
            return Jobs->GetState(Handle);
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

    class RecordingCameraApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            Kernel().Services().Find<Runtime::CameraControllerRegistry>()->Register(
                Runtime::CameraControllerSlot::Main, std::move(controller));
        }

        RecordingCameraController* Controller{nullptr};
    };

    // UI-078: scripted frontend stand-in, sorted after every production module.
    // `UiBuild` gets the 1-based frame number; an `Idle` script, when set before
    // Initialize(), also registers the minimized-frame hook.
    class GizmoFrontendProbe final : public Runtime::IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "zz.Test.GizmoFrontendProbe";
        }
        [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup& setup) override
        {
            if (Core::Result result = setup.RegisterFrameHook(
                    Runtime::FramePhase::UiBuild,
                    [this](Runtime::RuntimeFrameHookContext&) { UiBuild(++Frame); });
                !result.has_value())
            {
                return result;
            }
            if (Idle)
            {
                if (Core::Result result = setup.RegisterFrameHook(
                        Runtime::FramePhase::Idle,
                        [this](Runtime::RuntimeFrameHookContext&) { Idle(); });
                    !result.has_value())
                {
                    return result;
                }
            }
            return setup.RegisterFrameHook(
                Runtime::FramePhase::BeforeExtraction,
                [this](Runtime::RuntimeFrameHookContext&) { BeforeExtraction(Frame); });
        }
        [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }
        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}

        std::function<void(std::uint32_t)> UiBuild{[](std::uint32_t) {}};
        std::function<void(std::uint32_t)> BeforeExtraction{[](std::uint32_t) {}};
        std::function<void()> Idle{};
        std::uint32_t Frame{0u};
    };

    [[nodiscard]] Platform::Input::Context& MutableInput(Engine& engine)
    {
        const Platform::IWindow& window = engine.GetWindow();
        return const_cast<Platform::Input::Context&>(window.GetInput());
    }

    [[nodiscard]] Extrinsic::ECS::Scene::Registry& ActiveScene(Engine& engine)
    {
        return *engine.Worlds().Get(engine.ActiveWorld());
    }

    // A frontend-style matrix session on `entity` with an accepted translation
    // preview of `dx` along world X.
    void BeginTranslatePreview(Runtime::GizmoInteraction& gizmo,
                               Extrinsic::ECS::Scene::Registry& scene,
                               const Extrinsic::ECS::EntityHandle entity,
                               const float dx)
    {
        const Extrinsic::ECS::EntityHandle selected[] = {entity};
        ASSERT_TRUE(gizmo.Begin(scene, selected, Runtime::GizmoMode::Translate,
                                Runtime::GizmoOrientation::Global,
                                Runtime::GizmoPivotMode::WorldOrigins)
                        .Succeeded());
        ASSERT_TRUE(gizmo.Preview(scene,
                                  glm::translate(glm::mat4{1.0f}, glm::vec3{dx, 0.0f, 0.0f}) *
                                      gizmo.SessionFrame().Matrix)
                        .Succeeded());
    }

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
     EditorCallbackStaysBoundedAndRenderAdvancesWhileJobRuns)
{
    auto app = std::make_unique<SlowJobApplication>();
    SlowJobApplication* appPtr = app.get();
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
    EXPECT_TRUE(appPtr->Handle.IsValid());
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

// UI-078 slice 2: a frontend's viewport claim, merged after the adapter
// capture, blocks camera controller updates and new pick requests while the
// gizmo is hovered (frame 1) or dragged (frames 2-3), and suppresses keyboard
// input actions such as F. The claim does not end its own session: the ray
// driver neither cancels it on capture nor commits it on mouse release.
// Without a claim (frame 4) all consumers run again.
TEST(ImGuiAdapterEngineWiring, ViewportClaimBlocksCameraAndPickWithoutEndingItsSession)
{
    auto app = std::make_unique<RecordingCameraApplication>();
    auto* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(NullInputRoutingConfig(), std::move(app));
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    auto& probe = engine.EmplaceModule<GizmoFrontendProbe>();
    engine.Initialize();

    Runtime::EditorUiHost& host = *engine.Services().Find<Runtime::EditorUiHost>();
    Runtime::GizmoInteraction& gizmo =
        engine.Services().Find<Runtime::SceneInteractionModule>()->Interaction();
    auto& inputActions = *engine.Services().Find<Runtime::RuntimeInputActionRegistry>();
    std::uint32_t focusKeyRuns = 0u;
    const Runtime::RuntimeInputActionHandle focusKey = inputActions.Register({
        .DebugName = "Test.ClaimedFocusKey",
        .Binding = {.KeyCode = 'F'},
        .Execute = [&focusKeyRuns](const Runtime::RuntimeInputActionContext&,
                                   Runtime::RuntimeInputActionServices&)
        {
            ++focusKeyRuns;
            return Core::Ok();
        },
    });
    ASSERT_TRUE(focusKey.IsValid());
    Extrinsic::ECS::EntityHandle entity{};
    std::vector<bool> dragging{};
    std::vector<bool> claimed{};
    float previewX = 0.0f;
    probe.UiBuild = [&](const std::uint32_t frame)
    {
        auto& input = MutableInput(engine);
        auto& scene = ActiveScene(engine);
        if (frame == 1u)
        {
            entity = Extrinsic::ECS::Scene::CreateDefault(scene, "Gizmo target");
            input.SetMousePosition(32.0f, 48.0f);
            input.SetMouseButtonState(0, true);
            input.SetKeyState(Platform::Input::Key::W, true);
            input.SetKeyState(Platform::Input::Key::F, true);
        }
        if (frame == 2u)
        {
            BeginTranslatePreview(gizmo, scene, entity, 1.0f);
            input.SetKeyState(Platform::Input::Key::F, false);
        }
        if (frame == 3u)
        {
            ASSERT_TRUE(gizmo.Preview(scene,
                glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}) *
                    gizmo.SessionFrame().Matrix).Succeeded());
            input.SetMouseButtonState(0, false);
        }
        if (frame == 4u)
        {
            ASSERT_TRUE(gizmo.DragCancel(scene).Succeeded());
            input.SetMouseButtonState(0, true);
            input.SetKeyState(Platform::Input::Key::F, true);
            engine.RequestExit();
            return;
        }
        host.RequestViewportInput({.CaptureViewportInput = true});
    };
    probe.BeforeExtraction = [&](const std::uint32_t frame)
    {
        dragging.push_back(gizmo.IsDragging());
        claimed.push_back(host.GetDiagnostics().CapturesViewportInput);
        if (frame == 3u)
            previewX = ActiveScene(engine).Raw()
                .get<Extrinsic::ECS::Components::Transform::Component>(entity).Position.x;
    };

    ASSERT_FALSE(engine.GetWindow().ShouldClose());
    engine.Run();

    EXPECT_EQ(claimed, (std::vector<bool>{true, true, true, false}));
    EXPECT_EQ(dragging, (std::vector<bool>{false, true, true, false}));
    EXPECT_EQ(previewX, 2.0f);
    EXPECT_EQ(gizmo.Diagnostics().DragsCommitted, 0u);
    EXPECT_EQ(gizmo.Diagnostics().DragsCancelled, 1u);
    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_EQ(appPtr->Controller->Updates, 1u);
    EXPECT_EQ(appPtr->Controller->MouseClickUpdates, 1u);
    EXPECT_EQ(engine.Services().Find<Runtime::SelectionController>()
                  ->GetDiagnostics().ClickRequestsSubmitted,
              1u);
    // The claim also captures the keyboard: F pressed in frame 1 must not run,
    // the fresh press in unclaimed frame 4 runs exactly once.
    EXPECT_EQ(focusKeyRuns, 1u);

    inputActions.Unregister(focusKey);
    engine.Shutdown();
}

namespace
{
    enum class HideRoute
    {
        HostCommand,
        Shortcut,
    };

    // Frame 1 previews +2 along X under a claim; frame 2 hides the UI. The
    // drag must end in frame 2 with the start TRS restored, no history entry,
    // and the restored world matrix already flushed for that frame's
    // extraction.
    void ExpectHideEndsDragBeforeTheTransformFlush(const HideRoute route)
    {
        Intrinsic::Tests::RuntimeTestKernel engine(
            NullWindowHeadlessConfig(), std::make_unique<BoundedRunApplication>(2u));
        engine.EmplaceModule<Runtime::EditorUiModule>();
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.EmplaceModule<Runtime::SceneInteractionModule>();
        auto& probe = engine.EmplaceModule<GizmoFrontendProbe>();
        engine.Initialize();

        Runtime::EditorUiHost& host = *engine.Services().Find<Runtime::EditorUiHost>();
        Runtime::EditorCommandHistory& history =
            *engine.Services().Find<Runtime::EditorCommandHistory>();
        Runtime::GizmoInteraction& gizmo =
            engine.Services().Find<Runtime::SceneInteractionModule>()->Interaction();
        auto& scene = ActiveScene(engine);
        const Extrinsic::ECS::EntityHandle entity =
            Extrinsic::ECS::Scene::CreateDefault(scene, "Gizmo target");
        std::vector<float> extractedX{};
        std::vector<bool> dragging{};
        probe.UiBuild = [&](const std::uint32_t frame)
        {
            if (frame == 1u)
            {
                BeginTranslatePreview(gizmo, scene, entity, 2.0f);
                host.RequestViewportInput({.CaptureViewportInput = true});
                if (route == HideRoute::Shortcut)
                {
                    static_cast<Platform::Backends::Null::NullWindow&>(engine.GetWindow())
                        .QueueKey(Platform::Input::Key::G, true);
                }
                return;
            }
            host.RequestViewportInput({.CaptureViewportInput = true});
            if (route == HideRoute::HostCommand)
            {
                (void)host.ApplyVisibilityCommand(
                    {Runtime::EditorUiVisibilityCommandKind::Hide});
            }
        };
        probe.BeforeExtraction = [&](std::uint32_t)
        {
            dragging.push_back(gizmo.IsDragging());
            extractedX.push_back(
                scene.Raw()
                    .get<Extrinsic::ECS::Components::Transform::WorldMatrix>(entity)
                    .Matrix[3][0]);
        };

        engine.Run();

        EXPECT_FALSE(host.IsVisible());
        EXPECT_EQ(dragging, (std::vector<bool>{true, false}));
        EXPECT_EQ(extractedX, (std::vector<float>{2.0f, 0.0f}));
        EXPECT_EQ(scene.Raw().get<Extrinsic::ECS::Components::Transform::Component>(entity)
                      .Position,
                  glm::vec3{0.0f});
        EXPECT_EQ(history.UndoCount(), 0u);
        EXPECT_EQ(gizmo.Diagnostics().DragsCommitted, 0u);
        EXPECT_EQ(gizmo.Diagnostics().DragsCancelled, 1u);
        engine.Shutdown();
    }
}

TEST(ImGuiAdapterEngineWiring, HostHideEndsDragBeforeTheTransformFlush)
{
    ExpectHideEndsDragBeforeTheTransformFlush(HideRoute::HostCommand);
}

TEST(ImGuiAdapterEngineWiring, ShortcutHideEndsDragBeforeTheTransformFlush)
{
    ExpectHideEndsDragBeforeTheTransformFlush(HideRoute::Shortcut);
}

// UI-078 slice 2: Engine republishes native focus events on the kernel bus.
// A loss while the window is minimized reaches SceneInteractionModule on the
// minimized path, before any restore, and cancels without history.
TEST(ImGuiAdapterEngineWiring, FocusLossWhileMinimizedCancelsDragWithoutHistory)
{
    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(), std::make_unique<BoundedRunApplication>(1000u));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    auto& probe = engine.EmplaceModule<GizmoFrontendProbe>();
    bool draggingWhenIdle = true;
    float idleX = -1.0f;
    Extrinsic::ECS::EntityHandle entity{};
    Runtime::GizmoInteraction* gizmo = nullptr;
    probe.Idle = [&]
    {
        draggingWhenIdle = gizmo->IsDragging();
        idleX = ActiveScene(engine).Raw()
            .get<Extrinsic::ECS::Components::Transform::Component>(entity).Position.x;
        engine.RequestExit();
    };
    engine.Initialize();

    gizmo = &engine.Services().Find<Runtime::SceneInteractionModule>()->Interaction();
    Runtime::EditorUiHost& host = *engine.Services().Find<Runtime::EditorUiHost>();
    auto& scene = ActiveScene(engine);
    entity = Extrinsic::ECS::Scene::CreateDefault(scene, "Gizmo target");
    probe.UiBuild = [&](std::uint32_t)
    {
        BeginTranslatePreview(*gizmo, scene, entity, 2.0f);
        host.RequestViewportInput({.CaptureViewportInput = true});
        auto& window = static_cast<Platform::Backends::Null::NullWindow&>(engine.GetWindow());
        window.QueueResize(0, 0);
        window.QueueEvent(Platform::WindowFocusEvent{.Focused = false});
    };

    engine.Run();

    EXPECT_EQ(probe.Frame, 1u) << "the second frame is minimized";
    EXPECT_FALSE(draggingWhenIdle);
    EXPECT_EQ(idleX, 0.0f);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->UndoCount(), 0u);
    EXPECT_EQ(gizmo->Diagnostics().DragsCommitted, 0u);
    engine.Shutdown();
}
