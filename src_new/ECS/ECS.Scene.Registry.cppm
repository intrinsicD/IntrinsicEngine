module;

#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Scene.Registry;

import Extrinsic.ECS.Scene.Handle;

namespace Extrinsic::ECS::Scene
{
    // Owning wrapper around an entt::registry. Provides a minimal typed
    // entity-lifecycle surface; systems that need the full entt API — views,
    // groups, component storage, listeners — reach through Raw(). The typed
    // methods cover the common lifecycle path; Raw() is intentionally explicit
    // so privileged use is obvious at call sites.
    //
    // Thread model: single-threaded. Create/Destroy/Clear and any iteration
    // through Raw() must run on the same thread. Async producers that need
    // to mutate the registry must route through a main-thread queue.
    export class Registry
    {
    public:
        Registry() = default;
        ~Registry() = default;

        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

        [[nodiscard]] EntityHandle Create() { return m_Registry.create(); }

        void Destroy(const EntityHandle entity) { m_Registry.destroy(entity); }

        [[nodiscard]] bool IsValid(const EntityHandle entity) const noexcept
        {
            return m_Registry.valid(entity);
        }

        void Clear() { m_Registry.clear(); }

        [[nodiscard]] entt::registry& Raw() noexcept { return m_Registry; }

        [[nodiscard]] const entt::registry& Raw() const noexcept { return m_Registry; }

    private:
        entt::registry m_Registry;
    };
}
