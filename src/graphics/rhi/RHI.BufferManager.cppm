// Leased buffer ownership and views, with opt-in cached scratch leases under a byte budget.
module;

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>

export module Extrinsic.RHI.BufferManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferView;
import Extrinsic.RHI.TransferQueue;
extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

// ============================================================
// BufferManager
// ============================================================
// Owns the lifetime of every GPU buffer allocated through it.
// Consumers hold a BufferLease — a ref-counted RAII wrapper
// around a BufferHandle that calls Release() on destruction.
//
// Thread-safety contract:
//   - Create() / CreateScratch() / CollectScratch() / DrainScratch()
//     must be called on the device-owner thread (they may call
//     IDevice::CreateBuffer / DestroyBuffer).
//   - Retain() / Release() / AcquireLease() / View() / GetDesc() take
//     the manager mutex and are safe from any thread.
//   - Release() reaching zero on a plain buffer calls
//     IDevice::DestroyBuffer — callers must ensure this happens on the
//     render thread. The last release of a scratch buffer only parks it
//     (no device call) until DrainScratch() has run.
//
// Lifetime contract:
//   Every BufferLease issued by this manager MUST be destroyed
//   before the manager itself is destroyed. A lingering lease
//   will call Release() on a manager whose internal state has
//   been freed — classic use-after-free. The destructor asserts
//   this invariant in Debug; Release builds with NDEBUG will
//   still fault but the contract is explicit here. Typical
//   culprits: raw pointer captures in long-lived lambdas,
//   BufferLease members of async-task state, caches that
//   outlive the frame graph.
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
//
// Scratch leases (ARCH-023): CreateScratch() serves a request from a
// power-of-two size class (at least MinScratchClassBytes) and the same
// Usage/HostVisible/HostReadback. Its last release parks the buffer as
// Returned; CollectScratch() stamps it Pending, and once the stamp is
// retired (IDevice::IsFrameRetired and the compute lane, so without new
// frames as well) it is Idle: reusable by the next
// matching request, and the only state the LRU eviction may destroy.
// Active, returned, pending and idle class bytes all count toward the
// budget; a request that cannot fit even after evicting every idle
// buffer is refused (ResourceBudgetExceeded) without any device call.
// Reused contents are undefined. GetDesc()/View() report the requested
// size; the class capacity is private to the cache.
// ============================================================

export namespace Extrinsic::RHI
{
    // Proof that no submission made before the stamp still uses a released buffer that frames never record (method
    // scratch, workspaces): its frame number retired (IDevice::IsFrameRetired: graphics-queue transfers, readbacks and
    // graphics-lane compute) and the compute-lane fence then current completed. A stamp taken between frames does not
    // wait for frames still in flight; a buffer a frame records needs that frame's own stamp.
    struct RetirementStamp
    {
        std::uint64_t Frame{};
        ReadbackToken ComputeFence{};
    };
    [[nodiscard]] RetirementStamp StampRetirement(IDevice& device);
    [[nodiscard]] bool IsRetired(IDevice& device, const RetirementStamp& stamp);

    // Physical bytes of the cached scratch states and allocation outcomes. Misses count device allocations a
    // cache miss requested; refusals count requests the budget (or an overflowing size) rejected.
    struct ScratchStats
    {
        std::uint64_t ActiveBytes{}, PendingBytes{}, IdleBytes{}, BudgetBytes{};
        std::uint64_t Hits{}, Misses{}, Refusals{}, Evictions{};
        // The last refused request's class bytes, and the active plus pending bytes it would have joined.
        std::uint64_t LastRefusedBytes{}, LastRefusalCommittedBytes{};
    };

    // C++ language linkage, as IDevice's: consumers forward-declare it without importing this module.
    extern "C++" class BufferManager
    {
    public:
        // Convenience alias — consumers spell out the full type once
        // (or import and use auto), so the manager stays self-contained.
        using BufferLease = Core::Lease<BufferHandle, BufferManager>;

        // Sentinel for View() — means "from offset to end of buffer".
        static constexpr std::uint64_t WholeBuffer = ~std::uint64_t{0};

        // Smallest scratch size class; requests above 2^63 bytes have no class (0).
        static constexpr std::uint64_t MinScratchClassBytes = 64;
        [[nodiscard]] static constexpr std::uint64_t ScratchClassBytes(const std::uint64_t requested) noexcept
        {
            return requested > (std::uint64_t{1} << 63u) ? 0u
                                                         : std::bit_ceil(std::max(requested, MinScratchClassBytes));
        }

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
        /// Returns a Core::ErrorCode on failure:
        ///   - OutOfDeviceMemory: IDevice::CreateBuffer returned an invalid handle.
        [[nodiscard]] Core::Expected<BufferLease> Create(const BufferDesc& desc);

        /// A cached scratch lease (see the class comment); refcount 1. Errors: DeviceNotOperational,
        /// OutOfRange (no size class), ResourceBudgetExceeded (no device call), OutOfDeviceMemory.
        [[nodiscard]] Core::Expected<BufferLease> CreateScratch(const BufferDesc& desc);
        /// Keeps `lease` when it is a buffer of `desc`'s memory class whose requested size covers
        /// desc.SizeBytes; otherwise replaces it through CreateScratch (the old buffer is released).
        /// False leaves `lease` empty.
        [[nodiscard]] bool EnsureScratch(BufferLease& lease, const BufferDesc& desc);
        /// Unlimited (all bits set) until set. Boot-time configuration; it never evicts by itself.
        void SetScratchBudget(std::uint64_t bytes) noexcept;
        /// Owner thread: stamps returned buffers with `returned` and makes pending buffers whose
        /// stamp retired idle.
        void CollectScratch(const RetirementStamp& returned);
        /// Owner thread, once the device is idle (and in the destructor): destroys every returned,
        /// pending and idle buffer; later scratch releases destroy their buffer directly.
        void DrainScratch();
        [[nodiscard]] ScratchStats GetScratchStats() const;

        // -----------------------------------------------------------------
        // LeasableManager concept requirements (called by Lease internals)
        // -----------------------------------------------------------------
        void Retain(BufferHandle handle);   ///< Increment refcount.
        void Release(BufferHandle handle);  ///< Decrement; destroy (or park scratch) at zero.

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

        /// Return the descriptor the buffer was created with (a scratch
        /// buffer: as requested), or nullptr if the handle is invalid /
        /// already released.
        [[nodiscard]] const BufferDesc* GetDesc(BufferHandle handle) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

