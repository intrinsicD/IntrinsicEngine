// ARCH-023: the worker side of a resident method's own GPU pages, shared by the shared GPU transaction core,
// resident LOP/WLOP and the CPD E-step (the pattern K-Means' worker step introduced). A page is recorded and
// submitted on the calling thread (a step run's worker, or the owner at a rest) with a pulled readback, waited for
// in bounded slices that check the run's abort, and its bytes are taken exactly once or given up, so a page never
// needs CollectCompleted or a frame. The owner seals the run before releasing what pages read or write: once Seal
// returned no page is submitted any more, so the transfer queue's ComputeFence() covers every use (stamp residency
// slots with it before their leases go). The recorder may only record commands into buffers prepared on the owner
// (no resource creation or destruction; RHI::IDevice::SubmitComputeReadback).
// Include after the RHI Device, TransferQueue and CommandContext imports; the including unit supplies atomic,
// chrono, cstdint, functional, mutex, span, string and utility.
#pragma once

#include "Runtime.ComputePageWait.hpp"

extern "C++"
{
namespace Extrinsic::Runtime
{
    enum class WorkerGpuPageStatus : std::uint8_t
    {
        Complete, // the page ran; its bytes were taken
        Aborted,  // the run was sealed or given up; nothing more is submitted (the page, if any, was given up)
        Refused,  // nothing was submitted (the recorder refused, or the device did)
        Failed,   // the device was lost, or the completed bytes could not be taken
        TimedOut, // the page did not complete within the caller's limit (given up; it may still run)
    };

    class WorkerGpuPages
    {
    public:
        // Owner: captures the device and its transfer queue (the worker never asks the device for them).
        explicit WorkerGpuPages(RHI::IDevice& device,
            RHI::ComputeInputOrder initialOrder = RHI::ComputeInputOrder::AfterIssuedTransfers) noexcept
            : m_Device(&device), m_Transfer(&device.GetTransferQueue()), m_InputOrder(initialOrder) {}
        WorkerGpuPages(const WorkerGpuPages&) = delete;
        WorkerGpuPages& operator=(const WorkerGpuPages&) = delete;

        // Any thread: submits `record` and waits for it (at most `timeout`), taking exactly `out.size()` bytes of
        // the buffer it returns (0: completion only). `Aborted` when the run was aborted or sealed before the submission.
        [[nodiscard]] WorkerGpuPageStatus Run(const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record,
            const std::span<std::byte> out,
            const std::chrono::steady_clock::duration timeout = std::chrono::steady_clock::duration::max())
        {
            RHI::ReadbackToken token{};
            {
                const std::scoped_lock lock{m_Mutex};
                if (Aborted())
                    return WorkerGpuPageStatus::Aborted;
                token = m_Device->SubmitComputeReadback(record, out.size(), RHI::ReadbackSink::Pull(), m_InputOrder);
                if (!token.IsValid())
                    return WorkerGpuPageStatus::Refused;
                m_InputOrder = RHI::ComputeInputOrder::LaneOrderOnly;
                m_Last.store(token.Value, std::memory_order_release);
                m_Submitted.fetch_add(1u, std::memory_order_relaxed);
            }
            if (const auto waited = Await(token, timeout); waited != WorkerGpuPageStatus::Complete)
            {
                m_Transfer->ReleaseReadback(token); // its bytes go once the device finished
                return waited;
            }
            if (!m_Transfer->TakeCompletedReadback(token, out))
            {
                m_Transfer->ReleaseReadback(token);
                return WorkerGpuPageStatus::Failed;
            }
            m_ReadbackBytes.fetch_add(out.size(), std::memory_order_relaxed);
            return WorkerGpuPageStatus::Complete;
        }

        // Any thread: a device copy that is not waited for (a preview into a slot the owner offered), with a
        // completion-only ordinary token the owner polls (IsComplete). Invalid: nothing submitted (aborted or sealed too).
        [[nodiscard]] RHI::ReadbackToken SubmitCopy(const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record)
        {
            const std::scoped_lock lock{m_Mutex};
            if (Aborted())
                return {};
            const auto token =
                m_Device->SubmitComputeReadback(record, 0u, RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}), m_InputOrder);
            if (token.IsValid())
            {
                m_InputOrder = RHI::ComputeInputOrder::LaneOrderOnly;
                m_Last.store(token.Value, std::memory_order_release);
                m_Submitted.fetch_add(1u, std::memory_order_relaxed);
            }
            return token;
        }

        // Any thread: a waiting page gives up at its next slice, nothing more is submitted; the owner then seals.
        void Abort() noexcept { m_Aborted.store(true, std::memory_order_release); }
        [[nodiscard]] bool Aborted() const noexcept { return m_Aborted.load(std::memory_order_acquire) || Sealed(); }
        // Owner, before releasing what the pages use: after Seal no page or copy is submitted any more (a submission
        // under way finishes first), so ComputeFence() covers them all. The worker's wait gives up too.
        void Seal()
        {
            m_Aborted.store(true, std::memory_order_release);
            const std::scoped_lock lock{m_Mutex};
            m_Sealed.store(true, std::memory_order_release);
        }
        [[nodiscard]] bool Sealed() const noexcept { return m_Sealed.load(std::memory_order_acquire); }
        // Owner: a restart (nothing in flight) may submit again; a sealed run stays sealed.
        // A new generation orders its first page after its uploads. LaneOrderOnly is valid when owner Init already
        // submitted that ordering page, or when all inputs are mapped host writes (e.g. CPD).
        void ClearAbort(RHI::ComputeInputOrder initialOrder = RHI::ComputeInputOrder::AfterIssuedTransfers) noexcept
        {
            const std::scoped_lock lock{m_Mutex};
            if (!Sealed())
            {
                m_InputOrder = initialOrder;
                m_Aborted.store(false, std::memory_order_release);
            }
        }

        // Every page's and copy's last token (0: none), how many were submitted and the bytes taken.
        [[nodiscard]] RHI::ReadbackToken Last() const noexcept { return {m_Last.load(std::memory_order_acquire)}; }
        [[nodiscard]] std::uint32_t Submitted() const noexcept { return m_Submitted.load(std::memory_order_acquire); }
        [[nodiscard]] std::uint64_t ReadbackBytes() const noexcept { return m_ReadbackBytes.load(std::memory_order_acquire); }

    private:
        WorkerGpuPageStatus Await(const RHI::ReadbackToken token, const std::chrono::steady_clock::duration timeout)
        {
            switch (WaitForComputePage(*m_Transfer, token, [this] { return Aborted(); }, timeout))
            {
            case ComputePageWait::Complete: return WorkerGpuPageStatus::Complete;
            case ComputePageWait::Abandoned: return WorkerGpuPageStatus::Aborted;
            case ComputePageWait::Failed: return WorkerGpuPageStatus::Failed;
            case ComputePageWait::TimedOut: return WorkerGpuPageStatus::TimedOut;
            }
            return WorkerGpuPageStatus::Failed;
        }

        RHI::IDevice* m_Device;
        RHI::ITransferQueue* m_Transfer;
        std::mutex m_Mutex{};
        RHI::ComputeInputOrder m_InputOrder; // guarded by m_Mutex; later pages never join unrelated graphics uploads
        std::atomic<bool> m_Aborted{false}, m_Sealed{false};
        std::atomic<std::uint64_t> m_Last{0u}, m_ReadbackBytes{0u};
        std::atomic<std::uint32_t> m_Submitted{0u};
    };
}
}
