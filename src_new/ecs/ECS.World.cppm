module;

#include <entt/entity/registry.hpp>

export module ECS.World;

import ECS.EntityHandle;

namespace Extrinsic::ECS
{
    export class World
    {
    public:
        ~World() = default;

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        [[nodiscard]] EntityHandle Create();

        void Destroy(EntityHandle entity);

        [[nodiscard]] bool IsValid(EntityHandle entity) const noexcept;

        void Clear();

        [[nodiscard]] entt::registry& Raw() noexcept { return mRegistry; }

        [[nodiscard]] const entt::registry& Raw() const noexcept { return mRegistry; }
    private:
        entt::registry mRegistry;
    };
}