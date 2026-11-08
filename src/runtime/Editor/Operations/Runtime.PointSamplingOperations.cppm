// Standalone point sampling (RUNTIME-274): orders one entity's points with a selectable
// Geometry.PointSampling method and publishes the selection as undoable rank/selection
// properties or as a new point-cloud entity. ARCH-023: with the session's step runs and job lane
// both backends run as step runs (one selected sample is one step of exact farthest-point
// sampling, any other method one step): the CPU on a worker, Vulkan (RUNTIME-290,
// Runtime.PointSamplingGpu) as an owner-stepped GPU transaction that falls back to the CPU,
// saying why, where it cannot run. Without them the CPU orders inline.
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
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        std::string Message{};
        // ARCH-023: the run's step run (run_control, run_observe); 0 for an inline run or a refusal. Steps: the
        // logical steps of the published result (samples of exact farthest point, else 1).
        std::uint64_t StepRun{};
        std::uint32_t Steps{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status == EditorCommandStatus::Applied; }
    };

    [[nodiscard]] ActionReadiness PreviewEditorPointSamplingCommand(const EditorProcessingCommands&,
                                                                    const PointSamplingOperationConfig&);
    // The batch command (agent, scripts): runs at once and accepts at its end whatever the config's start and
    // auto_accept. With the session's step runs and job lane it returns Pending and delivers the final result to
    // `onComplete` (also called for immediate results); without them the CPU publishes at once. While a run writes
    // the same rank output, a property-output request (either backend) answers Pending "Point sampling already has
    // an active ... job" without calling `onComplete`.
    [[nodiscard]] EditorPointSamplingResult ApplyEditorPointSamplingCommand(
        const EditorProcessingCommands&, const PointSamplingOperationConfig&,
        std::function<void(EditorPointSamplingResult)> onComplete = {});
    // The interactive start: the step run with the config's start and auto_accept (ready by default: nothing is
    // computed until run or step n). Pending with StepRun on success, which the shared controls drive (Stop keeps
    // the samples so far, Accept publishes them once, Restart begins again); refused without the session's step
    // runs and job lane.
    [[nodiscard]] EditorPointSamplingResult StartEditorPointSampling(const EditorProcessingCommands&,
        const PointSamplingOperationConfig&,
        std::function<void(EditorPointSamplingResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSamplingConfig(const EditorProcessingCommands&,
                                                                               const PointSamplingOperationConfig&,
                                                                               std::string sourceId = {});
    [[nodiscard]] std::optional<PointSamplingOperationConfig> GetEditorPointSamplingConfig(const EditorProcessingCommands&);

    // UI-081 Compute arrow order: orders one element domain's live anchors (vertex/node/point
    // positions, edge midpoints, face centers; object space) with `Method` and publishes the
    // undoable rank GeometryGlyphOrderPropertyName(domain): the first min(L, cap) by the method,
    // the other live rows after them in row order, -1 on deleted rows. CPU, synchronous.
    inline constexpr std::uint32_t kEditorGlyphOrderCap = 65536u;
    struct EditorGlyphOrderCommand
    {
        std::uint32_t StableEntityId{0u};
        GeometryElementDomain Domain{GeometryElementDomain::Unknown};
        PointSamplingMethod Method{PointSamplingMethod::FarthestPoint};
    };
    struct EditorGlyphOrderResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::uint32_t LiveCount{0u};
        std::uint32_t OrderedCount{0u};
        double Milliseconds{0.0};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };
    [[nodiscard]] EditorGlyphOrderResult ApplyEditorGlyphOrderCommand(
        const EditorProcessingCommands&, const EditorGlyphOrderCommand&);
}
