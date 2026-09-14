// Point-analysis commands and copied results for weights, keypoints, outliers
// and FPFH descriptors. These share one radius/neighborhood analysis mechanism:
// a resolved feature scale, a capped radius support row per sample, and named
// same-domain scalar outputs published in one undoable transaction.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.PointAnalysisOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.OutlierAnalysisConfig;
export import Extrinsic.Runtime.KeypointAnalysisConfig;
export import Extrinsic.Runtime.DensityWeightConfig;
export import Extrinsic.Runtime.DescriptorAnalysisConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Geometry.PointCloud.Features;
import Geometry.PointCloud.Kernels;
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
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorOutlierAnalysisReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        OutlierAnalysisConfig Resolved{};
    };
    struct EditorKeypointAnalysisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        KeypointAnalysisBackend RequestedBackend{KeypointAnalysisBackend::CpuKDTree};
        GeometryPropertyRef Mask{}, Score{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, KeypointCount{}, WrittenCount{};
        Geometry::PointCloud::Features::KeypointScale Scale{};
        std::size_t MaximumNeighbors{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorKeypointAnalysisReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        KeypointAnalysisConfig Resolved{};
    };
    struct EditorDensityWeightResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        DensityWeightBackend RequestedBackend{DensityWeightBackend::CpuKDTree};
        GeometryPropertyRef Weights{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        Geometry::PointCloud::Kernels::DensityWeightDiagnostics Diagnostics{};
        float QueryRadius{}, MinWeight{}, MaxWeight{};
        std::size_t MaximumNeighbors{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorDensityWeightReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        DensityWeightConfig Resolved{};
    };
    struct EditorDescriptorAnalysisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        DescriptorAnalysisBackend RequestedBackend{DescriptorAnalysisBackend::CpuKDTree};
        std::array<GeometryPropertyRef,33> Outputs{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        Geometry::PointCloud::Features::DescriptorScale Scale{};
        std::size_t MaximumNeighbors{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorDescriptorAnalysisReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        DescriptorAnalysisConfig Resolved{};
    };
    enum class EditorPointAnalysisResultSlot : std::uint8_t { OutlierAnalysis, KeypointAnalysis, DensityWeight, DescriptorAnalysis };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointAnalysisResultSinks
        {
            std::function<void(EditorPointAnalysisResultSlot)> DismissResult{};
            std::function<void(EditorOutlierAnalysisResult)> OutlierAnalysis{};
            std::function<void(EditorKeypointAnalysisResult)> KeypointAnalysis{};
            std::function<void(EditorDensityWeightResult)> DensityWeight{};
            std::function<void(EditorDescriptorAnalysisResult)> DescriptorAnalysis{};
        };
        struct EditorPointAnalysisResultsSnapshot
        {
            std::optional<EditorOutlierAnalysisResult> LastOutlierAnalysisResult{};
            std::optional<EditorKeypointAnalysisResult> LastKeypointAnalysisResult{};
            std::optional<EditorDensityWeightResult> LastDensityWeightResult{};
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
    [[nodiscard]] EditorOutlierAnalysisReadiness PreviewEditorOutlierAnalysisCommand(const EditorProcessingCommands&, const OutlierAnalysisConfig&);
    [[nodiscard]] EditorOutlierAnalysisResult ApplyEditorOutlierAnalysisCommand(const EditorProcessingCommands&, const OutlierAnalysisConfig&, std::function<void(EditorOutlierAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorOutlierAnalysisConfig(const EditorProcessingCommands&, const OutlierAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<OutlierAnalysisConfig> GetEditorOutlierAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorOutlierAnalysisResult ApplyEditorConfiguredOutlierAnalysis(const EditorProcessingCommands&, std::function<void(EditorOutlierAnalysisResult)> onComplete = {});

    [[nodiscard]] EditorKeypointAnalysisReadiness PreviewEditorKeypointAnalysisCommand(const EditorProcessingCommands&, const KeypointAnalysisConfig&);
    [[nodiscard]] EditorKeypointAnalysisResult ApplyEditorKeypointAnalysisCommand(const EditorProcessingCommands&, const KeypointAnalysisConfig&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKeypointAnalysisConfig(const EditorProcessingCommands&, const KeypointAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KeypointAnalysisConfig> GetEditorKeypointAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorProcessingCommands&, std::function<void(EditorKeypointAnalysisResult)> onComplete = {});

    [[nodiscard]] EditorDensityWeightReadiness PreviewEditorDensityWeightCommand(const EditorProcessingCommands&, const DensityWeightConfig&);
    [[nodiscard]] EditorDensityWeightResult ApplyEditorDensityWeightCommand(const EditorProcessingCommands&, const DensityWeightConfig&, std::function<void(EditorDensityWeightResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorDensityWeightConfig(const EditorProcessingCommands&, const DensityWeightConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<DensityWeightConfig> GetEditorDensityWeightConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(const EditorProcessingCommands&, std::function<void(EditorDensityWeightResult)> onComplete = {});

    [[nodiscard]] EditorDescriptorAnalysisReadiness PreviewEditorDescriptorAnalysisCommand(const EditorProcessingCommands&, const DescriptorAnalysisConfig&);
    [[nodiscard]] EditorDescriptorAnalysisResult ApplyEditorDescriptorAnalysisCommand(const EditorProcessingCommands&, const DescriptorAnalysisConfig&, std::function<void(EditorDescriptorAnalysisResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorDescriptorAnalysisConfig(const EditorProcessingCommands&, const DescriptorAnalysisConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<DescriptorAnalysisConfig> GetEditorDescriptorAnalysisConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorDescriptorAnalysisResult ApplyEditorConfiguredDescriptorAnalysis(const EditorProcessingCommands&, std::function<void(EditorDescriptorAnalysisResult)> onComplete = {});
}
