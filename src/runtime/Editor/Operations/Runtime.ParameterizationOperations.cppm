// UV regeneration, surface parameterization and the UV view surface, driven
// through shared processing commands.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

export module Extrinsic.Runtime.ParameterizationOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.ParameterizationConfig;
export import Geometry.Parameterization.Types;
export import Geometry.UvAtlas.Types;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;

export namespace Extrinsic::Runtime
{
    enum class EditorParameterizationUvViewStatus : std::uint8_t
    {
        Disabled,
        CpuLayout,
        CpuFallbackNonOperational,
        WaitingForGeometry,
        WaitingForGpuFrame,
        InvalidRequest,
        ResourceCreationFailed,
        Ready,
    };

    enum class EditorParameterizationTextureState : std::uint8_t
    {
        Unavailable, Pending, Ready, Failed, Stale,
    };

    struct EditorParameterizationTextureTab
    {
        std::string Name{};
        std::string Diagnostic{};
        std::uint64_t TextureAssetId{0u};
        std::uint64_t CoverageTextureAssetId{0u};
        GeometryPropertyRef ResolvedTexcoords{};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        EditorParameterizationTextureState State{EditorParameterizationTextureState::Unavailable};
        bool RawFloat{true};
        std::uint32_t Encoding{0u};
        std::uint32_t Colormap{0u};
        std::uint64_t Revision{0u};
    };

    struct EditorParameterizationUvViewRequest
    {
        bool Enabled{false};
        std::uint64_t RequestToken{0u};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        glm::vec2 UvBoundsMin{0.0f};
        glm::vec2 UvBoundsMax{1.0f};
        ParameterizationViewConfig View{};
        std::optional<EditorParameterizationTextureTab> Texture{};
        glm::vec2 ViewCenter{0.5f};
        float ViewHalfExtent{0.55f};
        std::vector<std::uint32_t> LineIndices{};
        std::vector<float> TriangleConformalDistortion{};
    };
    struct EditorParameterizationUvViewState
    {
        EditorParameterizationUvViewStatus Status{EditorParameterizationUvViewStatus::Disabled};
        ParameterizationUvRenderMode RequestedMode{ParameterizationUvRenderMode::CpuLayout};
        ParameterizationUvRenderMode ActiveMode{ParameterizationUvRenderMode::CpuLayout};
        ParameterizationUvBackgroundMode RequestedBackground{
            ParameterizationUvBackgroundMode::Grid};
        ParameterizationUvBackgroundMode ActiveBackground{ParameterizationUvBackgroundMode::Grid};
        bool HeatmapActive{false};
        bool GpuReady{false};
        std::uint64_t RequestToken{0u};
        std::uint32_t BindlessIndex{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint64_t TargetGeneration{0u};
        std::uint64_t RecordedPassCount{0u};
        std::string Message{};
    };


    struct EditorUvRegenerationCommand
    {
        std::uint32_t StableEntityId{0u};
        bool PreserveValidAuthoredUvs{false};
        bool ForceRegenerate{true};
        ParameterizationAtlasConfig Atlas{};
    };

    struct EditorUvRegenerationCommandResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::UvAtlas::UvAtlasStatus UvStatus{Geometry::UvAtlas::UvAtlasStatus::Success};
        Geometry::UvAtlas::UvAtlasProvenance Provenance{Geometry::UvAtlas::UvAtlasProvenance::None};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint32_t ChartCount{0u};
        // Extra vertices an indexed GPU vertex buffer needs to carry this
        // atlas's seams. Duplication happens once at upload; this is not a
        // count of vertices added to the mesh, whose topology is preserved
        // while the seam is carried on the corner domain.
        std::size_t SeamSplitVertexCount{0u};
        Geometry::UvAtlas::UvAtlasMethod RequestedMethod{Geometry::UvAtlas::UvAtlasMethod::FastStaged};
        Geometry::UvAtlas::UvAtlasMethod ActualMethod{Geometry::UvAtlas::UvAtlasMethod::None};
        Geometry::UvAtlas::UvAtlasDistortion RequestedDistortion{Geometry::UvAtlas::UvAtlasDistortion::Both};
        Geometry::UvAtlas::UvAtlasDistortion ActualDistortion{Geometry::UvAtlas::UvAtlasDistortion::None};
        bool UsedFallback{false};
        std::string FallbackReason{};
        std::uint32_t RegionCount{0u};
        std::uint32_t StableEntityId{0u};
        double MaxConformalDistortion{0.0};
        double MaxAreaDistortion{0.0};
        double MeanConformalDistortion{0.0};
        double MeanAreaDistortion{0.0};
        std::uint32_t RefinementSplitCount{0u};
        std::uint32_t SingleTriangleChartCount{0u};
        std::uint32_t UnconvergedChartCount{0u};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };
    using EditorParameterizationStrategy = ParameterizationStrategyKind;

    struct EditorParameterizationCommand
    {
        std::uint32_t StableEntityId{0u};
        ParameterizationConfig Config{};
    };

    struct EditorConfiguredParameterizationCommand
    {
        std::uint32_t StableEntityId{0u};
    };

    struct EditorParameterizationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::uint32_t StableEntityId{0u};
        EditorParameterizationStrategy Strategy{EditorParameterizationStrategy::Lscm};
        std::string StrategyToken{"lscm"};
        Geometry::Parameterization::ParameterizationStatus ParameterizationStatus{
            Geometry::Parameterization::ParameterizationStatus::InvalidInput};
        Geometry::Parameterization::ParameterizationDiagnostics Diagnostics{};
        // Structured cause behind a solver rejection. Populated
        // only when the solver ran and refused the mesh; a rejection raised
        // before the solver (stale entity, bad config, unusable source) leaves
        // it unevaluated because no mesh reached the solver.
        Geometry::Parameterization::ParameterizationRejection Rejection{};
        std::size_t VertexCount{0u};
        // Identifies the exact canonical triangle topology and UV payload that
        // produced Diagnostics. A missing value means the diagnostics must not
        // be projected onto the current UV view.
        // Exact provenance for face diagnostics: rendered topology-to-face
        // mapping, vertex positions, and UV coordinates.
        std::optional<std::uint64_t> DiagnosticInputFingerprint{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied &&
                   ParameterizationStatus ==
                       Geometry::Parameterization::ParameterizationStatus::Success;
        }
    };

    struct EditorParameterizationViewModel
    {
        bool HasSelectedEntity{false};
        bool SelectedEntityIsMesh{false};
        bool HasUvCoordinates{false};
        bool HasFiniteUvBounds{false};
        bool HasLastResult{false};
        bool GpuUvCompatible{true};
        GeometryPropertyRef ResolvedTexcoords{};
        std::uint32_t SelectedStableEntityId{0u};
        // Matches LastParameterizationResult only while every input consumed
        // by the position- and UV-dependent face diagnostics is unchanged.
        std::optional<std::uint64_t> DiagnosticInputFingerprint{};
        EditorParameterizationStrategy Strategy{EditorParameterizationStrategy::Lscm};
        ParameterizationViewConfig View{};
        std::optional<EditorParameterizationTextureTab> Texture{};
        glm::vec2 ViewCenter{0.5f};
        float ViewHalfExtent{0.55f};
        std::vector<EditorParameterizationTextureTab> TextureTabs{};
        std::vector<glm::vec2> UVs{};
        std::vector<std::array<std::uint32_t, 3u>> Triangles{};
        std::vector<std::uint32_t> LineIndices{};
        std::vector<float> TriangleConformalDistortion{};
        glm::vec2 UvBoundsMin{0.0f};
        glm::vec2 UvBoundsMax{0.0f};
        std::optional<Geometry::Parameterization::ParameterizationStatus> LastStatus{};
        std::optional<Geometry::Parameterization::ParameterizationDiagnostics> LastDiagnostics{};
        std::string Message{};
    };


    // Incomplete borrowed containers keep sibling workspace features independent
    // of parameterization records; prepared frames copy their values.
    extern "C++"
    {
        // The UV view is a family surface, not a general command framework: the
        // session owns one guarded instance and the prepared frame copies it.
        struct EditorParameterizationUvViewCommandSurface
        {
            std::function<std::vector<EditorParameterizationTextureTab>(std::uint32_t)> TextureTabs{};
            std::function<EditorParameterizationUvViewState(EditorParameterizationUvViewRequest)>
                Submit{};

            [[nodiscard]] bool Available() const noexcept
            {
                return static_cast<bool>(Submit);
            }
        };
        struct EditorParameterizationResultSinks
        {
            std::function<void()> DismissResult{};
            std::function<void()> DismissUvRegenerationResult{};
            std::function<void(EditorParameterizationResult)> Parameterization{};
            std::function<void(EditorUvRegenerationCommandResult)> UvRegeneration{};
        };
        struct EditorParameterizationResultsSnapshot
        {
            std::optional<EditorParameterizationResult> LastParameterizationResult{};
            std::optional<EditorUvRegenerationCommandResult> LastUvRegenerationResult{};
        };
    }

    struct EditorParameterizationPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorParameterizationUvViewCommandSurface UvViewCommands{};
        EditorParameterizationResultSinks ResultSinks{};
        EditorParameterizationResultsSnapshot Results{};
    };
    [[nodiscard]] EditorParameterizationPreparedFrame
    PrepareEditorParameterizationFrame(const EditorWorkspaceAttachment&);

    [[nodiscard]] const char*
    DebugNameForEditorUvAtlasStatus(Geometry::UvAtlas::UvAtlasStatus status) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorUvAtlasProvenance(Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept;
    [[nodiscard]] const char* DebugNameForEditorParameterizationUvViewStatus(
        EditorParameterizationUvViewStatus status) noexcept;
    [[nodiscard]] std::string_view
    StableTokenForEditorParameterizationStrategy(EditorParameterizationStrategy strategy) noexcept;

    // Cheap admission preview: session, parameters, source metadata and active jobs.
    // Finite values and complete topology are validated when the command prepares its mesh.
    [[nodiscard]] ActionReadiness PreviewEditorUvRegenerationCommand(
        const EditorProcessingCommands&, const EditorUvRegenerationCommand&);

    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorUvRegenerationCommandResult ApplyEditorUvRegenerationCommand(
        const EditorProcessingCommands&, const EditorUvRegenerationCommand&,
        std::function<void(EditorUvRegenerationCommandResult)> onComplete = {});
    [[nodiscard]] EditorParameterizationResult ApplyEditorParameterizationCommand(
        const EditorProcessingCommands&, const EditorParameterizationCommand&,
        std::function<void(EditorParameterizationResult)> onComplete = {});
    [[nodiscard]] EditorParameterizationResult ApplyEditorConfiguredParameterizationCommand(
        const EditorProcessingCommands&, const EditorConfiguredParameterizationCommand&,
        std::function<void(EditorParameterizationResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorParameterizationConfig(
        const EditorProcessingCommands&, const ParameterizationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<ParameterizationConfig> GetEditorParameterizationConfig(
        const EditorProcessingCommands&) noexcept;
    // Omitted entity follows scene selection; an explicit entity also owns UV
    // preview and diagnostics. The caller supplies the family's copied results
    // so the view model never reaches into a sibling workspace aggregate.
    [[nodiscard]] EditorParameterizationViewModel BuildEditorParameterizationViewModel(
        const EditorProcessingCommands&, const EditorParameterizationResultsSnapshot& results,
        std::optional<std::uint32_t> entity = std::nullopt);
    [[nodiscard]] EditorParameterizationUvViewState SubmitEditorParameterizationUvView(
        const EditorParameterizationUvViewCommandSurface&,
        const EditorParameterizationViewModel& model,
        std::uint32_t width, std::uint32_t height);
    void DisableEditorParameterizationUvView(const EditorParameterizationUvViewCommandSurface&);
}
