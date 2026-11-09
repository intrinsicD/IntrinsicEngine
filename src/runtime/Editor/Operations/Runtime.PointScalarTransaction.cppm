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
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.RHI.Handles;
extern "C++"
{
    namespace Extrinsic::RHI
    {
        class ICommandContext;
    }
}
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
        std::uint64_t StepRun{}; // the run's step run (run_control, run_observe); 0: an accept-only transaction
    };
    // External producers retain their numerical workspace; this transaction owns the
    // typed ring, float presentation ring, Accept readback and exactly-once delivery.
    struct EditorPointScalarPublication
    {
        std::uint32_t EntityId{}, Count{};
        std::uint32_t PreviewBuffers{3u};
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
    // ARCH-023: a publication transaction's own producer, a one-shot of bounded internal pages: one logical step of
    // the transaction's step run. Start begins the device work once the step run allows the step (acquire the back
    // here: a restart renews the rings); Poll (main thread, while started) is true once the work ended; Outcome then
    // is empty when the front was published (PublishEditorPointScalarBack, not ready) or says why it failed; Reset
    // returns the producer to before Start for run_control restart (nothing of it is in flight then).
    // ARCH-023: one page of a worker-stepped producer: records it, submits it with a pulled readback of `out.size()`
    // bytes and waits for it (the transaction's page gate, no frame); false: refused, failed or abandoned.
    using EditorPointScalarPage = std::function<bool(
        const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record, std::span<std::byte> out)>;
    // A publication transaction's producer of one logical step.
    struct EditorPointScalarProducer
    {
        // Owner, the run's Init (once it computes; again after a restart): acquires the write slots and prepares the
        // workspace and index its pages read, capturing what they record with. False: Outcome() says why.
        std::function<bool()> Prepare{};
        // Worker: the step, every page through `page`, touching only what Prepare captured; empty on success, else
        // why it failed.
        std::function<std::string(const EditorPointScalarPage& page)> Step{};
        // Owner, once the step ended: publishes its preview; empty on success, else why it failed.
        std::function<std::string()> Outcome{};
        std::function<void()> Reset{};
        // Optional, owner: the residency slots its pages read or write (stamped with the compute fence before the
        // transaction releases them).
        std::function<void(std::vector<RHI::BufferHandle>& buffers)> Uses{};
        // Optional, owner: drops what Prepare captured for the worker (its workspace and index references), once no
        // step can run any more (the transaction's Release), so a finished handle pins no lease.
        std::function<void()> Release{};
    };
    // Queues the transaction's owner-stepped Run job under `identity` (the output) and its step run with
    // `execution` (cap 1; a batch passes start=run, auto_accept=end): Accept, Discard and Restart through the shared
    // controls, the one Accept reading the fronts back. False when refused (no step-run service, a rejected job:
    // the transaction is closed, `refusal` says why).
    [[nodiscard]] bool SubmitEditorPointScalarRun(const EditorPointScalarTransactionHandle&,
        EditorJobIdentity identity,
        StepRunExecution execution,
        EditorPointScalarProducer producer,
        std::string debugName,
        std::string& refusal);
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
