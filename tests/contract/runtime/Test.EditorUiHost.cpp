#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <imgui.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

namespace Core = Extrinsic::Core;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace {
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Simulate(double) override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

struct UiBracketProbeState {
  std::vector<std::string> Trace{};
  const Runtime::EditorInputCaptureSnapshot *UiBeginCapture{};
  const Runtime::EditorInputCaptureSnapshot *UiBuildCapture{};
  const Runtime::EditorInputCaptureSnapshot *UiEndCapture{};
  const Runtime::EditorInputCaptureSnapshot *BeforeExtractionCapture{};
  bool UiBeginSawStartedFrame{false};
  bool UiBeginClaimed{false};
  bool UiBuildClaimed{false};
  bool UiEndClaimed{false};
  bool BeforeExtractionClaimed{false};
};

class UiBracketApplication final : public Intrinsic::Tests::RuntimeTestModule
{
public:
  explicit UiBracketApplication(UiBracketProbeState &state) : State(state) {}

  void Resolve() override {}
  void Simulate(double) override {}
  void Frame(double, double) override
  {
      auto& engine = Kernel();
      State.Trace.emplace_back("variable_tick");
      engine.RequestExit();
  }
  void Shutdown() override {}

  private:
  UiBracketProbeState &State;
};

class UiBracketProbeModule final : public Runtime::IRuntimeModule {
public:
  explicit UiBracketProbeModule(UiBracketProbeState &state) : State(state) {}

  [[nodiscard]] std::string_view Name() const noexcept override {
    // Frame hooks are sorted by module name. This probe intentionally runs
    // after Runtime.EditorUiModule within each phase.
    return "zz.Runtime.EditorUiBracketProbe";
  }

  [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup &setup) override {
    if (Core::Result result = setup.RegisterFrameHook(
            Runtime::FramePhase::UiBegin,
            [this](Runtime::RuntimeFrameHookContext &context) {
              State.Trace.emplace_back("ui_begin_tail");
              State.UiBeginCapture = &context.EditorCapture;
              State.UiBeginClaimed =
                  context.EditorCapture.CapturesViewportInput();
              State.UiBeginSawStartedFrame =
                  ImGui::GetCurrentContext() != nullptr &&
                  ImGui::GetFrameCount() > 0;
            });
        !result.has_value()) {
      return result;
    }
    if (Core::Result result = setup.RegisterFrameHook(
            Runtime::FramePhase::UiBuild,
            [this](Runtime::RuntimeFrameHookContext &context) {
              State.Trace.emplace_back("ui_build_tail");
              State.UiBuildCapture = &context.EditorCapture;
              State.UiBuildClaimed =
                  context.EditorCapture.CapturesViewportInput();
            });
        !result.has_value()) {
      return result;
    }
    if (Core::Result result = setup.RegisterFrameHook(
            Runtime::FramePhase::UiEndCapture,
            [this](Runtime::RuntimeFrameHookContext &context) {
              State.Trace.emplace_back("ui_end_capture_tail");
              State.UiEndCapture = &context.EditorCapture;
              State.UiEndClaimed =
                  context.EditorCapture.CapturesViewportInput();
            });
        !result.has_value()) {
      return result;
    }
    return setup.RegisterFrameHook(
        Runtime::FramePhase::BeforeExtraction,
        [this](Runtime::RuntimeFrameHookContext &context) {
          State.Trace.emplace_back("before_extraction");
          State.BeforeExtractionCapture = &context.EditorCapture;
          State.BeforeExtractionClaimed =
              context.EditorCapture.CapturesViewportInput();
        });
  }

  [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup &) override {
    return Core::Ok();
  }
  void OnShutdown(Runtime::RuntimeModuleShutdownContext &) override {}

private:
  UiBracketProbeState &State;
};

struct OmittedEditorProbeState {
  const Runtime::EditorInputCaptureSnapshot *UiEndCapture{};
  const Runtime::EditorInputCaptureSnapshot *BeforeExtractionCapture{};
  bool UiEndClaimed{false};
  bool BeforeExtractionClaimed{false};
};

class OmittedEditorProbeModule final : public Runtime::IRuntimeModule {
public:
  explicit OmittedEditorProbeModule(OmittedEditorProbeState &state)
      : State(state) {}

  [[nodiscard]] std::string_view Name() const noexcept override {
    return "Runtime.OmittedEditorProbe";
  }
  [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup &setup) override {
    if (Core::Result result = setup.RegisterFrameHook(
            Runtime::FramePhase::UiEndCapture,
            [this](Runtime::RuntimeFrameHookContext &context) {
              State.UiEndCapture = &context.EditorCapture;
              State.UiEndClaimed =
                  context.EditorCapture.CapturesViewportInput();
            });
        !result.has_value()) {
      return result;
    }
    return setup.RegisterFrameHook(
        Runtime::FramePhase::BeforeExtraction,
        [this](Runtime::RuntimeFrameHookContext &context) {
          State.BeforeExtractionCapture = &context.EditorCapture;
          State.BeforeExtractionClaimed =
              context.EditorCapture.CapturesViewportInput();
        });
  }
  [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup &) override {
    return Core::Ok();
  }
  void OnShutdown(Runtime::RuntimeModuleShutdownContext &) override {}

private:
  OmittedEditorProbeState &State;
};

struct EditorShutdownProbeState {
  bool WindowWasLive{false};
  bool RendererWasLive{false};
  bool OverlayWasDetached{false};
  bool HostWasWithdrawn{false};
};

class EditorShutdownProbeModule final : public Runtime::IRuntimeModule {
public:
  explicit EditorShutdownProbeModule(EditorShutdownProbeState &state)
      : State(state) {}

  [[nodiscard]] std::string_view Name() const noexcept override {
    // Sorted before EditorUiModule so reverse shutdown observes its teardown.
    return "A.Runtime.EditorUiShutdownProbe";
  }
  [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup &) override {
    return Core::Ok();
  }
  [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup &) override {
    return Core::Ok();
  }
  void OnShutdown(Runtime::RuntimeModuleShutdownContext &context) override {
    State.WindowWasLive =
        context.Services.Find<Extrinsic::Platform::IWindow>() != nullptr;
    const Graphics::IRenderer *renderer =
        context.Services.Find<Graphics::IRenderer>();
    State.RendererWasLive = renderer != nullptr;
    State.OverlayWasDetached =
        renderer != nullptr && !renderer->HasImGuiOverlaySystem();
    State.HostWasWithdrawn =
        context.Services.Find<Runtime::EditorUiHost>() == nullptr;
  }

private:
  EditorShutdownProbeState &State;
};

class ReinitializeApplication final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    void Resolve() override { ++BootCount; }
    void Simulate(double) override {}
    void Frame(double, double) override
    {
        auto& engine = Kernel();
        if (BootCount == 2u)
        {
            const Extrinsic::Platform::IWindow& window = engine.GetWindow();
            auto& input = const_cast<Extrinsic::Platform::Input::Context&>(window.GetInput());
            input.SetKeyState(Extrinsic::Platform::Input::Key::G, true);
        }
        engine.RequestExit();
    }
    void Shutdown() override {}

    std::uint32_t BootCount{0u};
};

[[nodiscard]] Core::Config::EngineConfig HeadlessConfig() {
  Core::Config::EngineConfig config{};
  config.Simulation.WorkerThreadCount = 1u;
  config.ReferenceScene.Enabled = false;
  config.Camera.Enabled = false;
  config.Window.Backend = Core::Config::WindowBackend::Null;
  return config;
}
} // namespace

TEST(EditorUiHost, ComposedModulePublishesHostAndRunsFrameContribution) {
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    std::size_t frameCallbacks  = 0u;
    Runtime::EditorUiHost* host = engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(host, nullptr);
    ASSERT_TRUE(host->IsOperational());
    const Runtime::EditorUiFrameContributionHandle contribution =
        host->RegisterFrameContribution([&frameCallbacks] { ++frameCallbacks; });
    ASSERT_TRUE(contribution.IsValid());

    EXPECT_TRUE(host->IsVisible());
    engine.Run();
    EXPECT_EQ(frameCallbacks, 1u);
    EXPECT_EQ(host->GetDiagnostics().FramesProduced, 1u);
    EXPECT_TRUE(host->UnregisterFrameContribution(contribution));

    engine.Shutdown();
}

TEST(EditorUiHost, OmittedModuleLeavesEditorHostUnpublished) {
  OmittedEditorProbeState state{};
  Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                             std::make_unique<OneFrameApplication>());
  engine.EmplaceModule<OmittedEditorProbeModule>(state);
  engine.Initialize();

  EXPECT_EQ(engine.Services().Find<Runtime::EditorUiHost>(), nullptr);
  engine.Run();

  ASSERT_NE(state.UiEndCapture, nullptr);
  ASSERT_NE(state.BeforeExtractionCapture, nullptr);
  EXPECT_EQ(state.UiEndCapture, state.BeforeExtractionCapture);
  EXPECT_FALSE(state.UiEndClaimed);
  EXPECT_FALSE(state.BeforeExtractionClaimed);
  const Runtime::RuntimeFramePacingDiagnostics &pacing =
      engine.GetLastFramePacingDiagnostics();
  EXPECT_EQ(pacing.ImGuiBeginMicros, 0u);
  EXPECT_EQ(pacing.ImGuiEndMicros, 0u);
  EXPECT_EQ(pacing.ImGuiEditorCallbackMicros, 0u);
  EXPECT_EQ(pacing.ImGuiDrawListCount, 0u);
  EXPECT_EQ(pacing.ImGuiFontAtlasCopyCount, 0u);
  engine.Shutdown();
}

TEST(EditorUiHost, VisibilityCommandPreservesRegisteredWindowOpenState) {
  std::size_t windowCallbacks = 0u;
  Runtime::EditorUiHost host;
  const Runtime::EditorWindowHandle window =
      host.RegisterWindow(Runtime::EditorWindowDescriptor{
          .Id = "test.window",
          .MenuPath = {"Test"},
          .Title = "Window",
          .OpenByDefault = true,
          .Draw = [&windowCallbacks](bool &) { ++windowCallbacks; },
      });
  ASSERT_TRUE(window.IsValid());

  const Runtime::EditorUiVisibilityCommandResult hidden =
      host.ApplyVisibilityCommand(Runtime::EditorUiVisibilityCommand{
          Runtime::EditorUiVisibilityCommandKind::Hide});
  EXPECT_TRUE(hidden.Changed);
  EXPECT_FALSE(host.IsVisible());
  EXPECT_EQ(host.Windows().DrawOpenWindows(), 0u);
  ASSERT_EQ(host.BuildWindowMenuModel().size(), 1u);
  EXPECT_TRUE(host.BuildWindowMenuModel().front().Open);

  const Runtime::EditorUiVisibilityCommandResult shown =
      host.ApplyVisibilityCommand(Runtime::EditorUiVisibilityCommand{
          Runtime::EditorUiVisibilityCommandKind::Show});
  EXPECT_TRUE(shown.Changed);
  EXPECT_EQ(host.Windows().DrawOpenWindows(), 1u);
  EXPECT_EQ(windowCallbacks, 1u);
}

TEST(EditorUiHost, FrameContributionMutationIsSafeAndDeterministic) {
  Runtime::EditorUiHost host;
  Runtime::EditorUiHostOwnerControl owner = host.ClaimOwnerControl();
  ASSERT_TRUE(owner.IsValid());
  EXPECT_FALSE(host.ClaimOwnerControl().IsValid());
  owner.SetOperational(true);

  std::uint32_t selfRemovingCalls = 0u;
  std::uint32_t peerRemovingCalls = 0u;
  std::uint32_t removedPeerCalls = 0u;
  Runtime::EditorUiFrameContributionHandle selfRemoving{};
  Runtime::EditorUiFrameContributionHandle removedPeer{};

  selfRemoving = host.RegisterFrameContribution([&] {
    ++selfRemovingCalls;
    EXPECT_TRUE(host.UnregisterFrameContribution(selfRemoving));
  });
  const Runtime::EditorUiFrameContributionHandle peerRemoving =
      host.RegisterFrameContribution([&] {
        ++peerRemovingCalls;
        (void)host.UnregisterFrameContribution(removedPeer);
      });
  removedPeer = host.RegisterFrameContribution([&] {
    ++removedPeerCalls;
  });
  ASSERT_TRUE(selfRemoving.IsValid());
  ASSERT_TRUE(peerRemoving.IsValid());
  ASSERT_TRUE(removedPeer.IsValid());

  EXPECT_EQ(owner.DrawFrameContributions(), 2u);
  EXPECT_EQ(owner.DrawFrameContributions(), 1u);
  EXPECT_EQ(selfRemovingCalls, 1u);
  EXPECT_EQ(peerRemovingCalls, 2u);
  EXPECT_EQ(removedPeerCalls, 0u);
}

TEST(EditorUiModule, PreservesBeginVariableBuildEndAndCaptureOrdering) {
  UiBracketProbeState state{};
  Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                             std::make_unique<UiBracketApplication>(state));
  engine.EmplaceModule<Runtime::EditorUiModule>();
  engine.EmplaceModule<UiBracketProbeModule>(state);
  engine.Initialize();

  Runtime::EditorUiHost *host =
      engine.Services().Find<Runtime::EditorUiHost>();
  ASSERT_NE(host, nullptr);
  EXPECT_FALSE(host->ClaimOwnerControl().IsValid());
  const Runtime::EditorUiFrameContributionHandle contribution =
      host->RegisterFrameContribution([&state] {
        state.Trace.emplace_back("ui_contribution");
        ImGui::GetIO().WantCaptureKeyboard = true;
      });
  ASSERT_TRUE(contribution.IsValid());

  engine.Run();

  EXPECT_EQ(state.Trace,
            (std::vector<std::string>{
                "ui_begin_tail",
                "variable_tick",
                "ui_contribution",
                "ui_build_tail",
                "ui_end_capture_tail",
                "before_extraction",
            }));
  EXPECT_TRUE(state.UiBeginSawStartedFrame);
  EXPECT_FALSE(state.UiBeginClaimed);
  EXPECT_FALSE(state.UiBuildClaimed);
  EXPECT_TRUE(state.UiEndClaimed);
  EXPECT_TRUE(state.BeforeExtractionClaimed);
  ASSERT_NE(state.UiBeginCapture, nullptr);
  EXPECT_EQ(state.UiBeginCapture, state.UiBuildCapture);
  EXPECT_EQ(state.UiBeginCapture, state.UiEndCapture);
  EXPECT_EQ(state.UiBeginCapture, state.BeforeExtractionCapture);
  EXPECT_TRUE(host->GetDiagnostics().CapturesViewportInput);

  engine.Shutdown();
}

TEST(EditorUiModule, ReverseShutdownWithdrawsHostAndDetachesLiveRenderer) {
  EditorShutdownProbeState state{};
  Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                             std::make_unique<OneFrameApplication>());
  engine.EmplaceModule<EditorShutdownProbeModule>(state);
  engine.EmplaceModule<Runtime::EditorUiModule>();
  engine.Initialize();

  ASSERT_NE(engine.Services().Find<Runtime::EditorUiHost>(), nullptr);
  ASSERT_TRUE(engine.GetRenderer().HasImGuiOverlaySystem());

  engine.Shutdown();

  EXPECT_TRUE(state.WindowWasLive);
  EXPECT_TRUE(state.RendererWasLive);
  EXPECT_TRUE(state.OverlayWasDetached);
  EXPECT_TRUE(state.HostWasWithdrawn);
  EXPECT_EQ(engine.Services().Find<Runtime::EditorUiHost>(), nullptr);
}

TEST(EditorUiModule, ShutdownReinitializeStartsFromFreshEditorState) {
  auto application = std::make_unique<ReinitializeApplication>();
  ReinitializeApplication *applicationPtr = application.get();
  Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
  engine.EmplaceModule<Runtime::EditorUiModule>();
  engine.Initialize();

  Runtime::EditorUiHost *firstHost =
      engine.Services().Find<Runtime::EditorUiHost>();
  ASSERT_NE(firstHost, nullptr);
  std::uint32_t staleContributionCalls = 0u;
  const Runtime::EditorUiFrameContributionHandle staleContribution =
      firstHost->RegisterFrameContribution([&] {
        ++staleContributionCalls;
        ImGui::GetIO().WantCaptureKeyboard = true;
      });
  ASSERT_TRUE(staleContribution.IsValid());
  const Runtime::EditorWindowHandle staleWindow =
      firstHost->RegisterWindow(Runtime::EditorWindowDescriptor{
          .Id = "stale.window",
          .MenuPath = {"Test"},
          .Title = "Stale",
          .OpenByDefault = true,
          .Draw = [](bool &) {},
      });
  ASSERT_TRUE(staleWindow.IsValid());

  engine.Run();
  EXPECT_EQ(staleContributionCalls, 1u);
  EXPECT_TRUE(firstHost->GetDiagnostics().CapturesViewportInput);
  const Runtime::EditorUiVisibilityCommandResult hidden =
      firstHost->ApplyVisibilityCommand(Runtime::EditorUiVisibilityCommand{
          Runtime::EditorUiVisibilityCommandKind::Hide});
  EXPECT_TRUE(hidden.Changed);
  EXPECT_FALSE(firstHost->GetDiagnostics().CapturesViewportInput);
  engine.Shutdown();

  engine.Initialize();
  ASSERT_EQ(applicationPtr->BootCount, 2u);
  Runtime::EditorUiHost *secondHost =
      engine.Services().Find<Runtime::EditorUiHost>();
  ASSERT_NE(secondHost, nullptr);
  EXPECT_TRUE(secondHost->IsOperational());
  EXPECT_TRUE(secondHost->IsVisible());
  EXPECT_TRUE(secondHost->BuildWindowMenuModel().empty());
  EXPECT_TRUE(secondHost->GetDiagnostics().Initialized);
  EXPECT_EQ(secondHost->GetDiagnostics().FramesProduced, 0u);
  EXPECT_FALSE(secondHost->GetDiagnostics().CapturesViewportInput);

  engine.Run();

  // One fresh G action toggles visible -> hidden. A leaked first-boot action
  // would toggle twice and leave the host visible.
  EXPECT_FALSE(secondHost->IsVisible());
  EXPECT_FALSE(secondHost->GetDiagnostics().CapturesViewportInput);
  EXPECT_EQ(staleContributionCalls, 1u);
  engine.Shutdown();
}

TEST(EditorUiModule, FailedRegistrationAndResolutionWithdrawPartialHost) {
  Runtime::CommandBus commands;
  Runtime::KernelEventBus events;
  Runtime::JobService jobs;
  Runtime::WorldRegistry worlds;
  Runtime::ServiceRegistry services;
  Runtime::EditorUiModule module;

  services.BeginRegistration();
  Runtime::EngineSetup missingHookSetup(
      commands,
      events,
      jobs,
      worlds,
      services,
      [](Runtime::SimSystemDesc) {},
      {});
  const Core::Result registration = module.OnRegister(missingHookSetup);
  EXPECT_FALSE(registration.has_value());
  EXPECT_EQ(registration.error(), Core::ErrorCode::InvalidState);
  EXPECT_EQ(services.Find<Runtime::EditorUiHost>(), nullptr);

  std::vector<std::pair<Runtime::FramePhase, Runtime::RuntimeFrameHook>> hooks;
  services.BeginRegistration();
  Runtime::EngineSetup workingSetup(
      commands,
      events,
      jobs,
      worlds,
      services,
      [](Runtime::SimSystemDesc) {},
      [&hooks](const Runtime::FramePhase phase,
               Runtime::RuntimeFrameHook hook) {
        hooks.emplace_back(phase, std::move(hook));
      });
  ASSERT_TRUE(module.OnRegister(workingSetup).has_value());
  ASSERT_EQ(hooks.size(), 3u);
  ASSERT_NE(services.Find<Runtime::EditorUiHost>(), nullptr);

  services.BeginResolution();
  const Core::Result resolution = module.OnResolve(workingSetup);
  EXPECT_FALSE(resolution.has_value());
  EXPECT_EQ(resolution.error(), Core::ErrorCode::ResourceNotFound);
  EXPECT_EQ(services.Find<Runtime::EditorUiHost>(), nullptr);

  // The failed boot state can be reset and registered from a fresh host.
  hooks.clear();
  services.BeginRegistration();
  ASSERT_TRUE(module.OnRegister(workingSetup).has_value());
  ASSERT_EQ(hooks.size(), 3u);
  Runtime::EditorUiHost *freshHost =
      services.Find<Runtime::EditorUiHost>();
  ASSERT_NE(freshHost, nullptr);
  EXPECT_TRUE(freshHost->IsVisible());
  EXPECT_FALSE(freshHost->IsOperational());
  EXPECT_TRUE(freshHost->BuildWindowMenuModel().empty());
  EXPECT_FALSE(freshHost->GetDiagnostics().Initialized);
  EXPECT_FALSE(freshHost->ClaimOwnerControl().IsValid());

  Runtime::RuntimeModuleShutdownContext shutdown{
      .Commands = commands,
      .Events = events,
      .Jobs = jobs,
      .Worlds = worlds,
      .Services = services,
  };
  module.OnShutdown(shutdown);
  EXPECT_EQ(services.Find<Runtime::EditorUiHost>(), nullptr);
}
