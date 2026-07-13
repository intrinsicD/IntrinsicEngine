module;

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EditorUiHost;

export import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;

export namespace Extrinsic::Runtime {
struct EditorUiHostDescriptor {
  std::string ToggleActionDebugName{"EditorUi.ToggleVisibility"};
  std::function<void()> DrawFrame{};
};

class EditorUiHost final {
public:
  EditorUiHost();
  ~EditorUiHost();

  EditorUiHost(const EditorUiHost &) = delete;
  EditorUiHost &operator=(const EditorUiHost &) = delete;
  EditorUiHost(EditorUiHost &&) = delete;
  EditorUiHost &operator=(EditorUiHost &&) = delete;

  void Attach(Engine &engine, EditorUiHostDescriptor descriptor);
  void Detach();

  [[nodiscard]] EditorWindowHandle
  RegisterWindow(EditorWindowDescriptor descriptor);
  [[nodiscard]] bool UnregisterWindow(EditorWindowHandle handle);
  [[nodiscard]] bool SetWindowOpen(std::string_view id, bool open);
  [[nodiscard]] std::vector<EditorWindowMenuEntry> BuildWindowMenuModel() const;

  [[nodiscard]] EditorUiVisibilityCommandResult
  ApplyVisibilityCommand(EditorUiVisibilityCommand command) noexcept;
  [[nodiscard]] bool IsVisible() const noexcept;
  [[nodiscard]] bool IsAttached() const noexcept;

  [[nodiscard]] EditorWindowRegistry &Windows() noexcept;
  [[nodiscard]] const EditorWindowRegistry &Windows() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Extrinsic::Runtime
