// Scene document serialization and IO with explicit completeness diagnostics.
module;

#include <cstdint>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.SceneSerialization;

import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

extern "C++" { namespace Extrinsic::ECS::Scene { class Registry; } }

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
        std::uint32_t GeometryPresentationEntities{0u};
        // RUNTIME-315: entities with authored attribute bindings, and loaded
        // bindings whose source no longer resolves (kept as authored intent,
        // drawn from the default source, logged as a warning).
        std::uint32_t AttributeBindingEntities{0u};
        std::uint32_t StaleAttributeBindings{0u};
        // RUNTIME-319: typed element-domain properties written/read through the
        // per-domain property tables, and properties a save skipped (no value
        // kind, or NaN values; each logged as a warning).
        std::uint32_t GeometryProperties{0u};
        std::uint32_t UnpersistedGeometryProperties{0u};
        std::uint32_t UnsupportedPersistenceEntities{0u};
        std::uint32_t UnsupportedLightEntities{0u};
        std::uint32_t UnsupportedShadowEntities{0u};
        std::uint32_t UnsupportedPhysicsEntities{0u};
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
