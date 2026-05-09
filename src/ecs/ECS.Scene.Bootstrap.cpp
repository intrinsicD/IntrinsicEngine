module;

#include <entt/entity/registry.hpp>
#include <string>
#include <string_view>

module Extrinsic.ECS.Scene.Bootstrap;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;

namespace Extrinsic::ECS::Scene
{
    void EmplaceDefaults(Registry& registry, EntityHandle entity, std::string_view name)
    {
        auto& raw = registry.Raw();
        raw.emplace<Components::MetaData>(entity, std::string{name});
        raw.emplace<Components::Transform::Component>(entity);
        raw.emplace<Components::Transform::WorldMatrix>(entity);
        raw.emplace<Components::Hierarchy::Component>(entity);
    }

    EntityHandle CreateDefault(Registry& registry, std::string_view name)
    {
        const EntityHandle entity = registry.Create();
        EmplaceDefaults(registry, entity, name);
        return entity;
    }
}
