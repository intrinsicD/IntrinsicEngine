module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

export module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Core.StrongHandle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    struct EditorWorkspaceSnapshotQueriesAccess;
}

export namespace Extrinsic::Runtime
{
    struct EditorSelectedModelCache;

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
        std::vector<EditorDiagnostic> Diagnostics{};
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
        EditorPropertyCatalogModel PropertyCatalog{};
        EditorBoundRenderStateModel BoundState{};
        EditorTextureBakeControlsModel TextureBake{};
        EditorGeometryProcessingModel Processing{};
        EditorPrimitiveDetailModel Primitive{};
        std::vector<EditorDiagnostic> Diagnostics{};
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
        std::vector<std::uint32_t> SelectedStableIds{};
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
            const EditorSelectedModelCacheKey&) = default;
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
    struct EditorSelectedModelCache
    {
        std::array<EditorSelectedAnalysisCacheEntry, 4u>
            SelectedAnalysis{};
        std::array<EditorVisualizationModelCacheEntry, 4u>
            Visualization{};
        EditorSelectedModelCacheStats Counters{};

        void Clear() noexcept
        {
            for (EditorSelectedAnalysisCacheEntry& entry : SelectedAnalysis)
                entry.Valid = false;
            for (EditorVisualizationModelCacheEntry& entry : Visualization)
                entry.Valid = false;
            ++Counters.Invalidations;
        }

        [[nodiscard]] EditorSelectedModelCacheStats Stats() const noexcept
        {
            EditorSelectedModelCacheStats stats = Counters;
            for (const EditorSelectedAnalysisCacheEntry& entry : SelectedAnalysis)
            {
                if (entry.Valid)
                    ++stats.Entries;
            }
            for (const EditorVisualizationModelCacheEntry& entry : Visualization)
            {
                if (entry.Valid)
                    ++stats.Entries;
            }
            return stats;
        }
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
        std::vector<EditorEntityRow> Hierarchy{};
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
        std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorWorkspaceSnapshotContext
    {
        EditorSceneEditingContext Scene{};
        EditorGeometryProcessingContext Geometry{};
        EditorVisualizationEditingContext Visualization{};
        EditorRenderRecipeEditingContext RenderRecipe{};
        EditorSelectedModelCache* SelectedModelCache{nullptr};
    };

    class EditorWorkspaceSnapshotQueries final
    {
    public:
        EditorWorkspaceSnapshotQueries() = default;

        [[nodiscard]] bool IsBound() const noexcept;

    private:
        struct State;
        std::shared_ptr<const State> m_State{};

        explicit EditorWorkspaceSnapshotQueries(
            std::shared_ptr<const State> state);
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
    [[nodiscard]] EditorDomainWindowModel BuildEditorDomainWindowModel(
        const EditorWorkspaceSnapshotContext& context,
        EditorDomainWindowKind kind);
    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotQueries& queries);
    [[nodiscard]] EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotQueries& queries,
        const EditorWorkspaceSnapshotRequest& request);
    [[nodiscard]] EditorDomainWindowModel BuildEditorDomainWindowModel(
        const EditorWorkspaceSnapshotQueries& queries,
        EditorDomainWindowKind kind,
        EditorWorkspaceSnapshotStats* modelBuildStats = nullptr);

    struct EditorWorkspaceSnapshotPreparedFrame
    {
        EditorWorkspaceSnapshot Frame{};
        EditorWorkspaceSnapshotQueries SnapshotQueries{};
    };

    [[nodiscard]] std::optional<EditorWorkspaceSnapshotPreparedFrame>
    PrepareEditorWorkspaceSnapshotFrame(
        const EditorWorkspaceAttachment& attachment,
        const EditorWorkspaceSnapshotRequest& request = {},
        std::string pendingAssetImportPath = {},
        EditorAssetPayloadKind pendingAssetImportPayloadKind =
            EditorAssetPayloadKind::Unknown,
        std::string pendingSceneFilePath = {});
}

namespace Extrinsic::Runtime
{
    struct EditorWorkspaceSnapshotQueriesAccess final
    {
        [[nodiscard]] static const EditorWorkspaceSnapshotContext*
        Resolve(const EditorWorkspaceSnapshotQueries& queries) noexcept;
    };
}
