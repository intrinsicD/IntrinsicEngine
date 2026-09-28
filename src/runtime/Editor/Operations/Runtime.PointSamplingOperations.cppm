// Standalone point sampling (RUNTIME-274): orders one entity's points with a selectable
// Geometry.PointSampling method and publishes the selection as undoable rank/selection
// properties or as a new point-cloud entity. Runs synchronously on the CPU; the Vulkan
// backend axis follows with RUNTIME-290.
module;
#include <cstdint>
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
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status == EditorCommandStatus::Applied; }
    };

    [[nodiscard]] ActionReadiness PreviewEditorPointSamplingCommand(const EditorProcessingCommands&,
                                                                    const PointSamplingOperationConfig&);
    [[nodiscard]] EditorPointSamplingResult ApplyEditorPointSamplingCommand(const EditorProcessingCommands&,
                                                                            const PointSamplingOperationConfig&);
    [[nodiscard]] EditorPointSamplingResult ApplyEditorConfiguredPointSampling(const EditorProcessingCommands&);
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSamplingConfig(const EditorProcessingCommands&,
                                                                               const PointSamplingOperationConfig&,
                                                                               std::string sourceId = {});
    [[nodiscard]] std::optional<PointSamplingOperationConfig> GetEditorPointSamplingConfig(const EditorProcessingCommands&);
}
