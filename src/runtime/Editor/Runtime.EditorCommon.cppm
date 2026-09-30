// Shared editor diagnostics, domain identifiers and inspection statistics.
module;

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.EditorCommon;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorJobProjection;

export namespace Extrinsic::Runtime
{
    enum class EditorDiagnosticCode : std::uint8_t
    {
        MissingScene,
        MissingSelectionController,
        MissingImGuiAdapter,
        AssetImportUnavailable,
        AssetImportFailed,
        SceneFileUnavailable,
        SceneFileFailed,
        NoSelectedEntity,
        UnsupportedGeometryDomain,
        CameraRenderCommandsUnavailable,
        VisualizationCommandsUnavailable,
        RenderRecipeCommandsUnavailable,
        InvalidVisualizationProperty,
        InvalidVertexChannelBinding,
        GeometryProcessingFailed,
        RenderGraphStatsUnavailable,
        EditorCommandHistoryUnavailable,
        CorruptHierarchy,
    };
    enum class EditorCommandStatus : std::uint8_t
    {
        Applied,
        Pending,
        NoChange,
        MissingScene,
        MissingSelectionController,
        MissingCameraControllerRegistry,
        MissingAssetImportCommands,
        MissingSceneFileCommands,
        MissingPrimitiveViewCommands,
        MissingVisualizationCommands,
        AssetImportFailed,
        SceneNewFailed,
        SceneSaveFailed,
        SceneLoadFailed,
        SceneCloseFailed,
        StaleEntity,
        MissingTransform,
        UnsupportedGeometryDomain,
        InvalidVisualizationProperty,
        InvalidVertexChannelBinding,
        InvalidProcessingParameters,
        GeometryProcessingFailed,
    };
    // Phase of a GPU property transaction (ADR 0030 decisions 5-7): a method previews on the
    // device and publishes on Accept. Shared by every method that runs one (property
    // smoothing, vertex normals, ...).
    enum class EditorGpuTransactionPhase : std::uint8_t
    {
        Running,        // the device is computing (or a chunked solve is between chunks)
        ReadyToAccept,  // the front holds the result (or a stopped run's latest preview)
        Accepting,      // the front is being read back and published
        Applied,        // the CPU publication succeeded; the front is canonical
        Discarded,      // discarded, cancelled or stopped before any preview
        Failed,         // the device or the publication failed; nothing changed
    };
    [[nodiscard]] const char* ToString(EditorGpuTransactionPhase phase) noexcept;
    enum class EditorDomainWindowKind : std::uint8_t
    {
        Mesh,
        Graph,
        PointCloud,
    };
    [[nodiscard]] const char* DebugNameForEditorDiagnosticCode(
        EditorDiagnosticCode code) noexcept;
    [[nodiscard]] const char* DebugNameForEditorCommandStatus(
        EditorCommandStatus status) noexcept;
    [[nodiscard]] const char* DebugNameForEditorGeometryDomain(
        ECS::Components::GeometrySources::Domain domain) noexcept;
    [[nodiscard]] const char* DebugNameForEditorDomainWindowKind(
        EditorDomainWindowKind kind) noexcept;
    struct EditorDiagnostic
    {
        EditorDiagnosticCode Code{EditorDiagnosticCode::MissingScene};
        std::string                 Message{};
    };
    struct EditorWorkspaceSnapshotStats
    {
        std::uint32_t HierarchyModelBuilds{0u};
        std::uint32_t InspectorModelBuilds{0u};
        std::uint32_t SelectionModelBuilds{0u};
        std::uint32_t PropertyCatalogModelBuilds{0u};
        std::uint32_t VertexChannelTargetBuilds{0u};
        std::uint32_t VertexChannelResolverScans{0u};
        std::uint32_t VertexChannelScratchAllocations{0u};
        std::uint64_t VertexChannelScratchBytes{0u};
        std::uint32_t GeometryPresentationModelBuilds{0u};
        std::uint32_t BoundStateModelBuilds{0u};
        std::uint32_t UvDiagnosticsModelBuilds{0u};
        std::uint64_t UvDiagnosticsTexcoordElementsScanned{0u};
        std::uint32_t TextureBakeModelBuilds{0u};
        std::uint64_t TextureBakeSourceRowsEnumerated{0u};
        std::uint32_t VisualizationModelBuilds{0u};
        std::uint32_t DomainWindowModelBuilds{0u};
        std::uint32_t DomainWindowModelCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheMisses{0u};
        std::uint32_t VisualizationModelCacheHits{0u};
        std::uint32_t VisualizationModelCacheMisses{0u};
        std::uint64_t PanelFrameModelBuildTimeNs{0u};
        std::uint64_t InspectorModelBuildTimeNs{0u};
        std::uint64_t SelectedAnalysisModelBuildTimeNs{0u};
        std::uint64_t PropertyCatalogModelBuildTimeNs{0u};
        std::uint64_t VertexChannelValidationTimeNs{0u};
        std::uint64_t UvDiagnosticsModelBuildTimeNs{0u};
        std::uint64_t TextureBakeModelBuildTimeNs{0u};
        std::uint64_t VisualizationModelBuildTimeNs{0u};
        std::uint64_t DomainWindowModelBuildTimeNs{0u};
        std::uint64_t WorkspaceSnapshotBuildTimeNs{0u};
    };
    struct EditorUvDiagnosticsModel
    {
        bool HasSelectedEntity{false};
        bool IsMesh{false};
        bool HasTexcoords{false};
        bool TexcoordCountMatchesVertices{false};
        bool TexcoordsFinite{false};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::size_t VertexCount{0u};
        std::size_t TexcoordCount{0u};
        std::size_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint32_t ChartCount{0u};
        std::uint32_t SeamSplitVertexCount{0u};
        std::string Provenance{};
        std::string BackendId{};
        std::string LastFailure{};
        bool CheckerPreviewAvailable{false};
        std::optional<EditorJobRecord> UvRegenerationJob{};
    };

}
