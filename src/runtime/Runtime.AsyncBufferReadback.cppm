module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module Extrinsic.Runtime.AsyncBufferReadback;

import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

// ============================================================
// AsyncBufferReadback (RUNTIME-137)
// ============================================================
// A small, reusable async buffer readback for GPU compute backends.
//
// It composes `Graphics::GpuTransfer` (ScheduleReadback / IsDelivered) so a
// geometry/method GPU backend can drain its results WITHOUT the device-wide
// `vkDeviceWaitIdle` that `RHI::IDevice::ReadBuffer` performs on every call.
// The host destination is pooled and reused across sequential drains, removing
// the per-drain `std::vector` allocation that pushed backends toward the
// stalling default (see docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md
// Finding 1).
//
// Contract:
//   - One in-flight readback per instance. `Enqueue` while a prior readback is
//     still `Pending` fails closed (returns false), which keeps the pooled
//     destination stable for the transfer's deferred delivery — the sink holds
//     a span into the pooled buffer and must not be reallocated under it.
//   - The barrier (`ShaderWrite -> TransferRead` by default) and the download
//     are recorded into the caller's command context; delivery happens later
//     from the transfer queue's `CollectCompleted()` / `GpuTransfer::DrainCompleted`.
//   - `Poll()` advances `Pending -> Ready` once the transfer reports delivery;
//     no thread ever blocks on a GPU fence here.
// ============================================================

export namespace Extrinsic::Runtime
{
    enum class AsyncReadbackStatus : std::uint8_t
    {
        Idle,
        Pending,
        Ready,
    };

    struct AsyncBufferReadbackRequest
    {
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        RHI::BufferRange SourceRange{};
        RHI::MemoryAccess SourceAccess{RHI::MemoryAccess::ShaderWrite};
    };

    class AsyncBufferReadback
    {
    public:
        explicit AsyncBufferReadback(Graphics::GpuTransfer& transfer) noexcept;

        AsyncBufferReadback(const AsyncBufferReadback&) = delete;
        AsyncBufferReadback& operator=(const AsyncBufferReadback&) = delete;

        // Record the readback barrier + download into `cmd`, reusing the pooled
        // destination. Returns false (leaving status Idle) when a prior readback
        // is still Pending, the request is empty/invalid, or the transfer queue
        // rejects the download (e.g. a source without TransferSrc support).
        [[nodiscard]] bool Enqueue(RHI::ICommandContext& cmd,
                                   const AsyncBufferReadbackRequest& request);

        // Advance Pending -> Ready when the transfer reports delivery. Returns
        // true once the bytes are Ready. Safe to call repeatedly and never blocks.
        bool Poll() noexcept;

        [[nodiscard]] AsyncReadbackStatus Status() const noexcept { return m_Status; }
        [[nodiscard]] bool IsReady() const noexcept { return m_Status == AsyncReadbackStatus::Ready; }

        // Delivered bytes; non-empty only when IsReady().
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
        {
            if (m_Status != AsyncReadbackStatus::Ready)
                return {};
            return std::span<const std::byte>{m_Destination.data(), m_ReadySize};
        }

        // Typed view over the delivered bytes; empty when not Ready or when the
        // delivered size is not a whole multiple of sizeof(T).
        template <class T>
        [[nodiscard]] std::span<const T> BytesAs() const noexcept
        {
            const std::span<const std::byte> bytes = Bytes();
            if (bytes.empty() || bytes.size_bytes() % sizeof(T) != 0u)
                return {};
            return std::span<const T>{reinterpret_cast<const T*>(bytes.data()),
                                      bytes.size_bytes() / sizeof(T)};
        }

        // Return to Idle for the next drain, retaining pooled host capacity.
        void Reset() noexcept;

        [[nodiscard]] std::size_t PooledCapacityBytes() const noexcept;

    private:
        Graphics::GpuTransfer* m_Transfer{nullptr};
        std::vector<std::byte> m_Destination{};
        std::size_t m_ReadySize{0u};
        Graphics::GpuTransferReadbackTicket m_Ticket{};
        AsyncReadbackStatus m_Status{AsyncReadbackStatus::Idle};
    };
}
