module;

#include <entt/entity/entity.hpp>

export module Extrinsic.ECS.Scene.Handle;

export namespace Extrinsic::ECS::Scene
{
    using EntityHandle = entt::entity;
    constexpr EntityHandle InvalidEntityHandle = entt::null;
}
