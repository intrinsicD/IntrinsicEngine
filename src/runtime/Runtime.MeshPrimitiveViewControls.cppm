module;

#include <cstdint>

export module Extrinsic.Runtime.MeshPrimitiveViewControls;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;

export namespace Extrinsic::Runtime
{
    void ApplyMeshPrimitiveViewSettings(
        ECS::Scene::Registry& scene,
        std::uint32_t stableEntityId,
        MeshPrimitiveViewSettings settings);

    void ClearMeshPrimitiveViewSettings(
        ECS::Scene::Registry& scene,
        std::uint32_t stableEntityId) noexcept;

    [[nodiscard]] MeshPrimitiveViewSettings ReadMeshPrimitiveViewSettings(
        const ECS::Scene::Registry& scene,
        std::uint32_t stableEntityId) noexcept;
}
