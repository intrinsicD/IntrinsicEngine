// Outlier analysis as a declared operation (RUNTIME-354, ADR 0031 Decision 7): its result, tag,
// GPU transaction and entry points. A module apart from Extrinsic.Runtime.OutlierAnalysisConfig, whose
// hand-written codec and config-only boundary stay as they are.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
export module Extrinsic.Runtime.OutlierAnalysisOperation;
export import Extrinsic.Runtime.OutlierAnalysisConfig;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
export namespace Extrinsic::Runtime
{
    struct EditorOutlierAnalysisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        OutlierAnalysisMethod Method{OutlierAnalysisMethod::Statistical};
        OutlierAnalysisBackend RequestedBackend{OutlierAnalysisBackend::CpuOctree};
        OutlierAnalysisOperation Operation{OutlierAnalysisOperation::Analyze};
        GeometryPropertyRef Mask{}, Score{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, RejectedCount{}, WrittenCount{};
        float MeanDistance{}, StdDevDistance{}, DistanceThreshold{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange;
        }
    };
    // The operation's identity for its stored results (RUNTIME-354; the config's mode enum already
    // holds the name OutlierAnalysisOperation).
    struct OutlierOperation
    {
        using Params = OutlierAnalysisConfig;
        using Result = EditorOutlierAnalysisResult;
    };
    struct EditorOutlierTransaction;
    using EditorOutlierTransactionHandle = std::shared_ptr<EditorOutlierTransaction>;
    struct EditorOutlierTransactionSnapshot
    {
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        bool CanAccept{};
        std::string AcceptDisabledReason{};
        EditorOutlierAnalysisResult Result{};
    };
    // Null with `failure` filled when refused; an active job on the same output answers Pending
    // with the shared "already has an active" message, as for the other GPU transactions.
    [[nodiscard]] EditorOutlierTransactionHandle StartEditorOutlierAnalysisTransaction(
        const EditorProcessingCommands&, const OutlierAnalysisConfig&, EditorOutlierAnalysisResult& failure);
    [[nodiscard]] EditorOutlierTransactionSnapshot SnapshotEditorOutlierAnalysis(
        const EditorProcessingCommands&, const EditorOutlierTransactionHandle&);
    [[nodiscard]] EditorOutlierAnalysisResult AcceptEditorOutlierAnalysis(const EditorProcessingCommands&,
        const EditorOutlierTransactionHandle&,
        std::function<void(EditorOutlierAnalysisResult)> onComplete = {});
    void DiscardEditorOutlierAnalysis(const EditorProcessingCommands&, const EditorOutlierTransactionHandle&);
    // Mock-device seam: captured inputs and an already completed pair of full-row fronts.
    [[nodiscard]] EditorOutlierTransactionHandle MakeEditorOutlierTransactionForTest(const EditorProcessingCommands&,
        const OutlierAnalysisConfig&,
        std::vector<float> scores,
        std::vector<std::uint32_t> mask,
        Graphics::GpuPropertyResidency&);

    // Apply returns immediate outcomes directly. onComplete receives only the terminal outcome of a
    // newly queued job while its attachment remains active (an inline outcome is only returned).
    // Pending for an already active output observes that job and registers no additional callback.
    // Every outcome is stored in the session's operation results, except a refusal that answers for
    // another run: that duplicate, or a GPU start whose output still holds an unaccepted result.
    // Configured Apply follows the same delivery contract.
    [[nodiscard]] ActionReadiness PreviewEditorOutlierAnalysisCommand(
        const EditorProcessingCommands&, const OutlierAnalysisConfig&);
    [[nodiscard]] EditorOutlierAnalysisResult ApplyEditorOutlierAnalysisCommand(const EditorProcessingCommands&,
        const OutlierAnalysisConfig&,
        std::function<void(EditorOutlierAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorOutlierAnalysisConfig(
        const EditorProcessingCommands&, const OutlierAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<OutlierAnalysisConfig> GetEditorOutlierAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorOutlierAnalysisResult ApplyEditorConfiguredOutlierAnalysis(
        const EditorProcessingCommands&, std::function<void(EditorOutlierAnalysisResult)> onComplete = {});
}
