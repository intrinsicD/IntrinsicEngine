// Shared render-hint component snapshots and mutations for editor history.
// Definitions compile once in Runtime.EditorFeatureContextAdapters.cpp.
#pragma once

// Requires Graphics.Component.RenderGeometry, ECS.Scene.Handle, ECS.Scene.Registry
// and Runtime.EditorCommandHistory; provide EnTT registry declarations,
// <cstdint> and <optional> in the global module fragment.

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    struct EditorRenderHintComponents
    {
        std::optional<Graphics::Components::RenderSurface> Surface{};
        std::optional<Graphics::Components::RenderEdges> Edges{};
        std::optional<Graphics::Components::RenderPoints> Points{};
    };

    [[nodiscard]] EditorRenderHintComponents ReadRenderHintComponents(
        const entt::registry& raw,
        ECS::EntityHandle entity);

    [[nodiscard]] bool SameRenderHintComponents(
        const EditorRenderHintComponents& lhs,
        const EditorRenderHintComponents& rhs);

    [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintComponents(
        ECS::Scene::Registry* scene,
        std::uint32_t stableEntityId,
        const EditorRenderHintComponents& state);
}
}
