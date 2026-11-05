// ARCH-022: point-cloud edits for plugins. Captures an entity's live points and normals, gives neighbourhood rows
// (k nearest or within a radius) on the CPU or the GPU, and publishes new positions into a vec3 output, or values
// into several scalar outputs, with one undo entry; or runs a point scalar method on the GPU with a resident preview
// until Accept or Discard. It wraps the editor's shared point helpers, so a plugin imports no editor module, and it
// holds no plugin callable: undo entries hold host data, so they work after the plugin that recorded them is unloaded.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

export module Extrinsic.Runtime.PointCloudEditing;

export import Extrinsic.Runtime.GeometryAvailability;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.PointLBVH;
export import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    // What to read and where to write: a vec3 `Output` (Publish), or with `ScalarOutputs` one scalar column each
    // (PublishScalars; `Output` is unused then). Unknown domains resolve to the entity's primary point domain, the
    // normals' and the outputs' to the positions'. An empty `Normals` name takes the positions alone.
    struct PointEditRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Normals{};
        GeometryPropertyRef Output{};
        std::vector<GeometryPropertyRef> ScalarOutputs{};
        std::size_t MinimumLiveCount{2u};
        std::string Noun{}; // a missing entity answers "<Noun> target entity is stale or missing." (default "The")
        bool CopyValues{true};    // false: readiness only, nothing copied
        bool AcquireIndex{false}; // the cached spatial index of the points (CPU and GPU neighbourhoods)
        // The entity's local Transform (an unparented entity only): its matrix in the view, and the capture is stale
        // once the entity gets a parent or its Transform changes, appears or goes.
        bool CaptureTransform{false};
    };

    struct PointEdit; // a capture, owned by the service's code

    struct PointEditView
    {
        std::span<const glm::vec3> Points{};
        std::span<const glm::vec3> PointNormals{};
        std::size_t SlotCount{0u}, LiveCount{0u};
        bool ValidLbvh{false};  // the points fit an LBVH (count and coordinates)
        bool IndexReused{false};
        bool HasZeroNormals{false}; // a live normal of length zero
        GeometryPropertyRef Output{}; // with its resolved domain
        std::span<const GeometryPropertyRef> ScalarOutputs{}; // with their resolved domains
        glm::dmat4 Model{1.0}; // with CaptureTransform: the local Transform's matrix (identity without a Transform)
    };

    // Neighbour rows within a radius, compact indices: point i's neighbours are Indices[Offsets[i], Offsets[i+1]),
    // ascending, self excluded.
    struct PointNeighbourRows
    {
        std::vector<std::uint32_t> Offsets{0u}, Indices{};
        std::size_t MaximumNeighbors{0u}; // the largest support found, before a lowest-ID limit
    };

    enum class PointRowsState : std::uint8_t { Pending, Ready, Failed };

    struct PointGpuRowStats
    {
        std::size_t QueryBatches{0u}, WorkspaceBuilds{0u};
        double Milliseconds{0.0};
    };

    // Unrepresentable: a value does not fit a scalar output's storage exactly (nothing is written).
    enum class PointPublishStatus : std::uint8_t { Applied, Stale, Rejected, Unrepresentable };
    // Busy: not now (no free ring slot, or another result holds the ring); a later call may show it.
    enum class PointPreviewStatus : std::uint8_t { Shown, Busy, Unavailable };

    // A point scalar method (one value per point into one scalar output), named as its definition names it: `Noun`
    // ("Radii") names the output, the target and the Jobs row ("<Noun> estimation"), `Method` ("Point spacing") its
    // sample requirement, `History` the undo entry of a GPU run. `Params` are the device kernel's.
    struct ScalarAnalysisRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Output{}; // any scalar kind on the CPU; the GPU writes float outputs only
        Graphics::PointScalarGpuParams Params{};
        std::string Noun{}, Method{}, History{};
    };
    // Where the method runs: the readiness differs (the CPU index and the device have limits).
    enum class ScalarAnalysisBackend : std::uint8_t { CpuReference, CpuIndex, Gpu };
    enum class ScalarAnalysisPhase : std::uint8_t { Running, ReadyToAccept, Accepting, Applied, Discarded, Failed };

    struct ScalarAnalysisState
    {
        ScalarAnalysisPhase Phase{ScalarAnalysisPhase::Failed};
        std::string Message{}, AcceptRefusal{};
        bool CanAccept{false};
        bool Stale{false};    // it ended because an input or the output changed
        bool Detached{false}; // its workspace detached: discarded, nothing published
        bool IndexReused{false};
        std::size_t SlotCount{0u}, LiveCount{0u};
        GeometryPropertyRef Output{}; // with its resolved domain
        std::uint64_t GpuInputUploadBytes{0u}, GpuInputCacheHits{0u}, CpuStageReadbackBytes{0u};
        std::uint32_t GpuQueryBatches{0u};
        Graphics::PointScalarGpuStats Statistics{};
    };

    // A refused start: why, and whether another run owns the output (then that run's result answers, not this one).
    struct ScalarAnalysisRefusal
    {
        std::string Message{};
        bool Busy{false};
    };

    // One GPU run of a point scalar method (main thread). Its own Jobs run drives it, with the Accept stage joined
    // to it; the Jobs window's cancel ends it there. Move-only: destroying it discards a run that has not ended
    // (an Accept already publishing finishes), so the owner's lifetime bounds the preview. It also ends, Detached,
    // when its workspace detaches or the service stops using that workspace (UseEditorWorkspace, Release).
    class ScalarAnalysis
    {
    public:
        struct Run;
        ScalarAnalysis() noexcept;
        explicit ScalarAnalysis(std::shared_ptr<Run> run) noexcept;
        ScalarAnalysis(ScalarAnalysis&&) noexcept;
        ScalarAnalysis& operator=(ScalarAnalysis&&) noexcept;
        ~ScalarAnalysis();
        [[nodiscard]] explicit operator bool() const noexcept { return m_Run != nullptr; }

        // The run's state now. After its workspace detached it is discarded here (Detached).
        [[nodiscard]] ScalarAnalysisState Poll();
        // Publishes the preview into the output, as one undo entry, once its readback landed (Accepting until then);
        // refused with the reason while it is not ready or an input changed.
        ScalarAnalysisState Accept();
        // Releases the preview; the output and the history stay unchanged.
        void Discard();

    private:
        std::shared_ptr<Run> m_Run; // the only owner; the service keeps a weak reference to end it
    };

    // The editor session a result belongs to: the workspace the service used and that workspace's attachment.
    struct PointSession
    {
        std::function<bool()> Active{};
        const void* Workspace{nullptr};
    };

    // A plugin window's picker over an InputCatalog (ImGui, the host's context): true when the choice changed `ref`;
    // a `ref` the catalog does not list stays as it is.
    bool PointPropertyCombo(const char* label, GeometryPropertyRef& ref, const GeometryPropertyCatalogSnapshot& catalog);
    // A plugin window's scalar output (ImGui): its name and storage (bool, int32, uint32, uint64, float, double);
    // true when either changed.
    bool PointScalarOutputInput(const char* label, GeometryPropertyRef& ref);

    class PointCloudEditingService
    {
    public:
        PointCloudEditingService();
        ~PointCloudEditingService();
        PointCloudEditingService(const PointCloudEditingService&) = delete;
        PointCloudEditingService& operator=(const PointCloudEditingService&) = delete;

        // Main thread. Null with a diagnostic when the entity, the references or the rows do not fit.
        [[nodiscard]] std::shared_ptr<PointEdit> Capture(const PointEditRequest& request, std::string& diagnostic);
        [[nodiscard]] PointEditView View(const PointEdit& edit) const;
        // Main thread: inputs and output unchanged since the capture.
        [[nodiscard]] bool Current(const PointEdit& edit);
        // The cached CPU LBVH over the captured points (with AcquireIndex), indices compact; any thread.
        [[nodiscard]] const Geometry::PointLBVH::Index* CpuIndex(const PointEdit& edit) const;
        // Main thread: the cached spatial index of a capture made without AcquireIndex, as AcquireIndex would have
        // acquired it, so a caller can check its own limits first; false with the reason.
        [[nodiscard]] bool AcquireIndex(PointEdit& edit, std::string& diagnostic);
        // Main thread: GPU neighbourhood queries can run (framed spatial cache and job service).
        [[nodiscard]] bool GpuQueriesAvailable();
        // Main thread, once per frame until Ready or Failed: appends `width` compact neighbour indices (self
        // included) per point of `points`. The first pass queries the cached index; after RestartGpuKnnRows a
        // pass queries a private index over the moved `points`.
        [[nodiscard]] PointRowsState AdvanceGpuKnnRows(PointEdit& edit, std::span<const glm::vec3> points,
                                                       std::uint32_t width, std::uint32_t batchSize,
                                                       std::vector<std::uint32_t>& rows, std::string& diagnostic);
        void RestartGpuKnnRows(PointEdit& edit);
        // Rows within `radius` (with AcquireIndex). A positive `lowestIdLimit` keeps a point's lowest-ID neighbours
        // up to that many; 0 requires the complete support. CPU: from the cached index, any thread. GPU: main thread,
        // once per frame until Ready or Failed, pages of `batchSize` queries with `capacity` candidates each; Failed
        // when the inputs changed. False or Failed with a diagnostic when a support is incomplete.
        [[nodiscard]] bool CpuRadiusRows(const PointEdit& edit, float radius, std::uint32_t lowestIdLimit,
                                         PointNeighbourRows& rows, std::string& diagnostic) const;
        [[nodiscard]] PointRowsState AdvanceGpuRadiusRows(PointEdit& edit, float radius, std::uint32_t batchSize,
                                                          std::uint32_t capacity, std::uint32_t lowestIdLimit,
                                                          PointNeighbourRows& rows, std::string& diagnostic);
        [[nodiscard]] PointGpuRowStats GpuRowStats(const PointEdit& edit) const;
        // Main thread: writes `live` (one position per captured point) into the output for the live slots, records
        // one undo entry named `label` and marks the entity dirty. Stale when an input or the output changed;
        // Rejected (nothing written) for a non-finite position.
        [[nodiscard]] PointPublishStatus Publish(PointEdit& edit, std::span<const glm::vec3> live, std::string label);
        // Main thread, an in-place output (the positions): shows `live` (one position per captured point) as the
        // positions' ring front, which the renderer draws, without changing the positions or the history. Busy or
        // Unavailable (no operational device, not in place, inputs changed) with the reason otherwise. The capture
        // keeps its ring until DiscardPreview or Publish (which releases it first).
        [[nodiscard]] PointPreviewStatus PreviewPositions(PointEdit& edit, std::span<const glm::vec3> live,
                                                          std::string& diagnostic);
        // Main thread: releases the capture's preview ring (only that ring; nothing without one).
        void DiscardPreview(PointEdit& edit);
        // Main thread: writes one column per scalar output (one value per captured point) for the live slots, all or
        // none, as one undo entry named `label`. Deleted slots keep their values.
        [[nodiscard]] PointPublishStatus PublishScalars(PointEdit& edit, std::span<const std::vector<float>> columns,
                                                        std::string label);
        // Main thread, side-effect free: empty when the method can run on `backend`, else why (the target, the
        // capture, fewer than two live samples, the LBVH limits; on the GPU its admission: a float output, normal
        // coordinates and parameters, k <= 63 for methods 0 and 1, row and workspace limits, an operational float64
        // device with the framed spatial cache and jobs).
        [[nodiscard]] std::string ScalarAnalysisReadiness(const ScalarAnalysisRequest& request,
                                                          ScalarAnalysisBackend backend);
        // Main thread: the GPU run after the same checks. The output shows its preview until Accept or Discard; an
        // `autoAccept` run accepts as soon as it is ready. Empty with the reason in `refusal` otherwise.
        [[nodiscard]] ScalarAnalysis StartScalarAnalysis(const ScalarAnalysisRequest& request, bool autoAccept,
                                                         ScalarAnalysisRefusal& refusal);
        // A run over given values (one per slot) on a test residency, captured as a CPU run is; no device work.
        [[nodiscard]] ScalarAnalysis StartScalarAnalysisForTest(const ScalarAnalysisRequest& request,
                                                                std::vector<float> values,
                                                                Graphics::GpuPropertyResidency& residency,
                                                                ScalarAnalysisRefusal& refusal);
        // Main thread: the session results are kept for; current while that workspace is still the one used and
        // still attached (a detach and reattach of the same workspace makes a new session).
        [[nodiscard]] PointSession CurrentSession();
        [[nodiscard]] bool SessionCurrent(const PointSession& session) const;
        // Main thread: shows a property of the entity in the viewport (the processing panels' "Show"); false with
        // the reason when the editor refuses.
        [[nodiscard]] bool ShowProperty(std::uint32_t stableEntityId, const GeometryPropertyRef& property,
                                        std::string& diagnostic);
        // The entity's vec3 point properties with at least `minimumLiveCount` live rows, for pickers; with `domain`,
        // only that domain's (a normals picker offers the positions' domain).
        [[nodiscard]] GeometryPropertyCatalogSnapshot InputCatalog(
            std::uint32_t stableEntityId, std::size_t minimumLiveCount,
            std::optional<GeometryElementDomain> domain = std::nullopt);
        // The app's editor workspace (the Sandbox shell's), so edits share its history guards and session.
        void UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept;
        // Before that workspace detaches: back to the service's own.
        void ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept;

        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
        friend class PointCloudEditingModule;
    };

    class PointCloudEditingModule final : public IRuntimeModule
    {
    public:
        PointCloudEditingModule();
        ~PointCloudEditingModule() override;
        // After "Runtime.SceneDocumentModule" in name order, so it shuts down (and detaches its own workspace)
        // before the document it observes.
        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.Services.PointCloudEditing"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        [[nodiscard]] RuntimeModuleResult OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        PointCloudEditingService m_Service{};
    };
}
