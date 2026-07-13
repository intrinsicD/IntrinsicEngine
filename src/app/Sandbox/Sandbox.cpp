module;

#include <memory>

module Extrinsic.Sandbox;

import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.SandboxDefaultPolicies;

namespace Extrinsic::Sandbox {
class App final : public Runtime::IApplication {
public:
  void OnInitialize(Runtime::Engine &engine) override {
    m_DefaultPolicies = Runtime::RegisterSandboxDefaultRuntimePolicies(engine);
    m_EditorController.Attach(engine);
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
    m_EditorController.Detach();
    Runtime::UnregisterSandboxDefaultRuntimePolicies(engine, m_DefaultPolicies);
  }

private:
  Editor::SandboxEditorController m_EditorController{};
  Runtime::RuntimeSandboxDefaultPolicyRegistration m_DefaultPolicies{};
};

void RegisterSandboxRuntimeModules(Runtime::Engine &engine) {
  engine.EmplaceModule<Runtime::ClusteringModule>();
}

std::unique_ptr<Runtime::IApplication> CreateSandboxApp() {
  return std::make_unique<App>();
}
} // namespace Extrinsic::Sandbox
