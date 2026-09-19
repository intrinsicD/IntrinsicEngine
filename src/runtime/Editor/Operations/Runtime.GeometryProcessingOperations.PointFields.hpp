// Private capture and scalar transactions for point-property methods, plus the
// job envelope and main-thread job/cache helpers every processing family shares.
// Include after processing-context, property, command-history and job-projection
// imports, including Core.Error for shared result diagnostics.
#pragma once
#include "GeometryIntegration/Runtime.GeometryPositionCapture.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    // Family-neutral job/cache helpers compiled in
    // `Runtime.GeometryProcessingOperations.MeshSupport.cpp`. They live here
    // rather than in `…MeshSupport.hpp` because point-set families need them
    // without that header's by-value halfedge-mesh and mesh-soup snapshots.
    namespace MeshSupport
    {
        // Terminal payload of a queued editor method job. The computed result
        // reaches the main thread through the job's own shared state, so the
        // envelope carries only a diagnostic — and exists at all because an
        // empty envelope is how `JobService` reports a dropped job.
        struct EditorJobResult { std::string Diagnostic{}; };

        void InvalidateSelectedModelCache(const EditorProcessingContext& context);

        // The guard refuses a duplicate submission when the same entity+output
        // already has a non-terminal `JobService` job. Identity stays with the
        // editor session and is resolved through its active-output query.
        [[nodiscard]] std::optional<EditorJobRecord> FindActiveEditorJob(
            const EditorProcessingContext& context, const EditorJobIdentity& identity);

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            std::string_view label, const EditorJobRecord& job);

        [[nodiscard]] bool SameGeometryPositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept;

        void AppendDerivedJobHandleToMessage(
            std::string& message,
            const JobToken handle);

        [[nodiscard]] Core::ErrorCode ResultErrorOrUnknown(
            const Core::ErrorCode error) noexcept;

    }

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
    [[nodiscard]] bool GeometryPropertiesCurrent(const EditorProcessingContext&, entt::entity,
                                               std::span<const PointPropertyWatch>);

    struct PointDeletionSource
    {
        GeometryElementDomain Domain{};
        const char* Name{"v:deleted"};
        std::size_t Divisor{1};
    };
    // Halfedge rows share the deletion flag of their paired edge.
    [[nodiscard]] PointDeletionSource ResolvePointDeletionSource(GeometryElementDomain);

    struct PointInputCapture
    {
        std::vector<PointPropertyWatch> Inputs{};
        std::vector<glm::vec3> Points{};
        std::vector<std::uint32_t> Slots{};
        std::size_t SlotCount{}, LiveCount{};
        bool ValidLbvh{true};
        bool HasSubnormalCoordinates{};
    };
    struct PointScalarCapture : PointInputCapture
    {
        PointPropertyWatch OutputWatch{};
        std::vector<float> BeforeValues{}, AfterValues{};
    };

    // Metadata-only vec3 candidates; callers retain method-specific admission.
    [[nodiscard]] GeometryPropertyCatalogSnapshot BuildPointInputCandidateCatalog(
        const GeometryEntityAvailability&, std::uint32_t stableId);

    [[nodiscard]] GeometryPropertyCatalogSnapshot BuildPointInputCatalog(const EditorProcessingContext&, std::uint32_t stableId);

    // Readiness/catalog capture validates live rows without copying values. Resolved
    // domains are returned in the references; numerical/backend gates stay with callers.
    // Captures must be empty on entry. Slots preserve ascending source-row order.
    [[nodiscard]] bool CapturePointInput(
        const GeometryEntityAvailability&, GeometryPropertyRef& positions, bool copyValues,
        PointInputCapture&, std::string& diagnostic);
    [[nodiscard]] bool PreparePointInput(
        const EditorProcessingContext&, entt::entity, const GeometryEntityAvailability&,
        GeometryPropertyRef& positions, PointInputCapture&, std::string& diagnostic);
    [[nodiscard]] bool EditorProcessingContextWorldCurrent(const EditorProcessingContext&);
    [[nodiscard]] EditorPointInputReadinessStats PointInputReadinessStats(const EditorProcessingContext&);
    // Callers resolve output domains and validate their typed config before preflight.
    [[nodiscard]] bool ValidatePointOutputs(
        const GeometryEntityAvailability&, const GeometryPropertyRef& positions,
        std::span<const GeometryPropertyRef> outputs, std::string_view outputLabel,
        std::string& diagnostic);
    [[nodiscard]] bool CapturePointScalarField(
        const GeometryEntityAvailability&, GeometryPropertyRef& positions,
        GeometryPropertyRef& output, std::string_view outputLabel, bool copyValues,
        PointScalarCapture&, std::string& diagnostic);
    [[nodiscard]] bool PointScalarFieldCurrent(
        const EditorProcessingContext&, entt::entity, const PointScalarCapture&);
    [[nodiscard]] EditorCommandHistoryStatus PublishPointScalarField(
        const EditorProcessingContext&, entt::entity, const PointScalarCapture&,
        std::string label);
}
}
