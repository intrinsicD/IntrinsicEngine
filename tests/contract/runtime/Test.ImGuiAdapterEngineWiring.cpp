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

#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <imgui.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ImGuiAdapter;
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
    class BoundedRunApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        explicit BoundedRunApplication(const std::uint32_t targetFrames)
            : m_TargetFrames(targetFrames)
        {
        }

        void OnInitialize(Engine& /*engine*/) override {}
        void OnSimTick(Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Engine& engine, double /*alpha*/, double /*dt*/) override
        {
            ++m_VariableTicks;
            if (m_VariableTicks >= m_TargetFrames)
                engine.RequestExit();
        }
        void OnShutdown(Engine& /*engine*/) override {}

    private:
        std::uint32_t m_TargetFrames{0u};
        std::uint32_t m_VariableTicks{0u};
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

    class UiCapturedInputApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Engine& engine) override
        {
            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            engine.GetCameraControllerRegistry().Register(Runtime::CameraControllerSlot::Main,
                                                          std::move(controller));
        }

        void OnSimTick(Engine& /*engine*/, double /*fixedDt*/) override {}

        void OnVariableTick(Engine& engine, double /*alpha*/, double /*dt*/) override
        {
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

        void OnShutdown(Engine& /*engine*/) override {}

        RecordingCameraController* Controller{nullptr};
        std::uint32_t              VariableTicks{0u};
    };

    class CloseAfterInteractiveInputApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Engine& engine) override
        {
            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            engine.GetCameraControllerRegistry().Register(Runtime::CameraControllerSlot::Main,
                                                          std::move(controller));
        }

        void OnSimTick(Engine& /*engine*/, double /*fixedDt*/) override {}

        void OnVariableTick(Engine& engine, double /*alpha*/, double /*dt*/) override
        {
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

        void OnShutdown(Engine& /*engine*/) override {}

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
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled         = false;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig InputRoutingConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = true;
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
    Engine engine(HeadlessConfig(), std::make_unique<BoundedRunApplication>(1u));
    engine.Initialize();

    const auto& diag = engine.GetImGuiAdapter().GetDiagnostics();
    EXPECT_TRUE(engine.GetImGuiAdapter().IsInitialized());
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
    Engine engine(HeadlessConfig(), std::make_unique<BoundedRunApplication>(kFrames));
    engine.Initialize();

    if (engine.GetWindow().ShouldClose())
    {
        // No live window backend (e.g. headless CI with no display):
        // Engine::Run() would execute zero frames. The static wiring is still
        // asserted; the per-frame loop assertion needs a real window.
        EXPECT_TRUE(engine.GetImGuiAdapter().IsInitialized());
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame loop coverage requires a display";
    }

    engine.Run();

    EXPECT_EQ(engine.GetImGuiAdapter().GetDiagnostics().FramesProduced, kFrames);

    engine.Shutdown();
}

// The editor hook registered through the engine is invoked once per engine
// frame, and a panel draw issued by the hook flows into the produced overlay
// frame's draw lists.
TEST(ImGuiAdapterEngineWiring, EditorHookInvokedOncePerFrameAndProducesDrawLists)
{
    constexpr std::uint32_t kFrames = 3u;
    Engine engine(HeadlessConfig(), std::make_unique<BoundedRunApplication>(kFrames));
    engine.Initialize();

    std::uint32_t editorCalls = 0u;
    engine.SetImGuiEditorCallback(
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

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame editor-hook coverage requires a display";
    }

    engine.Run();

    const auto& diag = engine.GetImGuiAdapter().GetDiagnostics();
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
    Engine engine(NullWindowHeadlessConfig(),
                  std::make_unique<BoundedRunApplication>(kFrames));
    engine.Initialize();

    std::uint32_t editorCalls = 0u;
    engine.SetImGuiEditorCallback(
        [&editorCalls]
        {
            ++editorCalls;
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f));
            ImGui::Begin("UI-030 Frame Pacing Panel");
            ImGui::Text("frame pacing diagnostics");
            ImGui::End();
        });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    const Runtime::RuntimeFramePacingDiagnostics& pacing =
        engine.GetLastFramePacingDiagnostics();
    const Runtime::ImGuiAdapterDiagnostics& imgui =
        engine.GetImGuiAdapter().GetDiagnostics();
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

TEST(ImGuiAdapterEngineWiring, UiCaptureSuppressesRuntimeInputConsumers)
{
    auto app = std::make_unique<UiCapturedInputApplication>();
    auto* appPtr = app.get();
    Engine engine(InputRoutingConfig(), std::move(app));
    engine.Initialize();

    std::uint32_t editorFrames = 0u;
    engine.SetImGuiEditorCallback(
        [&editorFrames]
        {
            ++editorFrames;
            if (editorFrames == 1u)
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                ImGui::SetNextFrameWantCaptureKeyboard(true);
            }
        });

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; input-routing coverage requires a live window";
    }

    engine.Run();

    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_EQ(appPtr->VariableTicks, 2u);
    EXPECT_EQ(editorFrames, 2u);
    EXPECT_TRUE(engine.GetImGuiAdapter().WantsMouseCapture());
    EXPECT_TRUE(engine.GetImGuiAdapter().WantsKeyboardCapture());

    EXPECT_EQ(appPtr->Controller->Updates, 1u);
    EXPECT_EQ(appPtr->Controller->KeyboardUpdates, 0u);
    EXPECT_EQ(appPtr->Controller->MouseClickUpdates, 0u);

    const auto selectionDiagnostics =
        engine.GetSelectionController().GetDiagnostics();
    EXPECT_EQ(selectionDiagnostics.ClickRequestsSubmitted, 0u);
    EXPECT_EQ(selectionDiagnostics.PicksDrained, 0u);
    EXPECT_EQ(engine.GetSelectionController().InFlightPickCount(), 0u);
    EXPECT_EQ(engine.GetGizmoInteraction().ModifierMask(), 0u);

    engine.Shutdown();
}

TEST(ImGuiAdapterEngineWiring, RunNormalizesNativeCloseBeforeFirstFrame)
{
    auto app = std::make_unique<UiCapturedInputApplication>();
    auto* appPtr = app.get();
    Engine engine(InputRoutingConfig(), std::move(app));
    engine.Initialize();

    if (!RequestNativeWindowClose(engine.GetWindow()))
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; native close-state coverage requires a live GLFW window";
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
    Engine engine(InputRoutingConfig(), std::move(app));
    engine.Initialize();

    std::uint32_t editorFrames = 0u;
    engine.SetImGuiEditorCallback(
        [&editorFrames]
        {
            ++editorFrames;
            if (editorFrames == 1u)
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                ImGui::SetNextFrameWantCaptureKeyboard(true);
            }
        });

    if (engine.GetWindow().ShouldClose() ||
        engine.GetWindow().GetNativeHandle() == nullptr ||
        glfwSetWindowShouldClose == nullptr)
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; native close-state coverage requires a live GLFW window";
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
