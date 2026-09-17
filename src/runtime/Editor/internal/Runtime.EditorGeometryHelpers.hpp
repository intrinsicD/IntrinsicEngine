// Shared entity signatures, entity lookup and command-status conversion.
// Include after editor/common, command-history and ECS registry imports;
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

    [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
        const EditorCommandHistoryStatus status) noexcept;

}
}
