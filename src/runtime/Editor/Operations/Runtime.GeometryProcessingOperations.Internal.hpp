// Shared geometry snapshots and command helpers used by runtime implementation units.
#pragma once

namespace Extrinsic::Runtime::GeometryProcessingDetail
{
        struct PointPropertyWatch
        {
            GeometryElementDomain Domain{};
            std::string Name{};
            std::size_t Count{};
            std::optional<Geometry::PropertyRevision> Revision{};
            bool operator==(const PointPropertyWatch&) const = default;
        };
    [[nodiscard]] PointPropertyWatch ObserveGeometryProperty(const GeometryEntityAvailability&, GeometryElementDomain, std::string);
    [[nodiscard]] Geometry::PropertySet* MutableGeometryProperties(entt::registry&, entt::entity, GeometryElementDomain);
    [[nodiscard]] GeometryElementDomain PrimaryPointDomain(const GeometryEntityAvailability&);
    [[nodiscard]] bool FinitePosition(glm::vec3);
    [[nodiscard]] bool GeometryPropertiesCurrent(const EditorGeometryProcessingContext&, entt::entity,
                                               std::span<const PointPropertyWatch>);

    struct EditorMeshSourceSnapshot
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<glm::vec3> BeforePositions{};
        std::vector<bool> DeletedVertices{};
        // The mesh snapshot compacts deleted GeometrySources face slots.
        // Preserve the source slot for each resulting mesh face so callers
        // can project face diagnostics back onto the authoritative storage.
        std::vector<std::uint32_t> SourceFaceForMeshFace{};
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Diagnostic{};
    };

    [[nodiscard]] std::optional<ECS::EntityHandle>
    ResolveEditorStableEntity(
        const entt::registry& raw,
        std::uint32_t stableId);

    [[nodiscard]] std::uint64_t
    EditorGeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        ECS::EntityHandle entity);

    [[nodiscard]] EditorMeshSourceSnapshot
    BuildEditorMeshSourceSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& view);

    [[nodiscard]] std::string BuildActiveEditorGeometryJobMessage(
        std::string_view label,
        const EditorJobRecord& job);

    [[nodiscard]] EditorCommandStatus
    ToEditorMethodCommandStatus(EditorCommandHistoryStatus status) noexcept;
    [[nodiscard]] EditorMeshSourceSnapshot BuildEditorNormalMeshSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& view, std::string_view positionProperty);

}
