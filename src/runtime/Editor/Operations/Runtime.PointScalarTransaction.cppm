// Resident scalar previews and typed, undoable publication for point methods.
module;
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <span>
#include <optional>
export module Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Graphics.PointScalarAnalysis;
export import Extrinsic.Graphics.GpuPropertyResidency;
export import Extrinsic.Runtime.GeometryProperty.Types;
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
    // External producers retain their numerical workspace; this transaction owns the
    // typed ring, float presentation ring, Accept readback and exactly-once delivery.
    struct EditorPointScalarPublication
    {
        std::uint32_t EntityId{}, Count{};
        GeometryPropertyRef Output{};
        std::string Label{};
        std::function<bool()> Current{};
        std::function<EditorCommandStatus(std::span<const std::byte>)> Publish{};
    };
    [[nodiscard]] EditorPointScalarTransactionHandle BeginEditorPointScalarPublication(
        const EditorProcessingCommands&, EditorPointScalarPublication,
        EditorPointScalarTransactionSnapshot&,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink = {});
    struct EditorPointScalarBack
    {
        Graphics::GpuPropertyView Typed{}, Presentation{};
    };
    [[nodiscard]] std::optional<EditorPointScalarBack> AcquireEditorPointScalarBack(
        const EditorPointScalarTransactionHandle&);
    // Call only after the producer completion, after releasing its back leases.
    [[nodiscard]] bool PublishEditorPointScalarBack(const EditorPointScalarTransactionHandle&, bool ready);
    [[nodiscard]] EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
    // A nonempty Accept sink replaces the Start sink after admission succeeds.
    [[nodiscard]] EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink = {});
    void DiscardEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
}
