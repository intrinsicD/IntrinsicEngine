// One-shot resident scalar previews shared by density, spacing and density weights.
module;
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
export module Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
export namespace Extrinsic::Runtime
{
    struct EditorPointScalarTransaction;
    using EditorPointScalarTransactionHandle=std::shared_ptr<EditorPointScalarTransaction>;
    struct EditorPointScalarTransactionSnapshot
    {
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        EditorCommandStatus Status{EditorCommandStatus::Pending};
        std::string Message{}, AcceptRefusalReason{};
        bool CanAccept{}, IndexReused{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        std::uint32_t GpuQueryBatches{};
        std::size_t LiveCount{};
        Graphics::PointScalarGpuStats Statistics{};
    };
    [[nodiscard]] EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
    // A nonempty Accept sink replaces the Start sink after admission succeeds.
    [[nodiscard]] EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink = {});
    void DiscardEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
}
