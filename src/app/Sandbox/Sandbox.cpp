module;

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Sandbox;

import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Sandbox {
namespace {
struct SandboxDefaultPolicyHandles {
  Runtime::AssetImportPipeline *Pipeline{nullptr};
  Runtime::RuntimeInputActionRegistry *InputActions{nullptr};
  std::array<Runtime::RuntimeImportEntityAuthoringPolicyHandle, 3>
      ImportAuthoring{};
  Runtime::RuntimeImportCompletedHandlerHandle ImportCompleted{};
  Runtime::RuntimePostImportProcessorHandle DirectMeshPostProcessor{};
  std::optional<Runtime::RuntimeInputActionHandle> FocusAction{};

  [[nodiscard]] bool IsEmpty() const noexcept {
    if (Pipeline != nullptr || InputActions != nullptr ||
        ImportCompleted.IsValid() || DirectMeshPostProcessor.IsValid() ||
        FocusAction.has_value()) {
      return false;
    }
    for (const auto handle : ImportAuthoring) {
      if (handle.IsValid()) {
        return false;
      }
    }
    return true;
  }
};

void UninstallSandboxDefaultPolicies(
    SandboxDefaultPolicyHandles &handles) noexcept {
  if (handles.InputActions != nullptr && handles.FocusAction.has_value()) {
    handles.InputActions->Unregister(*handles.FocusAction);
  }
  handles.FocusAction.reset();

  if (handles.Pipeline != nullptr) {
    if (handles.DirectMeshPostProcessor.IsValid()) {
      handles.Pipeline->UnregisterPostImportProcessor(
          handles.DirectMeshPostProcessor);
    }
    handles.DirectMeshPostProcessor = {};

    if (handles.ImportCompleted.IsValid()) {
      handles.Pipeline->UnregisterImportCompletedHandler(
          handles.ImportCompleted);
    }
    handles.ImportCompleted = {};

    for (std::size_t index = handles.ImportAuthoring.size(); index > 0u;
         --index) {
      const auto handle = handles.ImportAuthoring[index - 1u];
      if (handle.IsValid()) {
        handles.Pipeline->UnregisterImportEntityAuthoringPolicy(handle);
      }
      handles.ImportAuthoring[index - 1u] = {};
    }
  } else {
    handles.DirectMeshPostProcessor = {};
    handles.ImportCompleted = {};
    handles.ImportAuthoring = {};
  }

  handles.InputActions = nullptr;
  handles.Pipeline = nullptr;
}

[[nodiscard]] bool InstallSandboxDefaultPolicies(
    Runtime::AssetImportPipeline *const pipeline,
    Runtime::RuntimeInputActionRegistry *const inputActions,
    Runtime::CameraControllerRegistry *const cameraControllers,
    Runtime::SelectionController *const selection,
    SandboxDefaultPolicyHandles &handles) {
  if (!handles.IsEmpty() || pipeline == nullptr || inputActions == nullptr) {
    return false;
  }

  handles.Pipeline = pipeline;
  handles.InputActions = inputActions;

  auto authoring = Runtime::MakeSandboxDefaultImportAuthoringPolicies();
  for (std::size_t index = 0u; index < authoring.size(); ++index) {
    handles.ImportAuthoring[index] =
        pipeline->RegisterImportEntityAuthoringPolicy(
            std::move(authoring[index]));
    if (!handles.ImportAuthoring[index].IsValid()) {
      UninstallSandboxDefaultPolicies(handles);
      return false;
    }
  }

  handles.ImportCompleted = pipeline->RegisterImportCompletedHandler(
      Runtime::MakeSandboxDefaultImportCompletedHandler(cameraControllers));
  if (!handles.ImportCompleted.IsValid()) {
    UninstallSandboxDefaultPolicies(handles);
    return false;
  }

  handles.DirectMeshPostProcessor = pipeline->RegisterPostImportProcessor(
      Runtime::MakeSandboxDefaultDirectMeshPostProcessor());
  if (!handles.DirectMeshPostProcessor.IsValid()) {
    UninstallSandboxDefaultPolicies(handles);
    return false;
  }

  if (cameraControllers != nullptr && selection != nullptr) {
    const Runtime::RuntimeInputActionHandle focusAction =
        inputActions->Register(Runtime::MakeSandboxDefaultFocusInputAction(
            *cameraControllers, *selection));
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

    auto *const pipeline = services.Find<Runtime::AssetImportPipeline>();
    auto *const inputActions =
        services.Find<Runtime::RuntimeInputActionRegistry>();
    auto *const selection = services.Find<Runtime::SelectionController>();
    (void)InstallSandboxDefaultPolicies(pipeline, inputActions,
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
