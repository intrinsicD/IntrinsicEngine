module;

#include <entt/entity/entity.hpp>

export module ECS.EntityHandle;

export namespace Extrinsic::ECS
{
    using EntityHandle = entt::entity;
    constexpr EntityHandle InvalidEntityHandle = entt::null;
}
