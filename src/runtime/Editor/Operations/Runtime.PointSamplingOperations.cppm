// Standalone point sampling (RUNTIME-274): orders one entity's points with a selectable
// Geometry.PointSampling method and publishes the selection as undoable rank/selection
// properties or as a new point-cloud entity. CPU runs are synchronous; the Vulkan backend
// (RUNTIME-290, Runtime.PointSamplingGpu) runs as a framed GPU job and falls back to the CPU,
// saying why, where it cannot run.
module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.PointSamplingOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.PointSamplingConfig;
export import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
export namespace Extrinsic::Runtime
{
    struct EditorPointSamplingResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::string Method{};
        std::uint32_t InputCount{0u};
        std::uint32_t SampleCount{0u};
        std::uint32_t OutputEntityId{0u}; // point-cloud output
        double Milliseconds{0.0};         // sampling only
        std::uint64_t DistancePairs{0u};  // farthest-point family diagnostics
        // RUNTIME-290: the backend asked for, the one that produced the order, and why the CPU
        // ran when Vulkan was asked for.
        std::string RequestedBackend{"cpu_reference"};
        std::string Backend{"cpu_reference"};
        std::string BackendDiagnostic{};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status == EditorCommandStatus::Applied; }
    };

    [[nodiscard]] ActionReadiness PreviewEditorPointSamplingCommand(const EditorProcessingCommands&,
                                                                    const PointSamplingOperationConfig&);
    // CPU runs return the final result. A Vulkan run with a job lane and device returns Pending
    // and delivers the final result to `onComplete` (also called for immediate results).
    [[nodiscard]] EditorPointSamplingResult ApplyEditorPointSamplingCommand(
        const EditorProcessingCommands&, const PointSamplingOperationConfig&,
        std::function<void(EditorPointSamplingResult)> onComplete = {});
    [[nodiscard]] EditorPointSamplingResult ApplyEditorConfiguredPointSampling(
        const EditorProcessingCommands&, std::function<void(EditorPointSamplingResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSamplingConfig(const EditorProcessingCommands&,
                                                                               const PointSamplingOperationConfig&,
                                                                               std::string sourceId = {});
    [[nodiscard]] std::optional<PointSamplingOperationConfig> GetEditorPointSamplingConfig(const EditorProcessingCommands&);
}
