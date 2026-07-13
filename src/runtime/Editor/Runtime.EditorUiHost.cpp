module;

#include <functional>
#include <memory>
#include <string>
#include <utility>

module Extrinsic.Runtime.EditorUiHost;

import Extrinsic.Core.Error;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.InputActions;

namespace Extrinsic::Runtime {
struct EditorUiHost::Impl {
  Engine *AttachedEngine{nullptr};
  EditorWindowRegistry WindowRegistry{};
  RuntimeInputActionHandle VisibilityAction{};
  std::function<void()> DrawFrame{};
};

EditorUiHost::EditorUiHost() : m_Impl(std::make_unique<Impl>()) {}

EditorUiHost::~EditorUiHost() { Detach(); }

void EditorUiHost::Attach(Engine &engine, EditorUiHostDescriptor descriptor) {
  Detach();

  m_Impl->AttachedEngine = &engine;
  m_Impl->DrawFrame = std::move(descriptor.DrawFrame);
  engine.SetImGuiEditorVisible(m_Impl->WindowRegistry.IsVisible());
  m_Impl->VisibilityAction = engine.RegisterInputAction(RuntimeInputActionDesc{
      .DebugName = descriptor.ToggleActionDebugName.empty()
                       ? "EditorUi.ToggleVisibility"
                       : std::move(descriptor.ToggleActionDebugName),
      .Binding =
          RuntimeInputActionBinding{
              .KeyCode = Platform::Input::Key::G,
              .Trigger = RuntimeInputActionTrigger::KeyJustPressed,
              .SuppressWhenImGuiCapturesKeyboard = false,
          },
      .Execute = [this](const RuntimeInputActionContext &,
                        RuntimeInputActionServices &) -> Core::Result {
        (void)ApplyVisibilityCommand(
            EditorUiVisibilityCommand{EditorUiVisibilityCommandKind::Toggle});
        return Core::Ok();
      },
  });
  engine.SetImGuiEditorCallback([this] {
    if (m_Impl->AttachedEngine == nullptr ||
        !m_Impl->WindowRegistry.IsVisible() || !m_Impl->DrawFrame) {
      return;
    }
    m_Impl->DrawFrame();
  });
}

void EditorUiHost::Detach() {
  if (m_Impl->AttachedEngine != nullptr) {
    m_Impl->AttachedEngine->UnregisterInputAction(m_Impl->VisibilityAction);
    m_Impl->AttachedEngine->SetImGuiEditorCallback({});
    m_Impl->AttachedEngine->SetImGuiEditorVisible(true);
  }
  m_Impl->VisibilityAction = {};
  m_Impl->DrawFrame = {};
  m_Impl->AttachedEngine = nullptr;
}

EditorWindowHandle
EditorUiHost::RegisterWindow(EditorWindowDescriptor descriptor) {
  return m_Impl->WindowRegistry.Register(std::move(descriptor));
}

bool EditorUiHost::UnregisterWindow(const EditorWindowHandle handle) {
  return m_Impl->WindowRegistry.Unregister(handle);
}

bool EditorUiHost::SetWindowOpen(const std::string_view id, const bool open) {
  return m_Impl->WindowRegistry.SetOpen(id, open);
}

std::vector<EditorWindowMenuEntry> EditorUiHost::BuildWindowMenuModel() const {
  return m_Impl->WindowRegistry.BuildMenuModel();
}

EditorUiVisibilityCommandResult EditorUiHost::ApplyVisibilityCommand(
    const EditorUiVisibilityCommand command) noexcept {
  EditorUiVisibilityCommandResult result =
      ApplyEditorUiVisibilityCommand(m_Impl->WindowRegistry, command);
  if (m_Impl->AttachedEngine != nullptr)
    m_Impl->AttachedEngine->SetImGuiEditorVisible(result.IsVisible);
  return result;
}

bool EditorUiHost::IsVisible() const noexcept {
  return m_Impl->WindowRegistry.IsVisible();
}

bool EditorUiHost::IsAttached() const noexcept {
  return m_Impl->AttachedEngine != nullptr;
}

EditorWindowRegistry &EditorUiHost::Windows() noexcept {
  return m_Impl->WindowRegistry;
}

const EditorWindowRegistry &EditorUiHost::Windows() const noexcept {
  return m_Impl->WindowRegistry;
}
} // namespace Extrinsic::Runtime
