module;
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Scene.Manager;

namespace Extrinsic::ECS
{
    export class SceneRegistry
    {
    public:
        SceneRegistry() = default;
        ~SceneRegistry() = default;

        SceneRegistry(const SceneRegistry&) = delete;
        SceneRegistry& operator=(const SceneRegistry&) = delete;

        
    private:
        entt::registry m_Registry;
    };
}
