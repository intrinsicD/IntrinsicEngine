module;

#include <cstdint>
#include <memory>

export module Extrinsic.RHI.BufferManager;

import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferView;
import Extrinsic.RHI.Device;

// ============================================================
// BufferManager
// ============================================================
// Owns the lifetime of every GPU buffer allocated through it.
// Consumers hold a BufferLease — a ref-counted RAII wrapper
// around a BufferHandle that calls Release() on destruction.
//
// Thread-safety contract:
//   - Create() must be called on the render thread (it calls
//     IDevice::CreateBuffer which may touch driver state).
//   - Retain() / Release() are atomic (safe from any thread).
//   - Release() reaching zero calls IDevice::DestroyBuffer —
//     callers must ensure this happens on the render thread.
//     The conventional pattern is to move the lease onto the
//     render thread's deferred-destroy queue before the last
//     reference is dropped.
//   - View() / GetDesc() are read-only and lock-free after
//     Create() has returned.
//
// Usage:
//
//   BufferManager mgr{device};
//
//   // Allocate — refcount starts at 1, caller owns the lease.
//   auto lease = mgr.Create({.SizeBytes = 1024,
//                            .Usage     = BufferUsage::Vertex,
//                            .DebugName = "MyVB"});
//
//   // Bind a sub-range without transferring ownership.
//   BufferView vb = mgr.View(lease.GetHandle(), 0, 512);
//
//   // Share produces a second independent lease (refcount → 2).
//   auto lease2 = lease.Share();
//
//   // lease goes out of scope → refcount → 1.
//   // lease2 goes out of scope → refcount → 0 → DestroyBuffer.
// ============================================================

export namespace Extrinsic::RHI
{
    class BufferManager
    {
    public:
        // Convenience alias — consumers spell out the full type once
        // (or import and use auto), so the manager stays self-contained.
        using BufferLease = Core::Lease<BufferHandle, BufferManager>;

        // Sentinel for View() — means "from offset to end of buffer".
        static constexpr std::uint64_t WholeBuffer = ~std::uint64_t{0};

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        explicit BufferManager(IDevice& device);
        ~BufferManager();

        BufferManager(const BufferManager&)            = delete;
        BufferManager& operator=(const BufferManager&) = delete;

        // -----------------------------------------------------------------
        // Allocation
        // -----------------------------------------------------------------

        /// Allocate a new GPU buffer and return a ref-counted lease.
        /// Refcount starts at 1 — the returned lease is the sole owner.
        /// Returns an empty (invalid) lease if allocation fails.
        [[nodiscard]] BufferLease Create(const BufferDesc& desc);

        // -----------------------------------------------------------------
        // LeasableManager concept requirements (called by Lease internals)
        // -----------------------------------------------------------------
        void Retain(BufferHandle handle);   ///< Atomically increment refcount.
        void Release(BufferHandle handle);  ///< Decrement; destroy at zero.

        // -----------------------------------------------------------------
        // Secondary lease — called by Lease::Share()
        // -----------------------------------------------------------------
        /// Produce an independent lease for an already-live handle.
        /// Increments the refcount atomically before returning.
        [[nodiscard]] BufferLease AcquireLease(BufferHandle handle);

        // -----------------------------------------------------------------
        // Data access (separate from ownership)
        // -----------------------------------------------------------------

        /// Build a BufferView over [offset, offset+size) bytes.
        /// Pass WholeBuffer for size to span the full allocation.
        /// Returns an invalid BufferView when the handle is stale or
        /// offset / size exceed the allocation.
        [[nodiscard]] BufferView View(BufferHandle handle,
                                      std::uint64_t  offset = 0,
                                      std::uint64_t  size   = WholeBuffer) const noexcept;

        /// Return the descriptor the buffer was created with, or nullptr
        /// if the handle is invalid / already released.
        [[nodiscard]] const BufferDesc* GetDesc(BufferHandle handle) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

