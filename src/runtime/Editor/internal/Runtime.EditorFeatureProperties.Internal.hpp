// Private property catalogs and timers shared by editor consumers.
// C++ declarations share the compiled owner Runtime.EditorFeatureContextAdapters.cpp.
#pragma once

// Requires VisualizationEditingOperations, EditorCommon,
// GeometryAvailability, VertexAttributeBinding, VertexChannelBindings,
// ECS.Components.GeometrySources and Geometry.Properties. Provide <chrono>,
// <cstddef>, <cstdint>, <optional>, <string>, <string_view> and <vector> in the
// global module fragment.

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    using namespace Extrinsic::Runtime;

    using EditorModelBuildClock = std::chrono::steady_clock;

    class ScopedEditorStatTimer final
    {
    public:
        explicit ScopedEditorStatTimer(std::uint64_t* target) noexcept;
        ScopedEditorStatTimer(const ScopedEditorStatTimer&) = delete;
        ScopedEditorStatTimer& operator=(const ScopedEditorStatTimer&) = delete;
        ~ScopedEditorStatTimer();

    private:
        std::uint64_t* m_Target{nullptr};
        EditorModelBuildClock::time_point m_Start{};
    };

    [[nodiscard]] bool IsInternalVisualizationProperty(
        const std::string& name) noexcept;

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorVisualizationPropertyDomain domain) noexcept;

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
        const GeometryEntityAvailability& availability,
        const EditorVisualizationPropertyDomain domain) noexcept;

    void AppendVisualizationPropertiesForDomain(
        std::vector<EditorVisualizationPropertyInfo>& out,
        const Geometry::PropertySet& properties,
        const EditorVisualizationPropertyDomain domain);

    [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
        const GeometryEntityAvailability& availability,
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] bool IsPropertyCatalogSupportedKind(
        const Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] std::optional<EditorPropertyCatalogDomain>
    VertexChannelCatalogDomainForView(
        const ECS::Components::GeometrySources::ConstSourceView& view) noexcept;

    [[nodiscard]] const Geometry::PropertySet*
    VertexChannelPropertySetForView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] std::optional<AttributeSourceType>
    ToAttributeSourceType(
        const Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] bool SourceTypeAllowedForVertexChannel(
        const VertexChannel channel,
        const AttributeSourceType type) noexcept;

    [[nodiscard]] AttributeBindResult EvaluateVertexChannelBinding(
        const Geometry::PropertySet& properties,
        const VertexChannel channel,
        const std::string_view propertyName,
        const AttributeSourceType sourceType,
        const std::size_t elementCount,
        EditorWorkspaceSnapshotStats* modelBuildStats);

    [[nodiscard]] std::uint64_t EditorElapsedNs(
        const EditorModelBuildClock::time_point start) noexcept;

} // namespace Extrinsic::Runtime::EditorFeatureDetail

} // extern "C++"
