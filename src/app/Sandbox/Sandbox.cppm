module;

#include <memory>

export module Extrinsic.Sandbox;

import Extrinsic.Runtime.Engine;

export namespace Extrinsic::Sandbox {
void RegisterSandboxRuntimeModules(Runtime::Engine &engine);
[[nodiscard]] std::unique_ptr<Runtime::IApplication> CreateSandboxApp();
} // namespace Extrinsic::Sandbox
