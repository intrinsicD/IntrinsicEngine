module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.VisualizationEditingOperations;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime
{
    struct EditorVisualizationEditingCommandsAccess;
}

export namespace Extrinsic::Runtime
{
    [[nodiscard]] const char* DebugNameForEditorVisualizationColorSource(
        Graphics::Components::VisualizationConfig::ColorSource source) noexcept;

    [[nodiscard]] const char* DebugNameForEditorVisualizationDomain(
        Graphics::Components::VisualizationConfig::Domain domain) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorVisualizationRecipeKind(VisualizationRecipeKind kind) noexcept;
    enum class EditorVisualizationPropertyDomain : std::uint8_t
    {
        MeshVertices,
        MeshEdges,
        MeshFaces,
        GraphVertices,
        GraphEdges,
        PointCloudPoints,
    };
    enum class EditorVisualizationPropertyPreset : std::uint8_t
    {
        Scalar,
        Isoline,
        ColorBuffer,
    };
    enum class EditorVisualizationTarget : std::uint8_t
    {
        Entity,
        Surface,
        Edges,
        Points,
    };

    [[nodiscard]] const char* DebugNameForEditorVisualizationPropertyDomain(
        EditorVisualizationPropertyDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForEditorVisualizationPropertyPreset(
        EditorVisualizationPropertyPreset preset) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorVisualizationTarget(EditorVisualizationTarget target) noexcept;
    struct EditorRenderHintModel
    {
        bool HasRenderSurface{false};
        std::string SurfaceDomain{};
        Graphics::Components::RenderSurface::SourceDomain SurfaceDomainValue{
            Graphics::Components::RenderSurface::SourceDomain::Vertex};
        bool HasRenderEdges{false};
        std::string EdgeDomain{};
        Graphics::Components::RenderEdges::SourceDomain EdgeDomainValue{
            Graphics::Components::RenderEdges::SourceDomain::Vertex};
        bool HasUniformEdgeWidth{false};
        float UniformEdgeWidth{0.0f};
        bool HasNamedEdgeWidth{false};
        std::string EdgeWidthName{};
        bool HasRenderPoints{false};
        std::string PointRenderType{};
        Graphics::Components::RenderPoints::RenderType PointRenderTypeValue{
            Graphics::Components::RenderPoints::RenderType::Sphere};
        bool HasUniformPointSize{false};
        float UniformPointSize{0.0f};
        bool HasNamedPointSize{false};
        std::string PointSizeName{};
    };
    struct EditorGeometryPresentationPropertyOptionModel
    {
        GeometryPropertyRef Descriptor{};
        Geometry::PropertyValueKind ActualValueKind{Geometry::PropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
        bool Compatible{false};
        std::string DisabledReason{};
    };
    enum class EditorPropertyCatalogDomain : std::uint8_t
    {
        MeshVertices,
        MeshEdges,
        MeshHalfedges,
        MeshFaces,
        GraphVertices,
        GraphEdges,
        PointCloudPoints,
    };

    [[nodiscard]] const char*
    DebugNameForEditorPropertyCatalogDomain(EditorPropertyCatalogDomain domain) noexcept;
    struct EditorPropertyValuePreview
    {
        bool HasValue{false};
        std::size_t ElementIndex{0u};
        std::string Text{};
    };
    struct EditorPropertyCatalogRow
    {
        std::string Name{};
        EditorPropertyCatalogDomain Domain{EditorPropertyCatalogDomain::MeshVertices};
        Geometry::PropertyValueKind ValueKind{Geometry::PropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
        std::uint8_t ComponentCount{0u};
        bool Supported{false};
        bool Bindable{false};
        bool Canonical{false};
        bool Internal{false};
        bool Connectivity{false};
        bool Generated{false};
        std::string UnsupportedReason{};
        GeometryPropertyRef Descriptor{};
        EditorPropertyValuePreview Preview{};
    };
    struct EditorPropertyBindingTargetModel
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};
        GeometryPresentationKind PresentationKind{GeometryPresentationKind::SurfaceMaterial};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{GeometryPresentationSourceKind::UniformDefault};
        GeometryElementDomain RequiredDomain{GeometryElementDomain::Unknown};
        GeometryPropertyValueKindFilter ExpectedValueKind{};
        std::size_t ExpectedElementCount{0u};
        std::vector<EditorGeometryPresentationPropertyOptionModel> Options{};
    };
    struct EditorVertexChannelBindingOptionModel
    {
        std::string PropertyName{};
        EditorPropertyCatalogDomain Domain{EditorPropertyCatalogDomain::MeshVertices};
        Geometry::PropertyValueKind ValueKind{Geometry::PropertyValueKind::Unknown};
        AttributeSourceType SourceType{AttributeSourceType::Vec3};
        std::size_t ElementCount{0u};
        AttributeBindResult Resolver{};
        bool Compatible{false};
        std::string DisabledReason{};
    };
    struct EditorVertexChannelBindingTargetModel
    {
        VertexChannel Channel{VertexChannel::Normal};
        bool HasBinding{false};
        VertexChannelSourceBinding Binding{};
        AttributeBindResult Resolver{};
        std::string Diagnostic{};
        std::vector<EditorVertexChannelBindingOptionModel> Options{};
    };

    struct EditorPropertyCatalogModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        std::vector<EditorPropertyCatalogRow> Rows{};
        std::vector<EditorPropertyBindingTargetModel> BindingTargets{};
        std::vector<EditorVertexChannelBindingTargetModel> VertexChannelTargets{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorGeometryPresentationSlotModel
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};
        GeometryPresentationKind PresentationKind{GeometryPresentationKind::SurfaceMaterial};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{GeometryPresentationSourceKind::UniformDefault};
        GeometryPresentationReadiness Readiness{GeometryPresentationReadiness::Unset};
        GeometryPresentationDefaultValue UniformDefault{};
        GeometryPropertyRef Property{};
        GeometryPropertyResolution PropertyResolution{};
        Assets::AssetId AuthoredTexture{};
        Assets::AssetId GeneratedTexture{};
        Assets::AssetId TextureAsset{};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        std::string Diagnostic{};
        std::vector<EditorGeometryPresentationPropertyOptionModel> PropertyOptions{};
    };

    struct EditorGeometryCompositionSummary
    {
        bool HasChildren{false};
        std::uint32_t ChildCount{0u};
        std::uint32_t ChildRecipeCount{0u};
        std::uint32_t ChildSlotCount{0u};
        std::uint32_t ChildPendingSlotCount{0u};
        std::uint32_t ChildFailedSlotCount{0u};
        std::uint32_t ChildJobCount{0u};
        std::uint32_t ChildActiveJobCount{0u};
        std::uint32_t ChildFailedJobCount{0u};
    };

    struct EditorGeometryPresentationModel
    {
        bool HasRecipe{false};
        GeometryPresentationShape Shape{GeometryPresentationShape::Unknown};
        std::uint64_t RecipeGeneration{0u};
        GeometryPresentationSnapshotStats Stats{};
        std::vector<EditorGeometryPresentationSlotModel> Slots{};
        std::vector<EditorJobModel> Jobs{};
        EditorGeometryCompositionSummary Composition{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    enum class EditorBoundRenderStateRowKind : std::uint8_t
    {
        RenderHint,
        GeometryPresentationSlot,
        DerivedJob,
        CompositionSummary,
        DisabledCommand,
    };

    [[nodiscard]] const char*
    DebugNameForEditorBoundRenderStateRowKind(EditorBoundRenderStateRowKind kind) noexcept;

    struct EditorBoundRenderStateRow
    {
        EditorBoundRenderStateRowKind Kind{EditorBoundRenderStateRowKind::GeometryPresentationSlot};
        std::string Label{};
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};
        GeometryPresentationKind PresentationKind{GeometryPresentationKind::SurfaceMaterial};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{GeometryPresentationSourceKind::UniformDefault};
        GeometryPresentationReadiness Readiness{GeometryPresentationReadiness::Unset};
        GeometryPropertyRef Property{};
        GeometryPropertyResolution PropertyResolution{};
        Assets::AssetId AuthoredTexture{};
        Assets::AssetId GeneratedTexture{};
        Assets::AssetId TextureAsset{};
        JobToken Job{};
        JobState JobStatus{JobState::Queued};
        float JobProgress{0.0f};
        bool JobProgressDeterminate{true};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        bool HasCatalogMatch{false};
        std::optional<std::size_t> CatalogRowIndex{};
        std::string SourceDescription{};
        std::string DisabledReason{};
        std::string Diagnostic{};
    };

    struct EditorBoundRenderStateModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        GeometryPresentationShape Shape{GeometryPresentationShape::Unknown};
        std::uint64_t RecipeGeneration{0u};
        std::vector<EditorBoundRenderStateRow> Rows{};
        EditorGeometryCompositionSummary Composition{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    enum class EditorTextureBakeSourceCategory : std::uint8_t
    {
        Bakeable,
        Internal,
        Connectivity,
        Unsupported,
        WrongDomain,
    };

    struct EditorTextureBakeSourceRow
    {
        std::string Name{};
        EditorPropertyCatalogDomain CatalogDomain{EditorPropertyCatalogDomain::MeshVertices};
        GeometryElementDomain BakeDomain{GeometryElementDomain::Unknown};
        Geometry::PropertyValueKind ValueKind{Geometry::PropertyValueKind::Unknown};
        GeometryPropertyValueKindFilter ExpectedValueKind{};
        std::size_t ElementCount{0u};

        // The bake representation APIs need a resolved kind, not a
        // constraint. An unconstrained expectation resolves to `Unknown`,
        // which those APIs already treat as "no usable representation" —
        // the same branch the retired `Any` enumerator shared with `Unknown`.
        [[nodiscard]] Geometry::PropertyValueKind ResolvedExpectedValueKind() const noexcept
        {
            return ExpectedValueKind.value_or(Geometry::PropertyValueKind::Unknown);
        }

        EditorTextureBakeSourceCategory Category{EditorTextureBakeSourceCategory::Unsupported};
        bool Bakeable{false};
        std::string DisabledReason{};
        GeometryPropertyRef Descriptor{};
    };

    struct EditorTextureBakeTarget
    {
        std::string PresentationKey{};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        Graphics::Colormap::Type Colormap{Graphics::Colormap::Type::Viridis};
        GeometryPresentationNormalSpace NormalSpace{GeometryPresentationNormalSpace::Object};
    };

    [[nodiscard]] bool IsEditorTextureBakeTargetCompatible(
        const EditorTextureBakeTarget& target, Geometry::PropertyValueKind valueKind,
        PropertyTextureBakeStorage storage, PropertyTextureBakeEncoding encoding) noexcept;
    [[nodiscard]] PropertyTextureBakeRepresentation ResolveEditorTextureBakeTargetRepresentation(
        Geometry::PropertyValueKind valueKind, PropertyTextureBakeStorage requestedStorage,
        PropertyTextureBakeEncoding requestedEncoding,
        std::span<const EditorTextureBakeTarget> targets) noexcept;

    struct EditorTextureBakeTargetSnapshot
    {
        std::string OutputName{};
        std::vector<EditorTextureBakeTarget> Targets{};
    };

    struct EditorTextureBakeTargetUpdateRequest
    {
        std::uint32_t StableEntityId{0u};
        std::string OutputName{};
        std::vector<EditorTextureBakeTarget> Targets{};
    };

    struct EditorTextureBakeControlsModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        bool IsMesh{false};
        bool HasRuntimeBakeCommand{false};
        bool CanBake{false};
        std::string DisabledReason{};
        GeometryPresentationSlotSemantic DefaultTargetSemantic{
            GeometryPresentationSlotSemantic::Albedo};
        PropertyTextureBakeEncoding DefaultEncoder{PropertyTextureBakeEncoding::Auto};
        std::uint32_t DefaultWidth{64u};
        std::uint32_t DefaultHeight{64u};
        EditorUvDiagnosticsModel Uv{};
        std::vector<EditorTextureBakeSourceRow> Sources{};
        std::vector<PropertyTextureBakeRecord> BakedTextures{};
        std::vector<EditorTextureBakeTargetSnapshot> TextureBakeTargets{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorVisualizationRecipeCommandSurface
    {
        std::function<std::optional<VisualizationRecipe>(std::uint32_t)> GetRecipe{};
        std::function<void(std::uint32_t, VisualizationRecipe)> SetRecipe{};
        std::function<void(std::uint32_t)> ClearRecipe{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetRecipe) && static_cast<bool>(SetRecipe) &&
                   static_cast<bool>(ClearRecipe);
        }
    };
    struct EditorVisualizationConfigModel
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
        // UI-032 — scalar styling controls.
        Graphics::Colormap::Type ScalarColormap{Graphics::Colormap::Type::Viridis};
        float IsolineWidth{1.5f};
        glm::vec4 IsolineColor{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, Graphics::Components::ScalarFieldConfig::kMaxIsolineValues>
            IsolineValues{};
        std::uint32_t IsolineValueCount{0u};
    };
    struct EditorVisualizationPropertyInfo
    {
        std::string Name{};
        EditorVisualizationPropertyDomain Domain{EditorVisualizationPropertyDomain::MeshVertices};
        Geometry::PropertyValueKind ValueKind{Geometry::PropertyValueKind::Float};
        std::size_t ElementCount{0u};
        bool ScalarPresetAvailable{false};
        bool IsolinePresetAvailable{false};
        bool ColorBufferPresetAvailable{false};
        bool VectorFieldCandidate{false};
    };
    struct EditorVisualizationRecipeModel
    {
        bool HasRecipe{false};
        VisualizationRecipeKind Kind{VisualizationRecipeKind::Empty};
        VisualizationRecipe Recipe{};
    };
    struct EditorVisualizationModel
    {
        bool GeometryDomainControlsAvailable{false};
        bool RecipeControlsAvailable{false};
        bool TargetAvailable{false};
        EditorVisualizationTarget Target{EditorVisualizationTarget::Entity};
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        EditorVisualizationConfigModel Visualization{};
        std::vector<EditorVisualizationPropertyInfo> Properties{};
        EditorVisualizationRecipeModel Recipe{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorRenderHintCommand
    {
        std::uint32_t StableEntityId{0u};

        bool SetSurface{false};
        bool EnableSurface{false};
        Graphics::Components::RenderSurface::SourceDomain SurfaceDomain{
            Graphics::Components::RenderSurface::SourceDomain::Vertex};

        bool SetEdges{false};
        bool EnableEdges{false};
        Graphics::Components::RenderEdges::SourceDomain EdgeDomain{
            Graphics::Components::RenderEdges::SourceDomain::Vertex};
        bool SetUniformEdgeWidth{false};
        float UniformEdgeWidth{1.0f};

        bool SetPoints{false};
        bool EnablePoints{false};
        Graphics::Components::RenderPoints::RenderType PointType{
            Graphics::Components::RenderPoints::RenderType::Sphere};
        bool SetPointRenderType{false};
        bool SetUniformPointSize{false};
        float UniformPointSize{6.0f};
    };
    struct EditorVisualizationConfigCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorVisualizationTarget Target{EditorVisualizationTarget::Entity};
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
        // UI-032 — scalar styling controls.
        Graphics::Colormap::Type ScalarColormap{Graphics::Colormap::Type::Viridis};
        float IsolineWidth{1.5f};
        glm::vec4 IsolineColor{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, Graphics::Components::ScalarFieldConfig::kMaxIsolineValues>
            IsolineValues{};
        std::uint32_t IsolineValueCount{0u};
    };
    struct EditorVisualizationPropertyCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorVisualizationTarget Target{EditorVisualizationTarget::Entity};
        EditorVisualizationPropertyDomain Domain{EditorVisualizationPropertyDomain::MeshVertices};
        EditorVisualizationPropertyPreset Preset{EditorVisualizationPropertyPreset::Scalar};
        std::string PropertyName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{12u};
    };
    struct EditorVisualizationRecipeCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableRecipe{true};
        VisualizationRecipe Recipe{};
    };
    struct EditorVertexChannelBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        VertexChannel Channel{VertexChannel::Normal};
        bool EnableBinding{true};
        std::string PropertyName{};
    };
    struct EditorGeometryPresentationSlotDefaultCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationDefaultValue Value{};
        bool Enabled{true};
    };
    struct EditorGeometryPresentationSlotPropertyCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{};
        GeometryPresentationSlotSemantic Semantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{GeometryPresentationSourceKind::PropertyBake};
        GeometryElementDomain Domain{GeometryElementDomain::Unknown};
        GeometryPropertyValueKindFilter ExpectedValueKind{};
        std::string PropertyName{};
    };
    struct EditorTextureBakeCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{"mesh.surface"};
        GeometryPresentationSlotSemantic TargetSemantic{GeometryPresentationSlotSemantic::Albedo};
        GeometryElementDomain SourceDomain{GeometryElementDomain::MeshVertex};
        GeometryPropertyValueKindFilter ExpectedValueKind{};
        std::string PropertyName{};
        PropertyTextureBakeEncoding Encoder{PropertyTextureBakeEncoding::Auto};
        PropertyTextureBakeRangePolicy RangePolicy{PropertyTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        std::uint32_t PaddingTexels{0u};
        std::string GeneratedKey{};
        std::string OutputName{};
        PropertyTextureBakeStorage Storage{PropertyTextureBakeStorage::Auto};
        Graphics::Colormap::Type EncodingColormap{Graphics::Colormap::Type::Viridis};
        GeometryPresentationNormalSpace NormalSpace{GeometryPresentationNormalSpace::Object};
        std::vector<EditorTextureBakeTarget> Targets{};
        bool BindGeneratedTexture{true};
    };
    struct EditorTextureBakeCommandResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        PropertyTextureBakeStatus BakeStatus{PropertyTextureBakeStatus::Success};
        Assets::AssetId GeneratedTexture{};
        // The selected-mesh bake runs on `JobService`.
        JobToken Job{};
        bool Scheduled{false};
        bool BoundGeneratedTexture{false};
        std::string GeneratedAssetPath{};
        std::string OutputName{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied ||
                   Status == EditorCommandStatus::NoChange;
        }
    };
    struct EditorVisualizationEditingContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        SelectionController* Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        TextureBakeService* TextureBake{nullptr};
        EditorVisualizationRecipeCommandSurface VisualizationRecipes{};
        std::uint64_t VisualizationRecipeRevision{0u};
        EditorJobCommandSurface JobCommands{};
        EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<void()> InvalidateWorkspaceSnapshotCache{};
        bool VisualizationCommandsAvailable{false};
    };

    class EditorVisualizationEditingCommands final
    {
    public:
        EditorVisualizationEditingCommands() = default;

        [[nodiscard]] bool IsBound() const noexcept;


    private:
        struct State;
        std::shared_ptr<const State> m_State{};

        explicit EditorVisualizationEditingCommands(std::shared_ptr<const State> state);
        friend struct EditorVisualizationEditingCommandsAccess;
        friend EditorVisualizationEditingCommands
        BindEditorVisualizationEditingCommands(EditorVisualizationEditingContext context);
    };

    [[nodiscard]] EditorVisualizationEditingCommands
    BindEditorVisualizationEditingCommands(EditorVisualizationEditingContext context);

    struct EditorVisualizationEditingPreparedFrame
    {
        EditorVisualizationEditingCommands Commands{};
    };

    [[nodiscard]] EditorVisualizationEditingPreparedFrame
    PrepareEditorVisualizationEditingFrame(
        const EditorWorkspaceAttachment& attachment);

    [[nodiscard]] bool
    IsEditorTextureBakeServiceAttached(const EditorVisualizationEditingContext& context) noexcept;

    EditorCommandStatus
    ApplyEditorRenderHintCommand(const EditorVisualizationEditingContext& context,
                                 const EditorRenderHintCommand& command);

    EditorCommandStatus
    ApplyEditorVisualizationConfigCommand(const EditorVisualizationEditingContext& context,
                                          const EditorVisualizationConfigCommand& command);

    EditorCommandStatus
    ApplyEditorVisualizationPropertyCommand(const EditorVisualizationEditingContext& context,
                                            const EditorVisualizationPropertyCommand& command);

    EditorCommandStatus
    ApplyEditorVisualizationRecipeCommand(const EditorVisualizationEditingContext& context,
                                          const EditorVisualizationRecipeCommand& command);

    EditorCommandStatus
    ApplyEditorVertexChannelBindingCommand(const EditorVisualizationEditingContext& context,
                                           const EditorVertexChannelBindingCommand& command);

    EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommand(
        const EditorVisualizationEditingContext& context,
        const EditorGeometryPresentationSlotDefaultCommand& command);

    EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommand(
        const EditorVisualizationEditingContext& context,
        const EditorGeometryPresentationSlotPropertyCommand& command);

    EditorTextureBakeCommandResult
    ApplyEditorTextureBakeCommand(const EditorVisualizationEditingContext& context,
                                  const EditorTextureBakeCommand& command);

    TextureBakeMutationResult
    RenameEditorBakedTexture(const EditorVisualizationEditingContext& context,
                             std::uint32_t stableEntityId, std::string_view currentName,
                             std::string_view newName);

    TextureBakeMutationResult
    RemoveEditorBakedTexture(const EditorVisualizationEditingContext& context,
                             std::uint32_t stableEntityId, std::string_view outputName);

    TextureBakeMutationResult
    SetEditorBakedTextureTargets(const EditorVisualizationEditingContext& context,
                                 const EditorTextureBakeTargetUpdateRequest& request);

    [[nodiscard]] bool IsEditorTextureBakeServiceAttached(
        const EditorVisualizationEditingCommands& commands) noexcept;
    EditorCommandStatus ApplyEditorRenderHintCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorRenderHintCommand& command);
    EditorCommandStatus ApplyEditorVisualizationConfigCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorVisualizationConfigCommand& command);
    EditorCommandStatus ApplyEditorVisualizationPropertyCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorVisualizationPropertyCommand& command);
    EditorCommandStatus ApplyEditorVisualizationRecipeCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorVisualizationRecipeCommand& command);
    EditorCommandStatus ApplyEditorVertexChannelBindingCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorVertexChannelBindingCommand& command);
    EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorGeometryPresentationSlotDefaultCommand& command);
    EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorGeometryPresentationSlotPropertyCommand& command);
    EditorTextureBakeCommandResult ApplyEditorTextureBakeCommand(
        const EditorVisualizationEditingCommands& commands,
        const EditorTextureBakeCommand& command);
    TextureBakeMutationResult RenameEditorBakedTexture(
        const EditorVisualizationEditingCommands& commands,
        std::uint32_t stableEntityId,
        std::string_view currentName,
        std::string_view newName);
    TextureBakeMutationResult RemoveEditorBakedTexture(
        const EditorVisualizationEditingCommands& commands,
        std::uint32_t stableEntityId,
        std::string_view outputName);
    TextureBakeMutationResult SetEditorBakedTextureTargets(
        const EditorVisualizationEditingCommands& commands,
        const EditorTextureBakeTargetUpdateRequest& request);
} // namespace Extrinsic::Runtime

namespace Extrinsic::Runtime
{
    struct EditorVisualizationEditingCommandsAccess final
    {
        [[nodiscard]] static const EditorVisualizationEditingContext*
        Resolve(const EditorVisualizationEditingCommands& commands) noexcept;
    };
}
