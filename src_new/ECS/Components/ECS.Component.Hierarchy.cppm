module;

#include <cstdint>

export module Extrinsic.ECS.Component.Hierarchy;

import Extrinsic.ECS.Scene.Handle;

namespace Extrinsic::ECS::Components::Hierarchy
{
    struct Component
    {
        EntityHandle Parent = InvalidEntityHandle;
        EntityHandle FirstChild = InvalidEntityHandle;
        EntityHandle NextSibling = InvalidEntityHandle;
        EntityHandle PrevSibling = InvalidEntityHandle;
        uint32_t ChildCount = 0;
    };
}