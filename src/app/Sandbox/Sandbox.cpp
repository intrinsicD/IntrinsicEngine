module;

#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Sandbox;

import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Sandbox {
namespace {
struct SandboxDefaultPolicyHandles {
  Runtime::RuntimeInputActionRegistry *InputActions{nullptr};
  std::optional<Runtime::RuntimeInputActionHandle> FocusAction{};

  [[nodiscard]] bool IsEmpty() const noexcept {
    return InputActions == nullptr && !FocusAction.has_value();
  }
};

void UninstallSandboxDefaultPolicies(
    SandboxDefaultPolicyHandles &handles) noexcept {
  if (handles.InputActions != nullptr && handles.FocusAction.has_value()) {
    handles.InputActions->Unregister(*handles.FocusAction);
  }
  handles.FocusAction.reset();

  handles.InputActions = nullptr;
}

[[nodiscard]] bool InstallSandboxDefaultPolicies(
    Runtime::RuntimeInputActionRegistry *const inputActions,
    Runtime::CameraControllerRegistry *const cameraControllers,
    Runtime::SelectionController *const selection,
    SandboxDefaultPolicyHandles &handles) {
  if (!handles.IsEmpty() || inputActions == nullptr) {
    return false;
  }

  handles.InputActions = inputActions;

  if (cameraControllers != nullptr && selection != nullptr) {
    const Runtime::RuntimeInputActionHandle focusAction = inputActions->Register(
        Runtime::MakeFocusCameraOnSelectionInputAction(
            *cameraControllers,
            *selection,
            "Sandbox.DefaultFocusCameraOnSelection",
            Runtime::RuntimeInputActionBinding{
                .KeyCode = 'F',
                .Trigger = Runtime::RuntimeInputActionTrigger::KeyJustPressed,
                .SuppressWhenImGuiCapturesKeyboard = true,
            }));
    if (!focusAction.IsValid()) {
      UninstallSandboxDefaultPolicies(handles);
      return false;
    }
    handles.FocusAction = focusAction;
  }

  return true;
}
} // namespace

struct SandboxSession::Impl {
  void Initialize(const Runtime::RuntimeEngineConfig &config,
                  Runtime::WorldRegistry &worlds,
                  Runtime::ServiceRegistry &services) {
    Shutdown();
    m_Worlds = &worlds;
    m_CameraControllers = services.Find<Runtime::CameraControllerRegistry>();

    const auto &referenceConfig = config.ReferenceScene;
    if (referenceConfig.Enabled && !m_ReferenceBootstrap.has_value()) {
      const auto world = worlds.ActiveWorld();
      if (auto *scene = worlds.Get(world); scene != nullptr) {
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

    auto *const inputActions =
        services.Find<Runtime::RuntimeInputActionRegistry>();
    auto *const selection = services.Find<Runtime::SelectionController>();
    (void)InstallSandboxDefaultPolicies(inputActions,
                                        m_CameraControllers, selection,
                                        m_DefaultPolicies);
    m_EditorController.Attach(worlds, services);
  }

  void Shutdown() noexcept {
    m_EditorController.Detach();
    UninstallSandboxDefaultPolicies(m_DefaultPolicies);
    if (m_ReferenceBootstrap.has_value()) {
      if (auto *scene = m_Worlds != nullptr
                            ? m_Worlds->Get(m_ReferenceBootstrap->World)
                            : nullptr;
          scene != nullptr) {
        Runtime::TeardownReferenceScene(*scene,
                                        m_ReferenceBootstrap->Population);
      }
      m_ReferenceBootstrap.reset();
    }
    m_CameraControllers = nullptr;
    m_Worlds = nullptr;
  }
  struct ReferenceBootstrap {
    Runtime::WorldHandle World{};
    Runtime::ReferenceScenePopulation Population{};
  };

  Editor::SandboxEditorController m_EditorController{};
  SandboxDefaultPolicyHandles m_DefaultPolicies{};
  Runtime::CameraControllerRegistry *m_CameraControllers{nullptr};
  Runtime::WorldRegistry *m_Worlds{nullptr};
  std::optional<ReferenceBootstrap> m_ReferenceBootstrap{};
};

SandboxSession::SandboxSession() : m_Impl(std::make_unique<Impl>()) {}

SandboxSession::~SandboxSession() { Shutdown(); }

void SandboxSession::Initialize(const Runtime::RuntimeEngineConfig &config,
                                Runtime::WorldRegistry &worlds,
                                Runtime::ServiceRegistry &services) {
  m_Impl->Initialize(config, worlds, services);
}

void SandboxSession::Shutdown() noexcept {
  if (m_Impl)
    m_Impl->Shutdown();
}
} // namespace Extrinsic::Sandbox
