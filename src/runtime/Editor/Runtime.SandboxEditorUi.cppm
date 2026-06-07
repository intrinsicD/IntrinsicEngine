module;

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;

export namespace Extrinsic::Runtime
{
    enum class SandboxEditorDiagnosticCode : std::uint8_t
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
    };

    enum class SandboxEditorCommandStatus : std::uint8_t
    {
        Applied,
        NoChange,
        MissingScene,
        MissingSelectionController,
        MissingCameraControllerRegistry,
        MissingAssetImportCommands,
        MissingSceneFileCommands,
        MissingPrimitiveViewCommands,
        MissingVisualizationCommands,
        AssetImportFailed,
        SceneSaveFailed,
        SceneLoadFailed,
        StaleEntity,
        MissingTransform,
        UnsupportedGeometryDomain,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorDiagnosticCode(
        SandboxEditorDiagnosticCode code) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorCommandStatus(
        SandboxEditorCommandStatus status) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryDomain(
        ECS::Components::GeometrySources::Domain domain) noexcept;

    enum class SandboxEditorDomainWindowKind : std::uint8_t
    {
        Mesh,
        Graph,
        PointCloud,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorDomainWindowKind(
        SandboxEditorDomainWindowKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorPrimitiveKind(
        RefinedPrimitiveKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorCameraControllerKind(
        Core::Config::CameraControllerKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorSpatialDebugKind(
        ECS::Components::SpatialDebugGeometryKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationColorSource(
        Graphics::Components::VisualizationConfig::ColorSource source) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationDomain(
        Graphics::Components::VisualizationConfig::Domain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationAdapterBindingKind(
        RenderExtractionCache::VisualizationAdapterBindingKind kind) noexcept;

    enum class SandboxEditorGeometryProcessingDomain : std::uint32_t
    {
        None = 0u,
        MeshVertices = 1u << 0u,
        MeshEdges = 1u << 1u,
        MeshHalfedges = 1u << 2u,
        MeshFaces = 1u << 3u,
        GraphVertices = 1u << 4u,
        GraphEdges = 1u << 5u,
        GraphHalfedges = 1u << 6u,
        PointCloudPoints = 1u << 7u,
    };

    [[nodiscard]] constexpr SandboxEditorGeometryProcessingDomain operator|(
        const SandboxEditorGeometryProcessingDomain lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<SandboxEditorGeometryProcessingDomain>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr SandboxEditorGeometryProcessingDomain operator&(
        const SandboxEditorGeometryProcessingDomain lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<SandboxEditorGeometryProcessingDomain>(
            static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }

    constexpr SandboxEditorGeometryProcessingDomain& operator|=(
        SandboxEditorGeometryProcessingDomain& lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool HasAnySandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingDomain domains,
        const SandboxEditorGeometryProcessingDomain query) noexcept
    {
        return static_cast<std::uint32_t>(domains & query) != 0u;
    }

    enum class SandboxEditorGeometryProcessingAlgorithm : std::uint8_t
    {
        KMeans,
        Remeshing,
        Simplification,
        Smoothing,
        Subdivision,
        Repair,
        NormalEstimation,
        ShortestPath,
        ConvexHull,
        SurfaceReconstruction,
        VectorHeat,
        Parameterization,
        BooleanCSG,
        Registration,
        BilateralFilter,
        OutlierEstimation,
        KernelDensity,
    };

    struct SandboxEditorGeometryProcessingCapabilities
    {
        SandboxEditorGeometryProcessingDomain Domains{
            SandboxEditorGeometryProcessingDomain::None};
        bool HasEditableSurfaceMesh{false};

        [[nodiscard]] bool HasAny() const noexcept
        {
            return HasEditableSurfaceMesh ||
                   Domains != SandboxEditorGeometryProcessingDomain::None;
        }
    };

    struct SandboxEditorGeometryProcessingEntry
    {
        SandboxEditorGeometryProcessingAlgorithm Algorithm{
            SandboxEditorGeometryProcessingAlgorithm::KMeans};
        SandboxEditorGeometryProcessingDomain Domains{
            SandboxEditorGeometryProcessingDomain::None};
    };

    [[nodiscard]] SandboxEditorGeometryProcessingDomain
    GetSandboxEditorSupportedGeometryProcessingDomains(
        SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept;

    [[nodiscard]] bool SupportsSandboxEditorGeometryProcessingDomain(
        SandboxEditorGeometryProcessingAlgorithm algorithm,
        SandboxEditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] SandboxEditorGeometryProcessingCapabilities
    GetSandboxEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        SandboxEditorGeometryProcessingCapabilities capabilities);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingDomain>
    GetAvailableSandboxEditorKMeansDomains(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryProcessingDomain(
        SandboxEditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryProcessingAlgorithm(
        SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept;

    struct SandboxEditorDiagnostic
    {
        SandboxEditorDiagnosticCode Code{SandboxEditorDiagnosticCode::MissingScene};
        std::string                 Message{};
    };

    struct SandboxEditorEntityRow
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t     StableEntityId{0u};
        std::string       Name{};
        bool              Selectable{false};
        bool              Selected{false};
        bool              Hovered{false};
        bool              HasDurableStableId{false};
        ECS::Components::StableId DurableStableId{};
    };

    struct SandboxEditorTransformModel
    {
        bool      HasLocalTransform{false};
        glm::vec3 LocalPosition{0.0f};
        glm::quat LocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 LocalScale{1.0f};
        bool      HasWorldTransform{false};
        glm::vec3 WorldPosition{0.0f};
    };

    struct SandboxEditorRenderHintModel
    {
        bool HasRenderSurface{false};
        std::string SurfaceDomain{};
        bool HasRenderLines{false};
        std::string LineDomain{};
        bool        HasUniformLineWidth{false};
        float       UniformLineWidth{0.0f};
        bool        HasNamedLineWidth{false};
        std::string LineWidthName{};
        bool HasRenderPoints{false};
        std::string PointRenderType{};
        bool        HasUniformPointSize{false};
        float       UniformPointSize{0.0f};
        bool        HasNamedPointSize{false};
        std::string PointSizeName{};
    };

    struct SandboxEditorGeometryDomainModel
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

    struct SandboxEditorInspectorModel
    {
        bool                             HasEntity{false};
        SandboxEditorEntityRow          Entity{};
        SandboxEditorTransformModel     Transform{};
        SandboxEditorRenderHintModel    RenderHints{};
        SandboxEditorGeometryDomainModel Geometry{};
        SandboxEditorGeometryProcessingCapabilities Processing{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPrimitiveDetailModel
    {
        bool HasPrimitive{false};
        PrimitiveSelectionResult Primitive{};
        bool HasFaceId{false};
        bool HasEdgeId{false};
        bool HasVertexId{false};
        bool HasPointId{false};
    };

    struct SandboxEditorSelectionModel
    {
        std::vector<std::uint32_t> SelectedStableIds{};
        std::vector<SandboxEditorEntityRow> SelectedEntities{};
        bool                       HasHovered{false};
        std::uint32_t              HoveredStableId{0u};
        bool                       HasHoveredEntity{false};
        SandboxEditorEntityRow     HoveredEntity{};
        SandboxEditorPrimitiveDetailModel Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    enum class SandboxEditorSceneFileOperation : std::uint8_t
    {
        Save,
        Load,
    };

    struct SandboxEditorSceneFileCommand
    {
        std::string Path{};
    };

    struct SandboxEditorSceneFileResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorSceneFileOperation Operation{SandboxEditorSceneFileOperation::Save};
        SceneSerializationStats Stats{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorSceneFileCommandSurface
    {
        std::function<SandboxEditorSceneFileResult(
            const SandboxEditorSceneFileCommand&)> Save{};
        std::function<SandboxEditorSceneFileResult(
            const SandboxEditorSceneFileCommand&)> Load{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Save) && static_cast<bool>(Load);
        }
    };

    struct SandboxEditorSceneFileModel
    {
        bool        Enabled{false};
        std::string PendingPath{};
        std::optional<SandboxEditorSceneFileResult> LastResult{};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorFileImportCommand
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    struct SandboxEditorFileImportResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorAssetImportCommandSurface
    {
        std::function<SandboxEditorFileImportResult(
            const SandboxEditorFileImportCommand&)> Import{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Import);
        }
    };

    struct SandboxEditorFileImportModel
    {
        bool        Enabled{false};
        std::string PendingPath{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::optional<SandboxEditorFileImportResult> LastResult{};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPrimitiveViewSettings
    {
        bool EnableEdgeView{false};
        bool EnableVertexView{false};

        [[nodiscard]] bool AnyEnabled() const noexcept
        {
            return EnableEdgeView || EnableVertexView;
        }
    };

    struct SandboxEditorPrimitiveViewCommandSurface
    {
        std::function<SandboxEditorPrimitiveViewSettings(std::uint32_t)> GetSettings{};
        std::function<void(std::uint32_t, SandboxEditorPrimitiveViewSettings)> SetSettings{};
        std::function<void(std::uint32_t)> ClearSettings{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetSettings) &&
                   static_cast<bool>(SetSettings) &&
                   static_cast<bool>(ClearSettings);
        }
    };

    struct SandboxEditorVisualizationAdapterBindingCommandSurface
    {
        std::function<std::optional<RenderExtractionCache::VisualizationAdapterBinding>(std::uint32_t)>
            GetBinding{};
        std::function<void(std::uint32_t, RenderExtractionCache::VisualizationAdapterBinding)>
            SetBinding{};
        std::function<void(std::uint32_t)> ClearBinding{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetBinding) &&
                   static_cast<bool>(SetBinding) &&
                   static_cast<bool>(ClearBinding);
        }
    };

    struct SandboxEditorCameraRenderModel
    {
        bool CameraControlsAvailable{false};
        bool RenderSettingsAvailable{false};
        bool PrimitiveViewControlsAvailable{false};
        bool HasMainCameraController{false};
        Core::Config::CameraControllerKind MainCameraControllerKind{
            Core::Config::CameraControllerKind::Orbit};
        bool HasPrimitiveViewEntity{false};
        std::uint32_t PrimitiveViewStableId{0u};
        SandboxEditorPrimitiveViewSettings PrimitiveView{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorSpatialDebugBindingModel
    {
        bool HasBinding{false};
        ECS::Components::SpatialDebugGeometryKind Kind{
            ECS::Components::SpatialDebugGeometryKind::Bvh};
        std::uint64_t RegistryKey{0u};
        bool LeafOnly{false};
        bool OccupancyOnly{false};
        std::uint32_t MaxDepth{32u};
    };

    struct SandboxEditorVisualizationConfigModel
    {
        bool HasConfig{false};
        Graphics::Components::VisualizationConfig::ColorSource Source{
            Graphics::Components::VisualizationConfig::ColorSource::Material};
        glm::vec4 Color{1.0f, 0.6f, 0.0f, 1.0f};
        std::string ScalarFieldName{};
        Graphics::Components::VisualizationConfig::Domain ScalarDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        std::string ColorBufferName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{0u};
    };

    struct SandboxEditorVisualizationAdapterBindingModel
    {
        bool HasBinding{false};
        std::uint64_t AdapterKey{0u};
        std::uint64_t BufferBDA{0u};
        RenderExtractionCache::VisualizationAdapterBindingKind Kind{
            RenderExtractionCache::VisualizationAdapterBindingKind::Scalar};
        VisualizationAdapterOptions Options{};
    };

    struct SandboxEditorVisualizationModel
    {
        bool GeometryDomainControlsAvailable{false};
        bool AdapterBindingControlsAvailable{false};
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        SandboxEditorSpatialDebugBindingModel SpatialDebug{};
        SandboxEditorVisualizationConfigModel Visualization{};
        SandboxEditorVisualizationAdapterBindingModel AdapterBinding{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorGeometryProcessingModel
    {
        bool HasSelectedEntity{false};
        SandboxEditorGeometryProcessingCapabilities Capabilities{};
        std::vector<SandboxEditorGeometryProcessingEntry> Entries{};
        std::vector<SandboxEditorGeometryProcessingDomain> KMeansDomains{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorDomainWindowModel
    {
        SandboxEditorDomainWindowKind Kind{SandboxEditorDomainWindowKind::Mesh};
        ECS::Components::GeometrySources::Domain ExpectedDomain{
            ECS::Components::GeometrySources::Domain::Mesh};
        bool HasSelectedEntity{false};
        SandboxEditorEntityRow SelectedEntity{};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        bool DomainMatches{false};
        SandboxEditorRenderHintModel RenderHints{};
        bool PrimitiveViewControlsAvailable{false};
        bool HasPrimitiveViewSettings{false};
        SandboxEditorPrimitiveViewSettings PrimitiveView{};
        bool VisualizationControlsAvailable{false};
        SandboxEditorVisualizationModel Visualization{};
        SandboxEditorGeometryProcessingModel Processing{};
        SandboxEditorPrimitiveDetailModel Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPanelFrame
    {
        std::vector<SandboxEditorEntityRow> Hierarchy{};
        SandboxEditorInspectorModel         Inspector{};
        SandboxEditorSelectionModel         Selection{};
        SandboxEditorSceneFileModel        SceneFile{};
        SandboxEditorFileImportModel        FileImport{};
        SandboxEditorCameraRenderModel      CameraRender{};
        SandboxEditorVisualizationModel     Visualization{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController*  Selection{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        SandboxEditorAssetImportCommandSurface AssetImportCommands{};
        SandboxEditorSceneFileCommandSurface SceneFileCommands{};
        SandboxEditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        SandboxEditorVisualizationAdapterBindingCommandSurface VisualizationAdapterBindings{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        const SandboxEditorFileImportResult* LastAssetImportResult{nullptr};
        const SandboxEditorSceneFileResult* LastSceneFileResult{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
    };

    struct SandboxEditorTransformEditCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetPosition{false};
        glm::vec3 Position{0.0f};
        bool SetRotation{false};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool SetScale{false};
        glm::vec3 Scale{1.0f};
    };

    struct SandboxEditorCameraControllerCommand
    {
        CameraControllerSlot Slot{CameraControllerSlot::Main};
        Core::Config::CameraControllerKind Kind{
            Core::Config::CameraControllerKind::Orbit};
        bool PreserveCurrentView{true};
        Core::Extent2D Viewport{};
    };

    struct SandboxEditorPrimitiveViewCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetEdgeView{false};
        bool EnableEdgeView{false};
        bool SetVertexView{false};
        bool EnableVertexView{false};
    };

    struct SandboxEditorSpatialDebugBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableBinding{true};
        ECS::Components::SpatialDebugGeometryKind Kind{
            ECS::Components::SpatialDebugGeometryKind::Bvh};
        std::uint64_t RegistryKey{0u};
        bool LeafOnly{false};
        bool OccupancyOnly{false};
        std::uint32_t MaxDepth{32u};
    };

    struct SandboxEditorVisualizationConfigCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableConfig{true};
        Graphics::Components::VisualizationConfig::ColorSource Source{
            Graphics::Components::VisualizationConfig::ColorSource::UniformColor};
        glm::vec4 Color{1.0f, 0.6f, 0.0f, 1.0f};
        std::string ScalarFieldName{};
        Graphics::Components::VisualizationConfig::Domain ScalarDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        std::string ColorBufferName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{0u};
    };

    struct SandboxEditorVisualizationAdapterBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableBinding{true};
        std::uint64_t AdapterKey{0u};
        std::uint64_t BufferBDA{0u};
        RenderExtractionCache::VisualizationAdapterBindingKind Kind{
            RenderExtractionCache::VisualizationAdapterBindingKind::Scalar};
        VisualizationAdapterOptions Options{};
    };

    [[nodiscard]] SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context);

    [[nodiscard]] SandboxEditorDomainWindowModel BuildSandboxEditorDomainWindowModel(
        const SandboxEditorContext& context,
        SandboxEditorDomainWindowKind kind);

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
                                   std::uint32_t stableEntityId);

    SandboxEditorFileImportResult ApplySandboxEditorFileImportCommand(
        const SandboxEditorContext& context,
        const SandboxEditorFileImportCommand& command);

    SandboxEditorSceneFileResult ApplySandboxEditorSceneSaveCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command);

    SandboxEditorSceneFileResult ApplySandboxEditorSceneLoadCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorTransformEdit(
        const SandboxEditorContext& context,
        const SandboxEditorTransformEditCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorCameraControllerCommand(
        const SandboxEditorContext& context,
        const SandboxEditorCameraControllerCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorPrimitiveViewCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPrimitiveViewCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorSpatialDebugBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSpatialDebugBindingCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationConfigCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationAdapterBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationAdapterBindingCommand& command);

    void DrawSandboxEditorPanelFrame(const SandboxEditorPanelFrame& frame);

    class SandboxEditorUi
    {
    public:
        SandboxEditorUi() = default;
        ~SandboxEditorUi();

        SandboxEditorUi(const SandboxEditorUi&)            = delete;
        SandboxEditorUi& operator=(const SandboxEditorUi&) = delete;
        SandboxEditorUi(SandboxEditorUi&&)                 = delete;
        SandboxEditorUi& operator=(SandboxEditorUi&&)      = delete;

        void Attach(Engine& engine);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept { return m_Engine != nullptr; }
        [[nodiscard]] const SandboxEditorPanelFrame& GetLastFrame() const noexcept
        {
            return m_LastFrame;
        }

    private:
        Engine*                 m_Engine{nullptr};
        SandboxEditorPanelFrame m_LastFrame{};
        std::array<char, 1024>  m_ImportPathBuffer{};
        std::array<char, 1024>  m_ScenePathBuffer{};
        std::array<bool, 12>    m_DomainWindowOpen{};
        std::optional<SandboxEditorFileImportResult> m_LastImportResult{};
        std::optional<SandboxEditorSceneFileResult> m_LastSceneFileResult{};
    };
}
