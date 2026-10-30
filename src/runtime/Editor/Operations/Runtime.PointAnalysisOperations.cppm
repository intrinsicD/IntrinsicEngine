// Point-analysis commands and copied results for keypoints and FPFH descriptors (density
// weights and outlier analysis moved to their operation modules, RUNTIME-353 and RUNTIME-354).
// These share one radius/neighborhood analysis mechanism: a resolved feature
// scale, a capped radius support row per sample, and named same-domain scalar
// outputs published in one undoable transaction.
module;
#include <array>
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
export import Extrinsic.Runtime.DensityWeightOperation;
export import Extrinsic.Runtime.DescriptorAnalysisConfig;
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
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorDescriptorAnalysisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        DescriptorAnalysisBackend RequestedBackend{DescriptorAnalysisBackend::CpuKDTree};
        std::array<GeometryPropertyRef,33> Outputs{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        // Resolved spacing and radii in source-property coordinate units.
        float MeanSpacing{}, FeatureRadius{};
        std::size_t MaximumNeighbors{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    enum class EditorPointAnalysisResultSlot : std::uint8_t { KeypointAnalysis, DescriptorAnalysis };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointAnalysisResultSinks
        {
            std::function<void(EditorPointAnalysisResultSlot)> DismissResult{};
            std::function<void(EditorKeypointAnalysisResult)> KeypointAnalysis{};
            std::function<void(EditorDescriptorAnalysisResult)> DescriptorAnalysis{};
        };
        struct EditorPointAnalysisResultsSnapshot
        {
            std::optional<EditorKeypointAnalysisResult> LastKeypointAnalysisResult{};
            std::optional<EditorDescriptorAnalysisResult> LastDescriptorAnalysisResult{};
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
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKeypointAnalysisConfig(const EditorProcessingCommands&, const KeypointAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KeypointAnalysisConfig> GetEditorKeypointAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorProcessingCommands&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});

    [[nodiscard]] ActionReadiness PreviewEditorDescriptorAnalysisCommand(const EditorProcessingCommands&, const DescriptorAnalysisConfig&);
    [[nodiscard]] EditorDescriptorAnalysisResult ApplyEditorDescriptorAnalysisCommand(const EditorProcessingCommands&, const DescriptorAnalysisConfig&, std::function<void(EditorDescriptorAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorDescriptorAnalysisConfig(const EditorProcessingCommands&, const DescriptorAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<DescriptorAnalysisConfig> GetEditorDescriptorAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorDescriptorAnalysisResult ApplyEditorConfiguredDescriptorAnalysis(const EditorProcessingCommands&, std::function<void(EditorDescriptorAnalysisResult)> onComplete = {});
}
