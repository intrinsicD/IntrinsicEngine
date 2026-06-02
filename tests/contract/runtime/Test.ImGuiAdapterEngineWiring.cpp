// RUNTIME-090 Slice B — contract coverage that the runtime-side Dear ImGui
// adapter is wired into `Engine`: the engine constructs and initializes the
// adapter (after the Window and Renderer exist), owns the overlay system it
// produces into, exposes the editor hook, and brackets each variable tick with
// the adapter so exactly one `ImGuiOverlayFrame` is produced per engine frame.
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

#include <gtest/gtest.h>
#include <imgui.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;

using Extrinsic::Runtime::Engine;

namespace
{
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

    engine.Shutdown();
}
