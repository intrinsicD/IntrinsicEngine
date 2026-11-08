// Runtime-owned, revision-aware entity point indices shared by geometry consumers.
module;
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <entt/entity/entity.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>
export module Extrinsic.Runtime.SpatialIndexCache;
export import Geometry.PointLBVH;
export import Extrinsic.Graphics.GpuPropertyResidency;
export import Extrinsic.Graphics.PointQuery;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;

// The CPU constructor only borrows a reference; matching the owner's C++ language
// linkage keeps this interface out of the WorldRegistry job/RHI import closure.
extern "C++" {
    namespace Extrinsic::Runtime { class WorldRegistry; }
    namespace Extrinsic::RHI
    {
        class BufferManager;
        class ICommandContext;
        class IDevice;
    }
}

export namespace Extrinsic::Runtime
{
    struct SpatialIndexHandle
    {
        std::uint64_t Value{};
    };
    struct SpatialIndexAcquisition
    {
        SpatialIndexHandle Handle{};
        bool Reused{};
        std::string Diagnostic{};
        [[nodiscard]] bool Ready() const noexcept
        {
            return Handle.Value != 0;
        }
    };
    struct SpatialIndexCacheStats
    {
        // Builds counts CPU hierarchies built (a GPU-only workspace builds none); GpuBuilds counts device
        // hierarchy builds in accepted submissions only.
        std::uint64_t Builds{}, Hits{}, Evictions{}, GpuBuilds{};
        // GRAPHICS-153: device buffer sets created for query batches (a reused batch that fits
        // allocates nothing), and private workspaces updated in place instead of rebuilt.
        std::uint64_t GpuBatchAllocations{}, WorkspaceUpdates{};
        // Device workspaces handed out by LeaseGpuWorkspace, and those served by a retained one.
        std::uint64_t WorkspaceLeases{}, WorkspaceReuses{};
        // Upload bytes: query-input writes only, excluding target-index builds and result readback.
        // Submissions: query batches and computations (QueueGpuCompute) the device accepted.
        std::uint64_t GpuQueryUploadBytes{}, GpuQueryTransformUploads{}, GpuQuerySubmissions{};
        std::uint64_t GpuComputeSubmissions{};
        // ARCH-023 method scratch (MethodScratch, budget render.gpu_method_buffer_budget_megabytes), in physical
        // size-class bytes: leased by runs, released but not yet proven complete, and idle (reusable, evictable);
        // cache hits, device allocations on a miss, budget refusals and LRU evictions.
        std::uint64_t ScratchActiveBytes{}, ScratchPendingBytes{}, ScratchIdleBytes{}, ScratchBudgetBytes{};
        std::uint64_t ScratchHits{}, ScratchMisses{}, ScratchRefusals{}, ScratchEvictions{};
        // Device bytes of the retained per-entity indices (sample/row-map buffers and hierarchies): outside the
        // scratch budget, held while their entry is current or a live query session reads it, plus the private
        // hierarchies of moving query sessions. A session's own buffers are method scratch (Scratch*Bytes).
        std::uint64_t IndexDeviceBytes{};
    };
    enum class SpatialIndexSpace : std::uint8_t { Property, EntityTransform };
    struct SpatialIndexSnapshot
    {
        Geometry::PointLBVH::Index Index{};
        std::vector<std::uint32_t> Slots{};
        // ARCH-023: false for a GPU-only snapshot (Acquire without cpuIndex): no CPU hierarchy, Index stays empty and
        // Positions holds the live rows' points (in Slots order) instead.
        bool Cpu{true};
        std::vector<glm::vec3> Positions{};
        // The live rows' points either way (the hierarchy's input order).
        [[nodiscard]] std::span<const glm::vec3> Points() const noexcept
        {
            return Cpu ? Index.Points() : std::span<const glm::vec3>(Positions);
        }
    };
    // Ordered source rows and exact numeric positions (+0 == -0); null never matches.
    [[nodiscard]] bool SpatialIndexSnapshotMatches(const SpatialIndexSnapshot*,
        std::span<const std::uint32_t> slots, std::span<const glm::vec3> points) noexcept;
    struct SpatialIndexWorkspace
    {
        SpatialIndexHandle Handle{};
        std::shared_ptr<const SpatialIndexSnapshot> Snapshot{};
        std::string Diagnostic{};
        [[nodiscard]] bool Ready() const noexcept { return Handle.Value && bool(Snapshot); }
    };
    enum class SpatialQueryState : std::uint8_t { Queued, Submitted, Ready, Failed };
    struct SpatialNearestBatch
    {
        SpatialQueryState State{SpatialQueryState::Queued};
        // Query i owns Capacity entries. Radius Counts report all hits, so only
        // min(Counts[i], Capacity) entries are stored; callers must handle overflow.
        std::vector<Geometry::PointLBVH::Neighbor> Neighbors{};
        std::vector<std::uint32_t> Counts{};
        std::uint32_t Capacity{1};
        std::string Diagnostic{};
    };
    // ARCH-023: neighbourhood queries a method's worker step records, submits and waits for itself, with no frame,
    // render thread or owner callback: the cache's kernels (LBVH traversal or exhaustive scan), packed result,
    // validation and original row IDs, as QueueGpuNearest/KNearest/Radius/NearestTransformed give them.
    // SpatialIndexCache::PrepareQuerySession (device-owner thread, a run's Init) makes a fixed target resident with its
    // row map and (Lbvh) hierarchy through one accepted owner submission (always made; it waits for every transfer
    // issued before it), and allocates every buffer and pipeline the pages use. Pages then call
    // IDevice::SubmitComputeReadback (a pulled readback, ComputeInputOrder::LaneOrderOnly: ordered after that
    // preparation, or a moving target's host-written rows, by the compute lane alone, so a later graphics-queue
    // upload does not hold a page, save queue order where the lane shares a queue with frames), ITransferQueue::WaitCompute and IDevice::WriteMappedBuffer only: no other device
    // call, no graphics-lane work, no frame, and no cache, ECS or residency state. Session buffers are method scratch
    // fixed at prepare (within render.gpu_method_buffer_budget_megabytes) and reused by every page and upload. A
    // page's pulled bytes land in a transfer-queue readback slot of the page's packed size (outside that budget,
    // reused by later readbacks of the lane and kept until device shutdown, reported by
    // TransferQueueDiagnostics::ReadbackRingHighWaterBytes).
    struct SpatialQuerySessionDesc
    {
        Graphics::PointQueryStrategy Strategy{Graphics::PointQueryStrategy::Lbvh};
        // Radius rows (a page gives its radius) retaining Capacity hits each (1..1024); else nearest/kNN, k = Capacity
        // (1..64).
        bool Radius{false};
        std::uint32_t Capacity{1};
        std::uint32_t PageQueries{}; // the most queries one page carries (1..2^20, within brute-force admission)
        // The query rows (1..2^20), written once here; a page queries a range of them. UploadQueries replaces them.
        std::span<const glm::vec3> Queries{};
        std::span<const std::uint32_t> Excluded{}; // empty or one original row ID per query row (~0u: none)
        bool Transformed{false}; // nearest only: each page gives a dmat4 applied in double precision (float64 device)
        // The worker replaces the query rows (UploadQueries): the rows are device-local, fed by one prepared
        // host-visible staging buffer that each page after an upload copies on the compute lane.
        bool Uploads{false};
        // No index: the target is the query rows themselves (identity row IDs, no CPU hierarchy), its device
        // hierarchy (Lbvh) rebuilt on the worker into prepared storage after each UploadQueries. Implies Uploads.
        bool Moving{false};
    };
    enum class SpatialPageStatus : std::uint8_t
    {
        Ready,
        Failed,
        Abandoned
    };
    // Owned by the cache's code; the run holds it (any thread may drop it). One worker at a time pages it (a run's
    // step). Dropping it ends the session: the cache releases its buffers on the device-owner thread once its last
    // page completed. Cache shutdown (after device idle) stops a session still held: its device objects go then, on
    // the owner, and its later pages and uploads fail ("Spatial query service stopped.").
    extern "C++" class SpatialQuerySession final
    {
    public:
        struct Impl;
        explicit SpatialQuerySession(std::unique_ptr<Impl> impl) noexcept;
        ~SpatialQuerySession();
        SpatialQuerySession(const SpatialQuerySession&) = delete;
        SpatialQuerySession& operator=(const SpatialQuerySession&) = delete;
        // Worker (an Uploads or Moving session): replaces every query row (finite, the session's row count) by a
        // mapped write into the prepared staging, which the next page copies on the compute lane before reading; a
        // moving target's next page also rebuilds its hierarchy over them. Refused while the session's last page
        // has not completed (the staging is reused). False (nothing written) with the reason.
        [[nodiscard]] bool UploadQueries(std::span<const glm::vec3> rows, std::string& diagnostic);
        // Worker: queries rows [first, first + count) (count <= PageQueries) with `radius` (radius sessions:
        // inclusive, finite, 0..1e18) or `transform` (a Transformed session), submits, then waits in bounded slices
        // until the page completed, `abort` holds, or the device failed. Ready: `out` holds the page (Neighbors,
        // Counts, Capacity) exactly as the cache's batch of those queries would. Failed: out.Diagnostic says why.
        // Abandoned: `abort` won; the page's bytes are released once the device finished it.
        [[nodiscard]] SpatialPageStatus Query(std::uint32_t first,
            std::uint32_t count,
            float radius,
            const glm::dmat4* transform,
            const std::atomic<bool>& abort,
            SpatialNearestBatch& out);
        [[nodiscard]] std::uint32_t Rows() const noexcept;
        [[nodiscard]] std::uint32_t PageQueries() const noexcept;
        [[nodiscard]] std::uint32_t Capacity() const noexcept;
        // Host traffic, cumulative (any thread): pages submitted, bytes written (queries, exclusions, poses,
        // uploads) and packed result bytes read back.
        [[nodiscard]] std::uint64_t Pages() const noexcept;
        [[nodiscard]] std::uint64_t UploadBytes() const noexcept;
        [[nodiscard]] std::uint64_t ReadbackBytes() const noexcept;
        // The cache's code only (Impl is defined there): its owner-side bookkeeping.
        [[nodiscard]] Impl& Internals() noexcept
        {
            return *m_Impl;
        }

    private:
        std::unique_ptr<Impl> m_Impl;
    };
    struct SpatialGpuIndexView
    {
        std::uint64_t NodesBDA{}, PositionsBDA{}, OriginalSlotsBDA{};
        std::uint32_t Count{};
    };
    // ARCH-023: an owner-prepared, retained and immutable view of a cached index's device resources, for a method
    // kernel that records its page on a worker (scalar analysis, normals, outliers, keypoints) exactly as its
    // QueueGpuCompute(handle, ...) recorder did. SpatialIndexCache::PrepareGpuIndex (device-owner thread, a run's
    // Init) makes the entry's samples, row map and hierarchy resident through one accepted owner submission (the
    // same build QueueGpuCompute records) and resolves their addresses; nothing is rebuilt per step. The lease keeps
    // the entry and its device buffers alive whatever the cache does meanwhile (eviction, a newer revision): a
    // worker reads only View() and Barriers(), never the cache. Dropping it (any thread) ends it: the cache stamps
    // the residency slots the index borrows with the compute fence and frees the entry (if no longer cached) on the
    // owner once that fence completed. Record the run's last page before dropping the lease. At the cache's final
    // OnShutdown (workers drained, device idle) a still-held lease gives up its entry there, on the owner; View() and
    // Barriers() are then empty and its later drop releases host memory only.
    extern "C++" class SpatialGpuIndexLease final
    {
    public:
        struct Impl;
        explicit SpatialGpuIndexLease(std::unique_ptr<Impl> impl) noexcept;
        ~SpatialGpuIndexLease();
        SpatialGpuIndexLease(const SpatialGpuIndexLease&) = delete;
        SpatialGpuIndexLease& operator=(const SpatialGpuIndexLease&) = delete;
        [[nodiscard]] const SpatialGpuIndexView& View() const noexcept;
        // The index's device buffers (samples, row map, hierarchy storage): a page records a barrier from their build
        // (an earlier submission on the same lane) before reading them.
        [[nodiscard]] std::span<const RHI::BufferHandle> Barriers() const noexcept;
        // The index's ordered original row IDs.
        [[nodiscard]] const std::shared_ptr<const SpatialIndexSnapshot>& Snapshot() const noexcept;
        [[nodiscard]] Impl& Internals() noexcept
        {
            return *m_Impl;
        }

    private:
        std::unique_ptr<Impl> m_Impl;
    };
    struct SpatialGpuResult
    {
        SpatialQueryState State{SpatialQueryState::Queued};
        std::vector<std::byte> Data{};
        std::string Diagnostic{};
        std::uint64_t CpuStageUploadBytes{};
    };
    // This concrete service is also its runtime module; there is no forwarding service layer.
    extern "C++" class SpatialIndexCache final : public IRuntimeModule
    {
      public:
        SpatialIndexCache();
        explicit SpatialIndexCache(WorldRegistry& worlds);
        ~SpatialIndexCache() override;
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        // Reads the residency cache limits (render.gpu_property_*) when EngineConfigControl is composed.
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;
        // `cpuIndex` false (ARCH-023: a capture only GPU queries read): the entry keeps its live rows' slots and points
        // but builds no CPU hierarchy (its snapshot is GPU-only, Builds unchanged; CPU queries answer nullopt). A later
        // acquisition with cpuIndex of the same current entry builds the hierarchy once into a new snapshot (Builds + 1;
        // the handle, rows, revision keys, device buffers and earlier snapshot leases stay).
        [[nodiscard]] SpatialIndexAcquisition Acquire(WorldHandle world,
            entt::entity entity,
            const GeometryPropertyRef& positions,
            SpatialIndexSpace space = SpatialIndexSpace::Property,
            bool cpuIndex = true);
        // Device-owner thread only. Owns private positions with identity row IDs.
        // Keep the snapshot lease alive while querying; dropping its last caller
        // lease expires the handle. Pending GPU work retains resources until safe.
        [[nodiscard]] SpatialIndexWorkspace CreateWorkspace(std::span<const glm::vec3> positions);
        // Replaces a private workspace's points (same count) for an iterative caller: the CPU
        // index is rebuilt into a new snapshot, the device index into its
        // existing buffers on its next GPU use (no new buffers or pipelines). False, leaving the
        // workspace as it was, for another handle or count, invalid points, or GPU work still
        // using it.
        [[nodiscard]] bool UpdateWorkspace(SpatialIndexWorkspace& workspace, std::span<const glm::vec3> positions);
        // Immutable CPU lease survives eviction; indices here are compact, Slots maps to original rows.
        [[nodiscard]] std::shared_ptr<const SpatialIndexSnapshot> Snapshot(SpatialIndexHandle handle) const;
        // Device-owner thread only. Every query batch and computation is submitted at once through
        // IDevice::SubmitComputeReadback (the compute lane; graphics where forced or single-queue)
        // and is Ready once its own readback arrived, with no frame delay: Submitted when accepted,
        // Failed when refused (never replayed). Missing gathers and hierarchy builds are recorded
        // into the same submission; the index counts them only once a submission is accepted.
        // Reuse a completed batch to retain its buffer allocation.
        // Results use original property row IDs, matching Nearest. Every batch query takes an
        // explicit strategy with one result contract: Lbvh (default) builds the GPU hierarchy
        // lazily; BruteForce scans the resident samples and builds none (GpuBuilds unchanged).
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuNearest(SpatialIndexHandle handle,
            std::span<const glm::vec3> queries,
            std::shared_ptr<SpatialNearestBatch> reuse = {},
            Graphics::PointQueryStrategy strategy = Graphics::PointQueryStrategy::Lbvh);
        // Exact kNN, 1..64; optional exclusions have one original property row ID per query.
        // Reuse requires the same query count and k. No CPU fallback on an unavailable device.
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuKNearest(SpatialIndexHandle handle,
            std::span<const glm::vec3> queries,
            std::uint32_t k,
            std::span<const std::uint32_t> excludedSlots = {},
            std::shared_ptr<SpatialNearestBatch> reuse = {},
            Graphics::PointQueryStrategy strategy = Graphics::PointQueryStrategy::Lbvh);
        // Inclusive radius; counts remain complete when retained hits exceed capacity (1..1024).
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuRadius(SpatialIndexHandle handle,
            std::span<const glm::vec3> queries,
            float radius,
            std::uint32_t capacity,
            std::span<const std::uint32_t> excludedSlots = {},
            std::shared_ptr<SpatialNearestBatch> reuse = {},
            Graphics::PointQueryStrategy strategy = Graphics::PointQueryStrategy::Lbvh);
        [[nodiscard]] bool GpuQueriesAvailable() const noexcept;
        // ARCH-023, device-owner thread, a run's Init: a worker query session over `target` (a current index; with
        // desc.Moving none, the handle ignored) under the same admission as the batches above. Null with the reason
        // in `diagnostic`. While it lives, a private workspace it targets refuses UpdateWorkspace.
        [[nodiscard]] std::shared_ptr<SpatialQuerySession> PrepareQuerySession(
            SpatialIndexHandle target, const SpatialQuerySessionDesc& desc, std::string& diagnostic);
        // Fixed source points stay uploaded while exact values match the completed batch.
        // Each iteration updates only a double-precision pose; requires shader float64.
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuNearestTransformed(SpatialIndexHandle handle,
            std::span<const glm::vec3> source,
            const glm::dmat4& transform,
            std::shared_ptr<SpatialNearestBatch> reuse = {},
            Graphics::PointQueryStrategy strategy = Graphics::PointQueryStrategy::Lbvh);
        // Record against the retained, current index on the device-owner thread; the recorder runs
        // inside this call. It owns its buffers (including a TransferSrc result of readbackBytes)
        // through captured leases, retained until the readback arrived. Residency slots it uses
        // must be NoteUse'd (or acquired) while recording: they are then guarded by the returned
        // compute token as well as their frame. Zero bytes requests completion only.
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueGpuCompute(SpatialIndexHandle handle,
            std::size_t readbackBytes,
            std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record);
        // Same recording and readback without an index; the view is empty.
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueGpuCompute(std::size_t readbackBytes,
            std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record);
        // ARCH-023, device-owner thread, a run's Init: the retained device view of `handle` (a current index) a
        // worker's kernel records against (SpatialGpuIndexLease), under QueueGpuCompute(handle, ...)'s admission
        // (at most 2^20 live rows). Null with the reason. While a lease lives, a private workspace it targets refuses
        // UpdateWorkspace.
        [[nodiscard]] std::shared_ptr<SpatialGpuIndexLease> PrepareGpuIndex(
            SpatialIndexHandle handle, std::string& diagnostic);
        // Stale world/entity/property/deletion revisions return nullopt; reacquire to rebuild.
        [[nodiscard]] std::optional<Geometry::PointLBVH::Neighbor> Nearest(
            SpatialIndexHandle handle, glm::vec3 query,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        [[nodiscard]] std::optional<Geometry::PointLBVH::RadiusResult> Radius(
            SpatialIndexHandle handle, glm::vec3 query, float radius, std::uint32_t capacity,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        [[nodiscard]] std::optional<std::vector<Geometry::PointLBVH::Neighbor>> KNearest(
            SpatialIndexHandle handle, glm::vec3 query, std::uint32_t k,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        void Prune();
        [[nodiscard]] SpatialIndexCacheStats Stats() const noexcept;
        // Canonical GPU copies of properties and method output rings (ADR 0030): a
        // property-space index over every row reads its positions from here, shared with every
        // other GPU user of that revision; GPU methods bind through Runtime.GpuPropertyBinding.
        // Created on first use; null without a device (const: null until the first use).
        [[nodiscard]] Graphics::GpuPropertyResidency* PropertyResidency() noexcept;
        [[nodiscard]] const Graphics::GpuPropertyResidency* PropertyResidency() const noexcept;
        // Accept of GPU-authored positions (RUNTIME-293, ADR 0030 decision 6): the published
        // rows and revision of an entity's `v:position` and the ring front that becomes its
        // canonical slot, handed to the render extraction this residency's observer is
        // registered on (`RenderExtractionCache::CommitAcceptedPositions`). For a 1:1 block
        // the shadow is patched and the revision acknowledged, so nothing is uploaded again;
        // `AcknowledgedCopyPending` when the block copies the front once at the next culling
        // head. `NotAcknowledged` without an extraction, for meshes, or when the block refuses
        // the bytes: the caller marks the positions dirty for the ordinary upload.
        struct GpuPositionCommit
        {
            std::uint32_t StableEntityId{};
            std::span<const std::byte> PositionBytes{};
            std::uint32_t RowCount{};
            std::uint64_t Revision{};
            RHI::BufferHandle Front{};
            std::uint64_t FrontAddress{}, FrontBytes{};
            std::uint32_t FrontCount{};
            std::uint64_t FrontStamp{};
            // The front slot's residency lease; a pending render copy holds it until done.
            std::shared_ptr<const void> FrontLease{};
        };
        enum class GpuPositionCommitStatus : std::uint8_t { Acknowledged, AcknowledgedCopyPending, NotAcknowledged };
        [[nodiscard]] GpuPositionCommitStatus CommitGpuPositions(const GpuPositionCommit& commit);
        // Device-owner thread. A device workspace of kind T for one run: the retained idle one when no recorded
        // submission can still use it, else a new one; null without a device. Overlapping leases never share an
        // object. Dropping the last copy (any thread) returns it; the owner thread then calls its ReleaseScratch()
        // (its buffers go back to MethodScratch) and stamps it with the scratch cache's retirement stamp. It is
        // leasable again once that stamp retired (GetFramesInFlight() + 1 frames and the compute-lane fence);
        // at most one idle workspace per kind keeps its pipelines until device-idle shutdown.
        // Capture the lease in the QueueGpuCompute recorder (or state it captures) and release it early only
        // after the result is Ready; a refused submission submitted nothing and is never replayed.
        // Each engine run owner instantiates its types through the private WorkspaceLease module; plugins see
        // only this declaration, so the pool parks no object, control block or deleter created by plugin code.
        template <class T> [[nodiscard]] std::shared_ptr<T> LeaseGpuWorkspace();
        // The device-owner thread's manager of method scratch buffers (null without a device, and after
        // shutdown): CreateScratch leases under the method budget, reused once retired. Leases must be dropped
        // before this module is destroyed; plugin-owned state must drop them before its plugin unloads.
        [[nodiscard]] RHI::BufferManager* MethodScratch() noexcept;
        // The method budget's refusal as an actionable diagnostic (bytes requested, committed and budgeted, and
        // the config key) when one happened since `refusals`, a previous Stats().ScratchRefusals; else `fallback`.
        [[nodiscard]] std::string MethodScratchDiagnostic(std::uint64_t refusals, std::string_view fallback) const;

    private:
        [[nodiscard]] std::shared_ptr<void> LeaseErasedGpuWorkspace(const void* kind,
            std::shared_ptr<void> (*make)(RHI::IDevice&, RHI::BufferManager&),
            void (*releaseScratch)(void*));
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuBatch(SpatialIndexHandle handle,
            std::span<const glm::vec3> queries,
            std::uint32_t capacity,
            float radius,
            std::span<const std::uint32_t> excludedSlots,
            std::shared_ptr<SpatialNearestBatch> reuse,
            Graphics::PointQueryStrategy strategy,
            const glm::dmat4* transform = nullptr);
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Extrinsic::Runtime
