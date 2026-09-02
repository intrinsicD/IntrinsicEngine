// Exposes the app-owned Sandbox session that binds editor lifetime to the
// runtime engine without moving UI composition into runtime.
module;

#include <memory>

export module Extrinsic.Sandbox;

import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Sandbox {
// The session initializes only after the runtime kernel is live and shuts down
// between Engine::BeginShutdown() and Engine::Shutdown().
class SandboxSession final {
public:
  SandboxSession();
  ~SandboxSession();

  SandboxSession(const SandboxSession &) = delete;
  SandboxSession &operator=(const SandboxSession &) = delete;
  SandboxSession(SandboxSession &&) = delete;
  SandboxSession &operator=(SandboxSession &&) = delete;

  void Initialize(const Runtime::RuntimeEngineConfig &config,
                  Runtime::WorldRegistry &worlds,
                  Runtime::ServiceRegistry &services);
  void Shutdown() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Extrinsic::Sandbox
