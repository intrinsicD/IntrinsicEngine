// Declares the app-owned domain panels that present runtime snapshots and
// commands without taking ownership of domain state or operations.
module;

#include <memory>
#include "Sandbox.EditorFwd.hpp"

export module Extrinsic.Sandbox.Editor.DomainPanels;


export namespace Extrinsic::Sandbox::Editor {
class DomainPanels final {
public:
  DomainPanels();
  ~DomainPanels();

  DomainPanels(const DomainPanels &) = delete;
  DomainPanels &operator=(const DomainPanels &) = delete;
  DomainPanels(DomainPanels &&) = delete;
  DomainPanels &operator=(DomainPanels &&) = delete;

  void Register(EditorShell &editorShell);
  void Unregister();

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Extrinsic::Sandbox::Editor
