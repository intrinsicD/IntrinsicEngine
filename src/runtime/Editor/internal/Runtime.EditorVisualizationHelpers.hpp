// Shared stored and effective visualization lookup for editor commands and models.
// Include after VisualizationEditingOperations, VisualizationConfig and ECS.Scene.Handle;
// provide EnTT registry declarations and <optional> in the global module fragment.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    [[nodiscard]] std::optional<Graphics::Components::VisualizationConfig>
    StoredVisualizationConfigForTarget(
        const entt::registry& raw,
        ECS::EntityHandle entity,
        EditorVisualizationTarget target);

    [[nodiscard]] std::optional<Graphics::Components::VisualizationConfig>
    EffectiveVisualizationConfigForTarget(
        const entt::registry& raw,
        ECS::EntityHandle entity,
        EditorVisualizationTarget target);
}
}
