// Point-set commands and copied results for bilateral filtering and progressive
// Poisson sampling. Both consume only the shared processing context, choose a
// CPU/GPU backend with truthful requested/actual/fallback reporting, and publish
// same-domain properties on the originating element domain without topology
// changes. Progressive Poisson uses the canonical serialized playground config.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
export module Extrinsic.Runtime.PointSetOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.BilateralFilterConfig;
export import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Geometry.PointCloud.Utils;
export namespace Extrinsic::Runtime
{
    struct EditorBilateralFilterResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        BilateralFilterBackend RequestedBackend{BilateralFilterBackend::CpuOctree};
        GeometryPropertyRef Output{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        float SpatialSigmaUsed{};
        Geometry::PointCloud::BilateralFilterResult Diagnostics{};
        std::uint32_t CompletedIterations{};
        std::size_t WorkspaceBuilds{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorBilateralFilterReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        BilateralFilterConfig Resolved{};
    };

    [[nodiscard]] const char*
    DebugNameForProgressivePoissonChannel(ProgressivePoissonPlaygroundChannel channel) noexcept;

    [[nodiscard]] const char*
    DebugNameForProgressivePoissonBackend(ProgressivePoissonPlaygroundBackend backend) noexcept;

    struct EditorProgressivePoissonCommand
    {
        std::uint32_t StableEntityId{0u};
        ProgressivePoissonPlaygroundConfig Config{};
    };

    struct EditorProgressivePoissonResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        ProgressivePoissonPlaygroundChannel Channel{ProgressivePoissonPlaygroundChannel::Level};
        std::uint32_t InputCount{0u};
        std::uint32_t AcceptedCount{0u};
        std::uint32_t PrefixCount{0u};
        std::uint32_t LevelCount{0u};
        ProgressivePoissonPlaygroundBackend RequestedBackend{
            ProgressivePoissonPlaygroundBackend::CpuReference};
        ProgressivePoissonPlaygroundBackend ActualBackend{
            ProgressivePoissonPlaygroundBackend::CpuReference};
        std::string RequestedBackendId{};
        std::string RequestedBackendDisplayName{};
        std::string BackendId{};
        std::string BackendDisplayName{};
        bool FellBackToCpu{false};
        std::string BackendFallbackReason{};
        std::vector<std::uint32_t> LevelAcceptedCounts{};
        float BaseRadius{0.0f};
        float UsedAlpha{0.0f};
        bool AlphaDefaulted{false};
        bool ClampedGridWidth{false};
        bool ClampedMaxLevels{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    enum class EditorProgressivePoissonConfigStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        MissingConfigControl,
        PreviewRejected,
        ApplyRejected,
    };

    struct EditorProgressivePoissonConfigCommand
    {
        ProgressivePoissonPlaygroundConfig Config{};
        std::string SourceId{"sandbox.progressive_poisson"};
    };

    struct EditorProgressivePoissonConfigResult
    {
        EditorProgressivePoissonConfigStatus Status{EditorProgressivePoissonConfigStatus::None};
        Core::Config::EngineConfigLoadResult Preview{};
        RuntimeEngineConfigApplyResult Apply{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorProgressivePoissonConfigStatus::Applied ||
                   Status == EditorProgressivePoissonConfigStatus::NoChange;
        }
    };

    enum class EditorPointSetResultSlot : std::uint8_t { BilateralFilter, ProgressivePoisson };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointSetResultSinks
        {
            std::function<void(EditorPointSetResultSlot)> DismissResult{};
            std::function<void(EditorBilateralFilterResult)> BilateralFilter{};
            std::function<void(EditorProgressivePoissonResult)> ProgressivePoisson{};
        };
        struct EditorPointSetResultsSnapshot
        {
            std::optional<EditorBilateralFilterResult> LastBilateralFilterResult{};
            std::optional<EditorProgressivePoissonResult> LastProgressivePoissonResult{};
        };
    }
    struct EditorPointSetPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorPointSetResultSinks ResultSinks{};
        EditorPointSetResultsSnapshot Results{};
    };
    [[nodiscard]] EditorPointSetPreparedFrame PrepareEditorPointSetFrame(const EditorWorkspaceAttachment&);

    // Apply returns immediate outcomes directly. onComplete receives only the
    // terminal outcome of a newly queued job while its attachment remains active.
    // Pending for an already active output observes that job and registers no
    // additional callback. Configured Apply follows the same delivery contract.
    [[nodiscard]] EditorBilateralFilterReadiness PreviewEditorBilateralFilterCommand(const EditorProcessingCommands&, const BilateralFilterConfig&);
    // Narrower than the shared point-input catalog: each candidate must also
    // supply count-matched normals and trial-capture a distinct filtered output.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorBilateralFilterInputCatalog(const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorBilateralFilterResult ApplyEditorBilateralFilterCommand(const EditorProcessingCommands&, const BilateralFilterConfig&, std::function<void(EditorBilateralFilterResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorBilateralFilterConfig(const EditorProcessingCommands&, const BilateralFilterConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<BilateralFilterConfig> GetEditorBilateralFilterConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorBilateralFilterResult ApplyEditorConfiguredBilateralFilter(const EditorProcessingCommands&, std::function<void(EditorBilateralFilterResult)> onComplete = {});

    [[nodiscard]] EditorProgressivePoissonResult ApplyEditorProgressivePoissonCommand(
        const EditorProcessingCommands&, const EditorProgressivePoissonCommand&,
        std::function<void(EditorProgressivePoissonResult)> onComplete = {});
    [[nodiscard]] EditorProgressivePoissonConfigResult ApplyEditorProgressivePoissonConfigCommand(
        const EditorProcessingCommands&, const EditorProgressivePoissonConfigCommand&);
    [[nodiscard]] std::optional<ProgressivePoissonPlaygroundConfig>
    GetEditorProgressivePoissonConfig(const EditorProcessingCommands&);
}
