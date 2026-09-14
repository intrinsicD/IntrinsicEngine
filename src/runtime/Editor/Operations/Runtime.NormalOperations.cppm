// Normal estimation for point, graph and mesh properties through shared processing commands.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.NormalOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.NormalEstimationConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    struct EditorNormalEstimationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        NormalEstimationMethod Method{NormalEstimationMethod::PointSetPCA};
        NormalEstimationBackend RequestedBackend{NormalEstimationBackend::CpuKDTree};
        GeometryPropertyRef Output{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{}, ChangedCount{}, ValidCount{}, FallbackCount{};
        std::size_t ProcessedFaces{}, InvalidEdges{};
        Geometry::PointCloud::Normals::Diagnostics PointDiagnostics{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorNormalEstimationReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        NormalEstimationConfig Resolved{};
    };
    // Incomplete borrowed containers keep sibling workspace features independent
    // of normal method records; prepared frames copy their values.
    extern "C++"
    {
        struct EditorNormalResultSinks
        {
            std::function<void()> DismissResult{};
            std::function<void(EditorNormalEstimationResult)> NormalEstimation{};
        };
        struct EditorNormalResultsSnapshot
        {
            std::optional<EditorNormalEstimationResult> LastNormalEstimationResult{};
        };
    }
    struct EditorNormalPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorNormalResultSinks ResultSinks{};
        EditorNormalResultsSnapshot Results{};
    };
    [[nodiscard]] EditorNormalPreparedFrame PrepareEditorNormalFrame(const EditorWorkspaceAttachment&);
    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorNormalEstimationReadiness PreviewEditorNormalEstimationCommand(const EditorProcessingCommands&, const NormalEstimationConfig&);
    [[nodiscard]] EditorNormalEstimationResult ApplyEditorNormalEstimationCommand(const EditorProcessingCommands&, const NormalEstimationConfig&, std::function<void(EditorNormalEstimationResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorNormalEstimationConfig(const EditorProcessingCommands&, const NormalEstimationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<NormalEstimationConfig> GetEditorNormalEstimationConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorNormalEstimationResult ApplyEditorConfiguredNormalEstimation(const EditorProcessingCommands&, std::function<void(EditorNormalEstimationResult)> onComplete = {});
}
