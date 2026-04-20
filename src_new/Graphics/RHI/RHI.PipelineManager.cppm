module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module Extrinsic.RHI.PipelineManager;

import Extrinsic.Core.Lease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

// ============================================================
// PipelineManager
// ============================================================
// Owns GPU pipeline objects and provides:
//
//   1. Ref-counted leases (same Lease pattern as Buffer/Texture).
//   2. Transparent hot-reload: Recompile() swaps the underlying
//      device pipeline without invalidating any caller's
//      PipelineHandle — callers always hold a pool handle.
//   3. Compilation callback: an optional OnCompiled callback fires
//      on the render thread after a (re)compile so passes can
//      update their command state without polling.
//
// Pipeline state machine per slot:
//
//   Created ──► Ready ──► Recompiling ──► Ready
//                 │                         │
//                 └─────────────────────────┘
//                   (hot-reload cycle)
//
// While Recompiling, GetDeviceHandle() returns the PREVIOUS
// device pipeline so rendering continues uninterrupted.  The
// new pipeline is promoted to Ready inside CommitPending(), which
// must be called on the render thread once per frame (after
// IDevice::WaitIdle() or with appropriate synchronisation).
//
// Thread-safety:
//   - Create() / CommitPending() — render thread only.
//   - Recompile() — any thread (e.g. file-watcher thread);
//     the new IDevice::CreatePipeline call happens on the
//     calling thread, then the result is atomically staged.
//   - Retain() / Release() — atomic, any thread.
//   - GetDeviceHandle() / IsReady() — lock-free read, any thread.
//
// Usage:
//
//   PipelineManager mgr{device};
//
//   auto lease = mgr.Create(pipelineDesc, [](PipelineHandle h) {
//       // called on render thread after compile / hot-reload
//       myPass.OnPipelineReady(h);
//   });
//
//   // Each frame:
//   mgr.CommitPending();   // promotes completed Recompile() results
//
//   if (mgr.IsReady(lease.GetHandle()))
//       cmd.BindPipeline(mgr.GetDeviceHandle(lease.GetHandle()));
//
//   // Trigger hot-reload from any thread:
//   mgr.Recompile(lease.GetHandle(), newDesc);
// ============================================================

export namespace Extrinsic::RHI
{
    using PipelineCompiledCallback = std::function<void(PipelineHandle poolHandle)>;

    class PipelineManager
    {
    public:
        using PipelineLease = Core::Lease<PipelineHandle, PipelineManager>;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        explicit PipelineManager(IDevice& device);
        ~PipelineManager();

        PipelineManager(const PipelineManager&)            = delete;
        PipelineManager& operator=(const PipelineManager&) = delete;

        // -----------------------------------------------------------------
        // Allocation
        // -----------------------------------------------------------------

        /// Compile a new pipeline synchronously and return a lease.
        /// The optional callback fires on CommitPending() after every
        /// successful compile (initial + hot-reload).
        /// Returns an empty lease on compilation failure.
        [[nodiscard]] PipelineLease Create(const PipelineDesc&             desc,
                                           PipelineCompiledCallback         onCompiled = {});

        // -----------------------------------------------------------------
        // LeasableManager concept requirements
        // -----------------------------------------------------------------
        void Retain(PipelineHandle handle);
        void Release(PipelineHandle handle);

        // -----------------------------------------------------------------
        // Secondary lease — called by PipelineLease::Share()
        // -----------------------------------------------------------------
        [[nodiscard]] PipelineLease AcquireLease(PipelineHandle handle);

        // -----------------------------------------------------------------
        // Hot-reload
        // -----------------------------------------------------------------

        /// Recompile the pipeline from a new (or identical) descriptor.
        /// Safe to call from any thread (e.g. a file-watcher thread).
        /// The result is staged internally and promoted to active on the
        /// next CommitPending() call on the render thread.
        /// No-op on stale handles.
        void Recompile(PipelineHandle handle, const PipelineDesc& newDesc);

        /// Promote all staged pipeline compiles to active.
        /// Destroys the previous device pipeline for each promoted slot.
        /// Must be called on the render thread once per frame.
        void CommitPending();

        // -----------------------------------------------------------------
        // Data access (lock-free)
        // -----------------------------------------------------------------

        /// True when the pipeline has compiled successfully and is ready
        /// to bind.  False while the initial compile is in flight (if ever
        /// made async) or during a hot-reload cycle before CommitPending().
        [[nodiscard]] bool IsReady(PipelineHandle handle) const noexcept;

        /// The device handle to pass to ICommandContext::BindPipeline().
        /// Returns an invalid handle when !IsReady().
        [[nodiscard]] PipelineHandle GetDeviceHandle(PipelineHandle handle) const noexcept;

        /// Descriptor the pipeline was most recently compiled from.
        /// Reflects the NEW desc immediately after Recompile() is called,
        /// even before CommitPending() promotes it.
        [[nodiscard]] const PipelineDesc* GetDesc(PipelineHandle handle) const noexcept;

        /// Number of live pipeline slots (for diagnostics / editor UI).
        [[nodiscard]] std::uint32_t GetLiveCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

