// Normal estimation for point, graph and mesh properties through shared processing commands.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.NormalOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.NormalEstimationConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Graphics.GpuPropertyResidency;
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
        // Input traffic, resident hits and diagnostic/Accept downloads. Topology bytes apply
        // to mesh methods; PCA uses the cached LBVH and never downloads neighborhoods.
        std::uint64_t GpuInputUploadBytes{}, GpuTopologyBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool GpuTopologyReused{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
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
    [[nodiscard]] ActionReadiness PreviewEditorNormalEstimationCommand(const EditorProcessingCommands&, const NormalEstimationConfig&);
    [[nodiscard]] EditorNormalEstimationResult ApplyEditorNormalEstimationCommand(const EditorProcessingCommands&, const NormalEstimationConfig&, std::function<void(EditorNormalEstimationResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorNormalEstimationConfig(const EditorProcessingCommands&, const NormalEstimationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<NormalEstimationConfig> GetEditorNormalEstimationConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorNormalEstimationResult ApplyEditorConfiguredNormalEstimation(const EditorProcessingCommands&, std::function<void(EditorNormalEstimationResult)> onComplete = {});

    // Mesh and point-set PCA normals share a GPU property transaction (ADR 0030
    // decisions 6-7): the kernels read the canonical positions slot and a topology bundle the
    // residency keeps per topology revision, and write the output's float3 ring. The ring is
    // not observed by the renderer (vec3 rings are not colormap scalars or positions), so the
    // viewport keeps the CPU normals until Accept: one readback, the existing undoable
    // "Estimate normals" publication, then the front becomes the canonical slot of the new
    // revision. Discard releases the ring; a stale result (any input or the output changed)
    // can only be discarded.
    struct EditorNormalTransactionSnapshot
    {
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        bool Stale{};                        // the inputs or the output changed while the result waits
        bool CanAccept{};                    // ReadyToAccept, current and a front exists
        std::string AcceptDisabledReason{};
        std::uint32_t Previews{};            // fronts published so far (0 or 1)
        bool DeviceWorkQueued{};
        EditorNormalEstimationResult Result{}; // Pending until Applied, Failed or Discarded
    };
    struct EditorNormalTransaction;
    using EditorNormalTransactionHandle = std::shared_ptr<EditorNormalTransaction>;
    // Validates and captures like the command, then queues the device run. Null with `failure`
    // filled when the request is rejected (an unsupported method/mode, CPU backend, missing device or
    // residency, or a result for the same output still waiting for Accept or Discard). An active
    // job on the same output answers Pending with the shared "already has an active" message.
    [[nodiscard]] EditorNormalTransactionHandle StartEditorNormalEstimationTransaction(
        const EditorProcessingCommands&, const NormalEstimationConfig&, EditorNormalEstimationResult& failure);
    [[nodiscard]] EditorNormalTransactionSnapshot SnapshotEditorNormalEstimation(
        const EditorProcessingCommands&, const EditorNormalTransactionHandle&);
    // Pending when the readback was queued (onComplete then receives the published result);
    // otherwise the refusal (stale, wrong phase, no front).
    [[nodiscard]] EditorNormalEstimationResult AcceptEditorNormalEstimation(
        const EditorProcessingCommands&, const EditorNormalTransactionHandle&,
        std::function<void(EditorNormalEstimationResult)> onComplete = {});
    // Cancels a running or accepting run, publishes nothing and releases the ring.
    void DiscardEditorNormalEstimation(const EditorProcessingCommands&, const EditorNormalTransactionHandle&);
    // Test seam (null or mock device): a transaction over the entity's captured inputs that
    // already waits for Accept with `front` (every output row) as the device result; with a
    // residency, its ring holds a published front.
    [[nodiscard]] EditorNormalTransactionHandle MakeEditorNormalTransactionForTest(
        const EditorProcessingCommands&, const NormalEstimationConfig&, std::vector<glm::vec3> front,
        Graphics::GpuPropertyResidency* residency = nullptr);
    // The topology bundle of the config's mesh (face rings and vertex incidences the kernels
    // gather over), resident under a derived key for the current topology revision: the
    // production resolve of the transaction, exposed so a contract test can show that a second
    // resolve on the same revision uploads nothing and a topology edit uploads once.
    struct EditorNormalTopologyResidency
    {
        std::uint64_t Bytes{};
        bool Uploaded{}; // this call uploaded the bundle (otherwise it was resident)
        std::uint32_t Faces{}, LiveRows{};
    };
    [[nodiscard]] std::optional<EditorNormalTopologyResidency> ResolveEditorNormalTopology(
        const EditorProcessingCommands&, const NormalEstimationConfig&, Graphics::GpuPropertyResidency&,
        std::string& diagnostic);
}
