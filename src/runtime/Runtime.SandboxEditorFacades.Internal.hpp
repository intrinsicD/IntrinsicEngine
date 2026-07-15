#pragma once

namespace Extrinsic::Runtime::Detail
{
    struct SandboxEditorMeshSourceSnapshot
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<glm::vec3> BeforePositions{};
        std::vector<bool> DeletedVertices{};
        SandboxEditorCommandStatus Status{
            SandboxEditorCommandStatus::NoChange};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Diagnostic{};
    };

    [[nodiscard]] std::optional<ECS::EntityHandle>
    ResolveSandboxMethodStableEntity(
        const entt::registry& raw,
        std::uint32_t stableId);

    [[nodiscard]] std::uint64_t
    SandboxEditorGeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        ECS::EntityHandle entity);

    [[nodiscard]] SandboxEditorMeshSourceSnapshot
    BuildSandboxEditorMeshSourceSnapshot(
        const Extrinsic::ECS::Components::GeometrySources::ConstSourceView& view);

    void InvalidateSandboxMethodSelectedModelCache(
        const SandboxEditorContext& context);

    [[nodiscard]] std::optional<DerivedJobSnapshot>
    FindActiveSandboxMethodDerivedJob(
        const SandboxEditorContext& context,
        const DerivedJobKey& key);

    [[nodiscard]] std::string BuildActiveSandboxMethodDerivedJobMessage(
        std::string_view label,
        const DerivedJobSnapshot& job);

    [[nodiscard]] EditorCommandHistoryStatus
    ApplySandboxMethodPointCloudPointState(
        ECS::Scene::Registry* scene,
        std::uint32_t stableEntityId,
        const Geometry::PointCloud::Cloud& cloud);

    [[nodiscard]] SandboxEditorCommandStatus
    ToSandboxMethodCommandStatus(EditorCommandHistoryStatus status) noexcept;

    [[nodiscard]] SandboxEditorKMeansResult
    MakeSandboxEditorKMeansCompletionResult(
        const KMeansRunCompleted& completed);

    [[nodiscard]] SandboxEditorKMeansResult
    PublishSandboxEditorKMeansGpuCompletion(
        const SandboxEditorContext& context,
        const RuntimeKMeansGpuJobResult& completed);
}
