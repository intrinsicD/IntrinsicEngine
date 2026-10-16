// Shared output revision guards for editor property history; include after the
// processing-context, geometry availability/properties and history imports.
#pragma once

extern "C++"
{
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
    [[nodiscard]] PointPropertyWatch ObserveGeometryProperty(
        const GeometryEntityAvailability&, GeometryElementDomain, std::string);
    [[nodiscard]] bool GeometryPropertiesCurrent(const EditorProcessingContext&, entt::entity,
                                                std::span<const PointPropertyWatch>);
    [[nodiscard]] std::vector<PointPropertyWatch> ObserveGeometryProperties(
        const EditorProcessingContext&, entt::entity, std::span<const GeometryPropertyRef>);

    // Whole-set authors observe the existing tracked keys, including absent outputs.
    // No domain selects all property sets replaced by an entity-state publication.
    [[nodiscard]] std::vector<PointPropertyWatch> ObservePointPropertyHistory(
        const EditorProcessingContext&, entt::entity, std::optional<GeometryElementDomain> domain);

    struct PointPropertyHistoryWatch;
    using PointPropertyHistoryWatches = std::vector<std::shared_ptr<PointPropertyHistoryWatch>>;
    // Related commands share a guard only while the live output still matches it.
    // Replacing a stale slot does not change guards retained by older commands.
    [[nodiscard]] PointPropertyHistoryWatches CapturePointPropertyHistory(
        const EditorProcessingContext&, entt::entity, std::span<const GeometryPropertyRef>);
    [[nodiscard]] bool PointPropertyHistoryCurrent(const EditorProcessingContext&, entt::entity,
                                                 std::span<const std::shared_ptr<PointPropertyHistoryWatch>>);
    // Call only after all guarded writes succeed, before any external callback.
    void StampPointPropertyHistory(const EditorProcessingContext&, entt::entity,
                                   std::span<const std::shared_ptr<PointPropertyHistoryWatch>>);
    // Other established history authors propagate only watches they still owned
    // immediately before their successful writes. Mismatched old guards stay stale.
    void RebasePointPropertyHistory(const EditorProcessingContext&, entt::entity,
                                   std::span<const PointPropertyWatch> before);
}
}
