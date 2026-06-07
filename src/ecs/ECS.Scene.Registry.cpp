module;

#include <entt/entity/registry.hpp>

module Extrinsic.ECS.Scene.Registry;

namespace Extrinsic::ECS::Scene
{
    EntityHandle Registry::Create()
    {
        return m_Registry.create();
    }

    void Registry::Destroy(EntityHandle entity)
    {
        if (IsValid(entity))
            m_Registry.destroy(entity);
    }

    bool Registry::IsValid(EntityHandle entity) const noexcept
    {
        return m_Registry.valid(entity);
    }

    void Registry::Clear()
    {
        m_Registry.clear();
    }
}
