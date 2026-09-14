// Shared entity signatures and undo helpers for scene and geometry operations.
// Include after editor/common, command-history, ECS registry and transform imports;
// the including global module fragment provides integer, optional and string types.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    inline constexpr std::uint64_t kEditorSignatureOffset = 1469598103934665603ull;

    void MixSignature(std::uint64_t& signature,
                      std::uint64_t value) noexcept;

    void MixSignatureString(std::uint64_t& signature,
                            const std::string_view value) noexcept;

    [[nodiscard]] std::uint64_t GeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        const ECS::EntityHandle entity);

    [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
        const entt::registry& raw,
        const std::uint32_t stableId);

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

    [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
        const EditorCommandHistoryStatus status) noexcept;

}
}
