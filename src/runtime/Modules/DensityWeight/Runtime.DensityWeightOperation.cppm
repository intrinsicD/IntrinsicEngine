// Compact density weights as a declared operation (RUNTIME-353, ADR 0031 Decision 7): its result, tag and
// entry points. A module apart from Extrinsic.Runtime.DensityWeightConfig, whose hand-written codec and
// config-only boundary stay as they are.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
export module Extrinsic.Runtime.DensityWeightOperation;
export import Extrinsic.Runtime.DensityWeightConfig;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
export namespace Extrinsic::Runtime
{
    struct EditorDensityWeightResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        DensityWeightBackend RequestedBackend{DensityWeightBackend::CpuKDTree};
        GeometryPropertyRef Weights{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        Geometry::PointCloud::Kernels::DensityWeightDiagnostics Diagnostics{};
        // Resolved compact-support radius in source-property units (configured, or derived when 0).
        double SupportRadius{};
        float QueryRadius{}, MinWeight{}, MaxWeight{};
        std::size_t MaximumNeighbors{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange;
        }
    };
    // The operation's identity for its stored results.
    struct DensityWeightOperation
    {
        using Params = DensityWeightConfig;
        using Result = EditorDensityWeightResult;
    };
    // Apply returns immediate outcomes directly. onComplete receives only the terminal outcome of a newly
    // queued job while its attachment remains active (an inline outcome is only returned). Pending for an
    // already active output observes that job and registers no additional callback. Every outcome but
    // that duplicate refusal is stored in the session's operation results. Configured Apply follows the
    // same delivery contract.
    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorDensityWeightTransaction(
        const EditorProcessingCommands&,
        const DensityWeightConfig&,
        EditorDensityWeightResult&,
        std::function<void(EditorDensityWeightResult)> onComplete = {});
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorDensityWeightTransactionForTest(
        const EditorProcessingCommands&,
        const DensityWeightConfig&,
        std::vector<float>,
        Graphics::GpuPropertyResidency&,
        const EditorPointScalarTransactionSnapshot& diagnostics = {});
    [[nodiscard]] ActionReadiness PreviewEditorDensityWeightCommand(
        const EditorProcessingCommands&, const DensityWeightConfig&);
    [[nodiscard]] EditorDensityWeightResult ApplyEditorDensityWeightCommand(const EditorProcessingCommands&,
        const DensityWeightConfig&,
        std::function<void(EditorDensityWeightResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorDensityWeightConfig(
        const EditorProcessingCommands&, const DensityWeightConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<DensityWeightConfig> GetEditorDensityWeightConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(
        const EditorProcessingCommands&, std::function<void(EditorDensityWeightResult)> onComplete = {});
}
