// Resident scalar previews and typed, undoable publication for point methods.
module;
#include <array>
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
export import Extrinsic.Graphics.PointScalarAnalysis;
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
        // RUNTIME-348: a start refused because another run owns the output (an active job, or a
        // transaction awaiting Accept or Discard); that run's result keeps the operation's slot.
        bool OutputBusy{};
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
        // Optional second typed field; the callback publishes both in one history entry.
        std::optional<GeometryPropertyRef> Companion{};
        std::function<EditorCommandStatus(std::span<const std::byte>, std::span<const std::byte>)> PublishPair{};
    };
    [[nodiscard]] EditorPointScalarTransactionHandle BeginEditorPointScalarPublication(
        const EditorProcessingCommands&, EditorPointScalarPublication,
        EditorPointScalarTransactionSnapshot&,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink = {});
    // A publication transaction's Accept stage joins the run that computes its front: it is
    // submitted under `identity` (the output, with `Run` set to that run's first job).
    void JoinEditorPointScalarRun(const EditorPointScalarTransactionHandle&, EditorJobIdentity identity);
    struct EditorPointScalarBack
    {
        Graphics::GpuPropertyView Typed{}, Presentation{}, Companion{};
    };
    [[nodiscard]] std::optional<EditorPointScalarBack> AcquireEditorPointScalarBack(
        const EditorPointScalarTransactionHandle&);
    // Call only after the producer completion, after releasing its back leases.
    [[nodiscard]] bool PublishEditorPointScalarBack(const EditorPointScalarTransactionHandle&, bool ready,
        std::optional<std::array<float, 2>> scalarRange = {});
    [[nodiscard]] EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
    // A nonempty Accept sink replaces the Start sink after admission succeeds.
    [[nodiscard]] EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(
        const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink = {});
    void DiscardEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle&);
    // The transaction's own tests (no engine method starts one alone since ARCH-022 Q90): the GPU kernel `params` over
    // `output` as the editing service starts it, captured as a CPU run would be. With `testResidency`, over given
    // `values` (one per slot of the output's domain) on that residency with no device work; without, on the device and
    // the spatial cache of `commands` after their admission (`values` unused). Null with the refusal in `refused` (its
    // Message, and Status GeometryProcessingFailed for a rejected submission).
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorPointScalarTransactionForTest(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, GeometryPropertyRef positions,
        GeometryPropertyRef output, std::vector<float> values, Graphics::GpuPropertyResidency* testResidency,
        const EditorPointScalarTransactionSnapshot& diagnostics = {},
        const Graphics::PointScalarGpuParams& params = {.Method = 2, .Capacity = 256, .Kernel = 1, .SupportRadius = 1},
        EditorPointScalarTransactionSnapshot* refused = nullptr);
}
