#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Runtime.EditorWindowRegistry;

namespace {
namespace Runtime = Extrinsic::Runtime;

[[nodiscard]] Runtime::EditorWindowDescriptor
MakeWindow(std::string id, std::vector<std::string> menuPath, int &drawCalls,
           std::vector<bool> *transitions = nullptr,
           const bool openByDefault = false) {
  return Runtime::EditorWindowDescriptor{
      .Id = std::move(id),
      .MenuPath = std::move(menuPath),
      .Title = "Test window",
      .OpenByDefault = openByDefault,
      .Draw = [&drawCalls](bool &) { ++drawCalls; },
      .OpenStateChanged =
          transitions != nullptr
              ? std::function<void(bool)>{[transitions](const bool open) {
                  transitions->push_back(open);
                }}
              : std::function<void(bool)>{},
  };
}
} // namespace

TEST(EditorWindowRegistry, RegisteredWindowsAppearInStructuredMenuModel) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;

  const Runtime::EditorWindowHandle handle = registry.Register(
      MakeWindow("mesh.simplify", {"Mesh", "Processing"}, draws));

  ASSERT_TRUE(handle.IsValid());
  const std::vector<Runtime::EditorWindowMenuEntry> menu =
      registry.BuildMenuModel();
  ASSERT_EQ(menu.size(), 1u);
  EXPECT_EQ(menu.front().Handle, handle);
  EXPECT_EQ(menu.front().Id, "mesh.simplify");
  EXPECT_EQ(menu.front().MenuPath,
            (std::vector<std::string>{"Mesh", "Processing"}));
  EXPECT_FALSE(menu.front().Open);
}

TEST(EditorWindowRegistry, InvalidAndDuplicateRegistrationsFailClosed) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;

  EXPECT_FALSE(registry.Register(MakeWindow("", {"Mesh"}, draws)).IsValid());
  EXPECT_FALSE(
      registry.Register(MakeWindow("mesh.empty-path", {}, draws)).IsValid());

  const Runtime::EditorWindowHandle first = registry.Register(
      MakeWindow("mesh.simplify", {"Mesh", "Processing"}, draws));
  ASSERT_TRUE(first.IsValid());
  EXPECT_FALSE(registry.Register(MakeWindow("mesh.simplify", {"Other"}, draws))
                   .IsValid());
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(EditorWindowRegistry, UnregisteredWindowsAreAbsentFromMenuModel) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;
  const Runtime::EditorWindowHandle handle = registry.Register(
      MakeWindow("mesh.simplify", {"Mesh", "Processing"}, draws));
  ASSERT_TRUE(handle.IsValid());

  EXPECT_TRUE(registry.Unregister(handle));
  EXPECT_FALSE(registry.Unregister(handle));
  EXPECT_TRUE(registry.BuildMenuModel().empty());
}

TEST(EditorWindowRegistry, ClosedAndGloballyHiddenWindowsDoNoPerFrameWork) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;
  const Runtime::EditorWindowHandle handle = registry.Register(
      MakeWindow("mesh.simplify", {"Mesh", "Processing"}, draws));
  ASSERT_TRUE(handle.IsValid());

  EXPECT_EQ(registry.DrawOpenWindows(), 0u);
  EXPECT_EQ(registry.DrawOpenWindows(), 0u);
  EXPECT_EQ(draws, 0);

  ASSERT_TRUE(registry.SetOpen(handle, true));
  EXPECT_EQ(registry.DrawOpenWindows(), 1u);
  EXPECT_EQ(draws, 1);

  registry.SetVisible(false);
  EXPECT_EQ(registry.DrawOpenWindows(), 0u);
  EXPECT_EQ(draws, 1);
  EXPECT_TRUE(registry.IsOpen(handle));

  registry.SetVisible(true);
  EXPECT_EQ(registry.DrawOpenWindows(), 1u);
  EXPECT_EQ(draws, 2);
}

TEST(EditorWindowRegistry, OpenTransitionsAreObservedExactlyOnce) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;
  std::vector<bool> transitions;
  const Runtime::EditorWindowHandle handle = registry.Register(
      MakeWindow("mesh.simplify", {"Mesh", "Processing"}, draws, &transitions));
  ASSERT_TRUE(handle.IsValid());

  EXPECT_TRUE(registry.SetOpen(handle, true));
  EXPECT_TRUE(registry.SetOpen(handle, true));
  EXPECT_TRUE(registry.SetOpen("mesh.simplify", false));
  EXPECT_EQ(transitions, (std::vector<bool>{true, false}));
}

TEST(EditorWindowRegistry, DrawCallbackCanCloseItsWindow) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;
  std::vector<bool> transitions;
  const Runtime::EditorWindowHandle handle =
      registry.Register(Runtime::EditorWindowDescriptor{
          .Id = "appearance.properties",
          .MenuPath = {"Mesh", "Appearance"},
          .Title = "Appearance properties",
          .OpenByDefault = true,
          .Draw =
              [&draws](bool &open) {
                ++draws;
                open = false;
              },
          .OpenStateChanged =
              [&transitions](const bool open) { transitions.push_back(open); },
      });
  ASSERT_TRUE(handle.IsValid());

  EXPECT_EQ(registry.DrawOpenWindows(), 1u);
  EXPECT_EQ(registry.DrawOpenWindows(), 0u);
  EXPECT_EQ(draws, 1);
  EXPECT_FALSE(registry.IsOpen(handle));
  EXPECT_EQ(transitions, (std::vector<bool>{false}));
}

TEST(EditorWindowRegistry, DrawCallbackMayUnregisterItself) {
  Runtime::EditorWindowRegistry registry;
  Runtime::EditorWindowHandle handle{};
  int draws = 0;
  handle = registry.Register(Runtime::EditorWindowDescriptor{
      .Id = "self-removing",
      .MenuPath = {"Debug"},
      .Title = "Self-removing window",
      .OpenByDefault = true,
      .Draw =
          [&registry, &handle, &draws](bool &) {
            ++draws;
            (void)registry.Unregister(handle);
          },
  });
  ASSERT_TRUE(handle.IsValid());

  EXPECT_EQ(registry.DrawOpenWindows(), 1u);
  EXPECT_EQ(draws, 1);
  EXPECT_EQ(registry.Size(), 0u);
}

TEST(EditorWindowRegistry, VisibilityCommandPreservesWindowOpenState) {
  Runtime::EditorWindowRegistry registry;
  int draws = 0;
  const Runtime::EditorWindowHandle handle = registry.Register(MakeWindow(
      "mesh.simplify", {"Mesh", "Processing"}, draws, nullptr, true));
  ASSERT_TRUE(handle.IsValid());

  const Runtime::EditorUiVisibilityCommandResult hidden =
      Runtime::ApplyEditorUiVisibilityCommand(
          registry, {Runtime::EditorUiVisibilityCommandKind::Hide});
  EXPECT_TRUE(hidden.WasVisible);
  EXPECT_FALSE(hidden.IsVisible);
  EXPECT_TRUE(hidden.Changed);
  EXPECT_EQ(registry.DrawOpenWindows(), 0u);
  EXPECT_EQ(draws, 0);
  EXPECT_TRUE(registry.IsOpen(handle));

  const Runtime::EditorUiVisibilityCommandResult restored =
      Runtime::ApplyEditorUiVisibilityCommand(
          registry, {Runtime::EditorUiVisibilityCommandKind::Toggle});
  EXPECT_FALSE(restored.WasVisible);
  EXPECT_TRUE(restored.IsVisible);
  EXPECT_TRUE(restored.Changed);
  EXPECT_EQ(registry.DrawOpenWindows(), 1u);
  EXPECT_EQ(draws, 1);
  EXPECT_TRUE(registry.IsOpen(handle));
}
