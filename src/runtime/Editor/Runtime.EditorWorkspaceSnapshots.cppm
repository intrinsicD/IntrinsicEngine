// Read-only editor workspace and panel snapshots, including cached selections.
// Exposes copied presentation state without granting scene mutation access.
module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Core.Std;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

namespace Extrinsic::Runtime
{
    struct EditorWorkspaceSnapshotQueriesAccess;
}

export namespace Extrinsic::Runtime
{
    struct EditorGeometryDomainModel
    {
        ECS::Components::GeometrySources::Domain Domain{
            ECS::Components::GeometrySources::Domain::None};
        bool        Valid{false};
        std::size_t VertexCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t HalfedgeCount{0u};
        std::size_t FaceCount{0u};
        std::size_t NodeCount{0u};
    };
    struct EditorInspectorModel
    {
        bool                             HasEntity{false};
        EditorEntityRow          Entity{};
        EditorTransformModel     Transform{};
        EditorRenderHintModel    RenderHints{};
        EditorGeometryDomainModel Geometry{};
        EditorPropertyCatalogModel PropertyCatalog{};
        EditorGeometryPresentationModel GeometryPresentation{};
        EditorBoundRenderStateModel BoundState{};
        EditorTextureBakeControlsModel TextureBake{};
        EditorGeometryProcessingCapabilities Processing{};
        Core::Std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorDomainWindowModel
    {
        EditorDomainWindowKind Kind{EditorDomainWindowKind::Mesh};
        ECS::Components::GeometrySources::Domain ExpectedDomain{
            ECS::Components::GeometrySources::Domain::Mesh};
        bool HasSelectedEntity{false};
        EditorEntityRow SelectedEntity{};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        bool DomainMatches{false};
        EditorRenderHintModel RenderHints{};
        bool PrimitiveViewControlsAvailable{false};
        bool HasPrimitiveViewSettings{false};
        EditorPrimitiveViewSettings PrimitiveView{};
        bool VisualizationControlsAvailable{false};
        bool VisualizationTargetAvailable{false};
        EditorVisualizationTarget VisualizationTarget{
            EditorVisualizationTarget::Entity};
        EditorVisualizationModel Visualization{};
        // Entity-level: independent of which base lanes are visible.
        EditorVectorFieldModel VectorFields{};
        EditorPropertyCatalogModel PropertyCatalog{};
        EditorBoundRenderStateModel BoundState{};
        EditorTextureBakeControlsModel TextureBake{};
        EditorGeometryProcessingModel Processing{};
        EditorPrimitiveDetailModel Primitive{};
        Core::Std::vector<EditorDiagnostic> Diagnostics{};
    };
    enum class EditorSelectedModelCacheSection : std::uint8_t
    {
        SelectedAnalysis,
        Visualization,
    };
    enum class EditorSelectedAnalysisCacheConsumer : std::uint8_t
    {
        Inspector,
        MeshDomainWindow,
        GraphDomainWindow,
        PointCloudDomainWindow,
    };
    struct EditorSelectedModelCacheKey
    {
        EditorSelectedModelCacheSection Section{
            EditorSelectedModelCacheSection::SelectedAnalysis};
        EditorSelectedAnalysisCacheConsumer SelectedAnalysisConsumer{
            EditorSelectedAnalysisCacheConsumer::Inspector};
        EditorVisualizationTarget VisualizationTarget{
            EditorVisualizationTarget::Entity};
        std::uint32_t PrimaryStableId{0u};
        Core::Std::vector<std::uint32_t> SelectedStableIds{};
        std::uint64_t SelectionGeneration{0u};
        std::uint64_t PrimitiveSelectionGeneration{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        std::size_t VertexCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t HalfedgeCount{0u};
        std::size_t FaceCount{0u};
        std::size_t NodeCount{0u};
        std::uint64_t GeometryMetadataSignature{0u};
        std::uint64_t RenderHintSignature{0u};
        std::uint64_t VisualizationStateSignature{0u};
        std::uint64_t BindingGeneration{0u};
        std::uint64_t GeometryPresentationRecipeGeneration{0u};
        std::uint64_t DerivedJobStateSignature{0u};
        std::uint64_t CommandHistoryRevision{0u};
        std::uint64_t VisualizationRecipeRevision{0u};
        std::uint32_t ViewportWidth{0u};
        std::uint32_t ViewportHeight{0u};
        bool VisualizationCommandsAvailable{false};
        bool VisualizationRecipesAvailable{false};

        friend bool operator==(
            const EditorSelectedModelCacheKey&,
            const EditorSelectedModelCacheKey&);
    };
    struct EditorSelectedAnalysisModel
    {
        EditorPropertyCatalogModel PropertyCatalog{};
        EditorGeometryPresentationModel GeometryPresentation{};
        EditorBoundRenderStateModel BoundState{};
        EditorTextureBakeControlsModel TextureBake{};
    };
    struct EditorSelectedAnalysisCacheEntry
    {
        bool Valid{false};
        EditorSelectedModelCacheKey Key{};
        EditorSelectedAnalysisModel Model{};
    };
    struct EditorVisualizationModelCacheEntry
    {
        bool Valid{false};
        EditorSelectedModelCacheKey Key{};
        EditorVisualizationModel Model{};
    };
    struct EditorSelectedModelCacheStats
    {
        std::uint32_t SelectedAnalysisCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheMisses{0u};
        std::uint32_t VisualizationModelCacheHits{0u};
        std::uint32_t VisualizationModelCacheMisses{0u};
        std::uint32_t Invalidations{0u};
        std::uint32_t Entries{0u};
    };
    // C++ language linkage so reference-only consumers can forward declare
    // these aggregates instead of importing this module.
    extern "C++" {
    struct EditorSelectedModelCache
    {
        Core::Std::array<EditorSelectedAnalysisCacheEntry, 4u>
            SelectedAnalysis{};
        Core::Std::array<EditorVisualizationModelCacheEntry, 4u>
            Visualization{};
        EditorSelectedModelCacheStats Counters{};

        void Clear() noexcept;

        [[nodiscard]] EditorSelectedModelCacheStats Stats() const noexcept;
    };
    struct EditorWorkspaceSnapshotRequest
    {
        bool Hierarchy{true};
        bool Inspector{true};
        bool Selection{true};
        bool Document{true};
        bool SceneFile{true};
        bool FileImport{true};
        bool AssetImportQueue{true};
        bool RenderGraph{true};
        bool RenderRecipe{true};
        bool CameraRender{true};
        bool Visualization{true};
    };
    struct EditorWorkspaceSnapshot
    {
        Core::Std::vector<EditorEntityRow> Hierarchy{};
        EditorInspectorModel         Inspector{};
        EditorSelectionModel         Selection{};
        EditorDocumentModel          Document{};
        EditorSceneFileModel        SceneFile{};
        EditorFileImportModel        FileImport{};
        EditorAssetImportQueueModel  AssetImportQueue{};
        EditorRenderGraphModel       RenderGraph{};
        EditorRenderRecipeEditorModel RenderRecipe{};
        EditorCameraRenderModel      CameraRender{};
        EditorVisualizationModel     Visualization{};
        EditorWorkspaceSnapshotStats        ModelBuildStats{};
        Core::Std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorWorkspaceSnapshotContext
    {
        EditorSceneEditingContext Scene{};
        EditorProcessingContext Geometry{};
        EditorVisualizationEditingContext Visualization{};
        EditorRenderRecipeEditingContext RenderRecipe{};
        EditorSelectedModelCache* SelectedModelCache{nullptr};
    };
    } // extern "C++"

    class EditorWorkspaceSnapshotQueries final
    {
    public:
        EditorWorkspaceSnapshotQueries() = default;

        [[nodiscard]] bool IsBound() const noexcept;

    private:
        Core::Std::shared_ptr<const EditorWorkspaceSnapshotContext> m_Context{};

        explicit EditorWorkspaceSnapshotQueries(
            Core::Std::shared_ptr<const EditorWorkspaceSnapshotContext> context);
        friend struct EditorWorkspaceSnapshotQueriesAccess;
        friend EditorWorkspaceSnapshotQueries
        BindEditorWorkspaceSnapshotQueries(
            EditorWorkspaceSnapshotContext context);
    };

    [[nodiscard]] EditorWorkspaceSnapshotQueries
    BindEditorWorkspaceSnapshotQueries(
        EditorWorkspaceSnapshotContext context);

    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotContext& context);
    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotContext& context,
        const EditorWorkspaceSnapshotRequest& request);
    // An explicit entity binds all model sections without changing scene selection; zero means no entity.
    [[nodiscard]] EditorInspectorModel BuildEditorInspectorModel(
        const EditorWorkspaceSnapshotContext& context,
        Core::Std::optional<std::uint32_t> entity = Core::Std::nullopt);
    [[nodiscard]] EditorDomainWindowModel BuildEditorDomainWindowModel(
        const EditorWorkspaceSnapshotContext& context,
        EditorDomainWindowKind kind,
        Core::Std::optional<std::uint32_t> entity = Core::Std::nullopt);
    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotQueries& queries);
    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotQueries& queries,
        const EditorWorkspaceSnapshotRequest& request);
    [[nodiscard]] EditorInspectorModel BuildEditorInspectorModel(
        const EditorWorkspaceSnapshotQueries& queries,
        EditorWorkspaceSnapshotStats* modelBuildStats = nullptr,
        Core::Std::optional<std::uint32_t> entity = Core::Std::nullopt);
    [[nodiscard]] EditorDomainWindowModel BuildEditorDomainWindowModel(
        const EditorWorkspaceSnapshotQueries& queries,
        EditorDomainWindowKind kind,
        EditorWorkspaceSnapshotStats* modelBuildStats = nullptr,
        Core::Std::optional<std::uint32_t> entity = Core::Std::nullopt);

    struct EditorWorkspaceSnapshotPreparedFrame
    {
        EditorWorkspaceSnapshot Frame{};
        EditorWorkspaceSnapshotQueries SnapshotQueries{};
    };

    [[nodiscard]] Core::Std::optional<EditorWorkspaceSnapshotPreparedFrame>
    PrepareEditorWorkspaceSnapshotFrame(
        const EditorWorkspaceAttachment& attachment,
        const EditorWorkspaceSnapshotRequest& request = {},
        Core::Std::string pendingAssetImportPath = {},
        EditorAssetPayloadKind pendingAssetImportPayloadKind =
            EditorAssetPayloadKind::Unknown,
        Core::Std::string pendingSceneFilePath = {});
}
