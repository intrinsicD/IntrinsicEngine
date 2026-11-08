// Point-analysis commands and copied results for keypoints: a resolved feature
// scale, a capped radius support row per sample, and named same-domain scalar
// outputs published in one undoable transaction.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.PointAnalysisOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.OutlierAnalysisOperation;
export import Extrinsic.Runtime.KeypointAnalysisConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    struct EditorKeypointAnalysisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        KeypointAnalysisBackend RequestedBackend{KeypointAnalysisBackend::CpuKDTree};
        GeometryPropertyRef Mask{}, Score{};
        std::string ActualBackend{}, Message{}, ImplementationId{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageUploadBytes{}, CpuStageReadbackBytes{};
        std::size_t SlotCount{}, LiveCount{}, KeypointCount{}, WrittenCount{};
        // Resolved spacing and radii in source-property coordinate units.
        float MeanSpacing{}, SalientRadius{}, NonMaxRadius{};
        std::size_t MaximumNeighbors{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        // ARCH-023: the run's step run (run_control, run_observe; one step, its bounded pages within); 0 for an
        // inline or job-chained run and a refusal.
        std::uint64_t StepRun{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    enum class EditorPointAnalysisResultSlot : std::uint8_t { KeypointAnalysis };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointAnalysisResultSinks
        {
            std::function<void(EditorPointAnalysisResultSlot)> DismissResult{};
            std::function<void(EditorKeypointAnalysisResult)> KeypointAnalysis{};
        };
        struct EditorPointAnalysisResultsSnapshot
        {
            std::optional<EditorKeypointAnalysisResult> LastKeypointAnalysisResult{};
        };
    }
    struct EditorPointAnalysisPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorPointAnalysisResultSinks ResultSinks{};
        EditorPointAnalysisResultsSnapshot Results{};
    };
    [[nodiscard]] EditorPointAnalysisPreparedFrame PrepareEditorPointAnalysisFrame(const EditorWorkspaceAttachment&);

    // Apply returns immediate outcomes directly. onComplete receives only the
    // terminal outcome of a newly queued job while its attachment remains active.
    // Pending for an already active output observes that job and registers no
    // additional callback. Configured Apply follows the same delivery contract.
    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorKeypointAnalysisTransaction(
        const EditorProcessingCommands&, const KeypointAnalysisConfig&, EditorKeypointAnalysisResult&,
        std::function<void(EditorKeypointAnalysisResult)> onComplete = {}, bool automatic = false);
    [[nodiscard]] ActionReadiness PreviewEditorKeypointAnalysisCommand(const EditorProcessingCommands&, const KeypointAnalysisConfig&);
    [[nodiscard]] EditorKeypointAnalysisResult ApplyEditorKeypointAnalysisCommand(const EditorProcessingCommands&, const KeypointAnalysisConfig&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});
    // ARCH-023: an interactive start of the CPU and Vulkan LBVH backends (Vulkan compute: its transaction) with the
    // section's execution (`config.Execution`: start and auto_accept), a one-shot step run that run_control and the
    // panel's row drive; it needs the session's step runs and a job lane. Apply is the batch (runs at once, accepts at
    // its end).
    [[nodiscard]] EditorKeypointAnalysisResult StartEditorKeypointAnalysisRun(const EditorProcessingCommands&,
        const KeypointAnalysisConfig&,
        std::function<void(EditorKeypointAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKeypointAnalysisConfig(const EditorProcessingCommands&, const KeypointAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KeypointAnalysisConfig> GetEditorKeypointAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorProcessingCommands&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});
    [[nodiscard]] EditorKeypointAnalysisResult StartEditorConfiguredKeypointAnalysis(
        const EditorProcessingCommands&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});
}
