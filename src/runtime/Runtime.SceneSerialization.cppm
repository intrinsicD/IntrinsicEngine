module;

#include <cstdint>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.SceneSerialization;

import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.ECS.Scene.Registry;

export namespace Extrinsic::Runtime
{
    struct SceneSerializationStats
    {
        std::uint32_t Entities{0u};
        std::uint32_t SelectableEntities{0u};
        std::uint32_t TransformEntities{0u};
        std::uint32_t HierarchyLinks{0u};
        std::uint32_t MeshEntities{0u};
        std::uint32_t GraphEntities{0u};
        std::uint32_t PointCloudEntities{0u};
        std::uint32_t RenderHintEntities{0u};
        std::uint32_t ProgressiveRenderDataEntities{0u};
        std::uint32_t UnsupportedPersistenceEntities{0u};
        std::uint32_t UnsupportedLightEntities{0u};
        std::uint32_t UnsupportedShadowEntities{0u};
        std::uint32_t UnsupportedPhysicsEntities{0u};
        std::uint32_t UnsupportedSpatialDebugEntities{0u};
        std::uint32_t UnsupportedAssetInstanceEntities{0u};
    };

    struct SceneSerializationResult
    {
        SceneSerializationStats Stats{};
    };

    struct SceneDeserializationResult
    {
        SceneSerializationStats Stats{};
    };

    [[nodiscard]] Core::Expected<std::string> SerializeSceneDocument(
        const ECS::Scene::Registry& scene);

    [[nodiscard]] Core::Expected<SceneSerializationResult> SaveSceneDocument(
        const ECS::Scene::Registry& scene,
        std::string_view path,
        Core::IO::IIOBackend& backend);

    [[nodiscard]] Core::Expected<SceneDeserializationResult> DeserializeSceneDocument(
        ECS::Scene::Registry& scene,
        std::string_view document);

    [[nodiscard]] Core::Expected<SceneDeserializationResult> LoadSceneDocument(
        ECS::Scene::Registry& scene,
        std::string_view path,
        Core::IO::IIOBackend& backend);
}
