module;

#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Sandbox;

import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Sandbox {
class App final : public Runtime::IApplication {
public:
  void OnInitialize(Runtime::Engine &engine) override {
    m_CameraControllers =
        engine.Services().Find<Runtime::CameraControllerRegistry>();

    const auto &referenceConfig = engine.GetEngineConfig().ReferenceScene;
    if (referenceConfig.Enabled && !m_ReferenceBootstrap.has_value()) {
      const auto world = engine.ActiveWorld();
      if (auto *scene = engine.Worlds().Get(world); scene != nullptr) {
        Runtime::ReferenceScenePopulation population =
            Runtime::BootstrapReferenceScene(referenceConfig.Selector, *scene);
        if (m_CameraControllers != nullptr) {
          (void)m_CameraControllers->SetWorldSeed(world, population.Camera);
        }
        m_ReferenceBootstrap = ReferenceBootstrap{
            .World = world,
            .Population = std::move(population),
        };
      }
    }

    m_DefaultPolicies = Runtime::RegisterSandboxDefaultRuntimePolicies(
        engine, m_CameraControllers);
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
    if (m_ReferenceBootstrap.has_value()) {
      if (auto *scene = engine.Worlds().Get(m_ReferenceBootstrap->World);
          scene != nullptr) {
        Runtime::TeardownReferenceScene(
            *scene, m_ReferenceBootstrap->Population);
      }
      m_ReferenceBootstrap.reset();
    }
    m_CameraControllers = nullptr;
  }

private:
  struct ReferenceBootstrap {
    Runtime::WorldHandle World{};
    Runtime::ReferenceScenePopulation Population{};
  };

  Editor::SandboxEditorController m_EditorController{};
  Runtime::RuntimeSandboxDefaultPolicyRegistration m_DefaultPolicies{};
  Runtime::CameraControllerRegistry *m_CameraControllers{nullptr};
  std::optional<ReferenceBootstrap> m_ReferenceBootstrap{};
};

void RegisterSandboxRuntimeModules(Runtime::Engine &engine) {
  engine.EmplaceModule<Runtime::AsyncWorkModule>();
  engine.EmplaceModule<Runtime::CameraModule>();
  engine.EmplaceModule<Runtime::ClusteringModule>();
  engine.EmplaceModule<Runtime::EditorUiModule>();
}

std::unique_ptr<Runtime::IApplication> CreateSandboxApp() {
  return std::make_unique<App>();
}
} // namespace Extrinsic::Sandbox
