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

    // The one element-domain -> overlay mapping. The Color binding, the
    // property-display recipe path and the model all use it. Without `lane`
    // the overlay is the lane a property of that domain colors by default
    // (mesh vertices and faces the surface, graph nodes the edges); with it,
    // that lane, if its Color row reads `domain`. Defined with the
    // attribute-binding model; nullopt for domains without an overlay
    // (halfedges, unknown) or a lane that does not read the domain.
    struct ColorOverlayTarget
    {
        EditorVisualizationTarget Target{EditorVisualizationTarget::Surface};
        Graphics::Components::VisualizationConfig::Domain VisDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        Graphics::Components::VisualizationConfig::ColorSource BufferSource{
            Graphics::Components::VisualizationConfig::ColorSource::PerVertexBuffer};
    };
    [[nodiscard]] std::optional<ColorOverlayTarget> ColorOverlayTargetFor(
        GeometryElementDomain domain, std::optional<EditorVisualizationTarget> lane = std::nullopt) noexcept;
    // Property the effective overlay of that lane colors `domain` by, if any.
    [[nodiscard]] std::optional<std::string> BoundColorOverlaySource(const entt::registry& raw,
        ECS::EntityHandle entity,
        GeometryElementDomain domain,
        std::optional<EditorVisualizationTarget> lane = std::nullopt);
    [[nodiscard]] EditorVisualizationTarget VisualizationTargetFor(GeometryRenderLane lane) noexcept;
}
}
