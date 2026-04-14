module;

export module Extrinsic.ECS.Scene.Handle;

import Core.Handle;

namespace Extrinsic::ECS::Scene
{
    struct EntityTag;
    using EntityHandle = Core::StrongHandle<EntityTag>;
}
