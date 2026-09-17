// Transform comparison and undo publication shared by registration and scene edits.
// Include after command-history, ECS registry, world-handle and transform imports;
// the global module fragment supplies integer and string types.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    [[nodiscard]] bool SameTransformComponent(
        const ECS::Components::Transform::Component& lhs,
        const ECS::Components::Transform::Component& rhs) noexcept;

    [[nodiscard]] EditorCommandHistoryResult ExecuteEditorTransformMutation(
        EditorCommandHistory& history,
        ECS::Scene::Registry* scene,
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const ECS::Components::Transform::Component& before,
        const ECS::Components::Transform::Component& after,
        std::string label);
}
}
