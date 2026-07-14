module;

#include <memory>

export module Extrinsic.Sandbox.Editor.DomainPanels;

import Extrinsic.Runtime.SandboxEditorUi;

export namespace Extrinsic::Sandbox::Editor {
class DomainPanels final {
public:
  DomainPanels();
  ~DomainPanels();

  DomainPanels(const DomainPanels &) = delete;
  DomainPanels &operator=(const DomainPanels &) = delete;
  DomainPanels(DomainPanels &&) = delete;
  DomainPanels &operator=(DomainPanels &&) = delete;

  void Register(Runtime::SandboxEditorUi &editorUi);
  void Unregister();

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Extrinsic::Sandbox::Editor
