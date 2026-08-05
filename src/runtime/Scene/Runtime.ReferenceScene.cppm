module;

#include <optional>
#include <vector>

export module Extrinsic.Runtime.ReferenceScene;

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;

namespace Extrinsic::Runtime
{
    export struct ReferenceSceneEntity
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
    };

    export struct ReferenceScenePopulation
    {
        std::vector<ReferenceSceneEntity> Entities;
        std::optional<Graphics::CameraViewInput> Camera;
    };

    export [[nodiscard]] ReferenceScenePopulation
        BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector selector,
            ECS::Scene::Registry& scene);

    export void TeardownReferenceScene(
        ECS::Scene::Registry& scene,
        const ReferenceScenePopulation& population) noexcept;
}
