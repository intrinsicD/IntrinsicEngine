// Shared stored and effective visualization lookup for editor commands and models.
// Include after VisualizationEditingOperations, GeometryAvailability, VisualizationConfig and ECS.Scene.Handle;
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

    // RUNTIME-315: the one element-domain -> overlay-lane mapping. The Color
    // binding, the property-display recipe path and the model all use it.
    // Defined with the attribute-binding model; nullopt for domains without an
    // overlay (halfedges, unknown).
    struct ColorOverlayTarget
    {
        EditorVisualizationTarget Target{EditorVisualizationTarget::Surface};
        Graphics::Components::VisualizationConfig::Domain VisDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        Graphics::Components::VisualizationConfig::ColorSource BufferSource{
            Graphics::Components::VisualizationConfig::ColorSource::PerVertexBuffer};
    };
    [[nodiscard]] std::optional<ColorOverlayTarget> ColorOverlayTargetFor(
        GeometryElementDomain domain) noexcept;
    // Property the effective overlay of that lane colors `domain` by, if any.
    [[nodiscard]] std::optional<std::string> BoundColorOverlaySource(
        const entt::registry& raw, ECS::EntityHandle entity, GeometryElementDomain domain);
}
}
