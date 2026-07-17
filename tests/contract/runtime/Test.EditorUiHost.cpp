#include <cstddef>
#include <memory>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Engine;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;

namespace {
class OneFrameApplication final : public Runtime::IApplication {
public:
  void OnInitialize(Runtime::Engine &) override {}
  void OnSimTick(Runtime::Engine &, double) override {}
  void OnVariableTick(Runtime::Engine &engine, double, double) override {
    engine.RequestExit();
  }
  void OnShutdown(Runtime::Engine &) override {}
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

TEST(EditorUiHost, AttachedVisibleHostRunsTheEditorFrameCallback) {
  Runtime::Engine engine(HeadlessConfig(),
                         std::make_unique<OneFrameApplication>());
  engine.Initialize();

  std::size_t frameCallbacks = 0u;
  Runtime::EditorUiHost host;
  host.Attach(engine,
              Runtime::EditorUiHostDescriptor{
                  .ToggleActionDebugName = "Test.Editor.ToggleVisibility",
                  .DrawFrame = [&frameCallbacks] { ++frameCallbacks; },
              });

  EXPECT_TRUE(host.IsAttached());
  EXPECT_TRUE(host.IsVisible());
  engine.Run();
  EXPECT_EQ(frameCallbacks, 1u);

  host.Detach();
  EXPECT_FALSE(host.IsAttached());
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
