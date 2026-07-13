module;

#include <memory>

module Extrinsic.Sandbox;

import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorUi;

namespace Extrinsic::Sandbox {
class App final : public Runtime::IApplication {
public:
  void OnInitialize(Runtime::Engine &engine) override {
    m_DefaultPolicies = Runtime::RegisterSandboxDefaultRuntimePolicies(engine);
    m_EditorUi.Attach(engine);
    m_MethodPanels.Register(m_EditorUi);
    m_MeshProcessingPanels.Register(m_EditorUi);
  }

  void OnSimTick(Runtime::Engine &engine, double fixedDt) override {
    (void)engine;
    (void)fixedDt;
  }

  void OnVariableTick(Runtime::Engine &engine, double alpha,
                      double dt) override {
    (void)engine;
    (void)alpha;
    (void)dt;
  }

  void OnShutdown(Runtime::Engine &engine) override {
    m_MeshProcessingPanels.Unregister();
    m_MethodPanels.Unregister();
    m_EditorUi.Detach();
    Runtime::UnregisterSandboxDefaultRuntimePolicies(engine, m_DefaultPolicies);
  }

private:
  Runtime::SandboxEditorUi m_EditorUi{};
  Editor::MethodPanels m_MethodPanels{};
  Editor::MeshProcessingPanels m_MeshProcessingPanels{};
  Runtime::RuntimeSandboxDefaultPolicyRegistration m_DefaultPolicies{};
};

void RegisterSandboxRuntimeModules(Runtime::Engine &engine) {
  engine.EmplaceModule<Runtime::ClusteringModule>();
}

std::unique_ptr<Runtime::IApplication> CreateSandboxApp() {
  return std::make_unique<App>();
}
} // namespace Extrinsic::Sandbox
