module;

module Extrinsic.Core.Tasks;

#include "Core.Tasks.Internal.hpp"

namespace Extrinsic::Core::Tasks
{
    Scheduler::WaitToken Scheduler::AcquireWaitToken()
    {
        if (!s_Ctx)
            return {};

        std::lock_guard lock(s_Ctx->waitMutex);
        uint32_t slot = 0;
        if (!s_Ctx->freeWaitSlots.empty())
        {
            slot = s_Ctx->freeWaitSlots.back();
            s_Ctx->freeWaitSlots.pop_back();
        }
        else
        {
            slot = static_cast<uint32_t>(s_Ctx->waitSlots.size());
            s_Ctx->waitSlots.emplace_back();
        }

        auto& waitSlot = s_Ctx->waitSlots[slot];
        waitSlot.inUse = true;
        waitSlot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedCount = 0;
        waitSlot.ready = false;
        return WaitToken{slot, waitSlot.generation};
    }

    void Scheduler::ReleaseWaitToken(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return;

        std::lock_guard lock(s_Ctx->waitMutex);
        if (token.Slot >= s_Ctx->waitSlots.size())
            return;

        auto& slot = s_Ctx->waitSlots[token.Slot];
        if (!slot.inUse || slot.generation != token.Generation)
            return;

        slot.inUse = false;
        uint32_t node = slot.parkedHead;
        const uint32_t safetyLimit = slot.parkedCount + 1;
        uint32_t iterations = 0;
        while (node != Detail::SchedulerContext::InvalidParkedNode)
        {
            if (++iterations > safetyLimit)
                break;
            auto& parkedNode = s_Ctx->parkedNodes[node];
            const uint32_t next = parkedNode.next;
            parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
            parkedNode.continuation = {};
            s_Ctx->freeParkedNodes.push_back(node);
            node = next;
        }

        slot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
        slot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
        slot.parkedCount = 0;
        slot.ready = false;
        slot.generation++;
        if (slot.generation == 0)
            slot.generation = 1;

        s_Ctx->freeWaitSlots.push_back(token.Slot);
    }

    bool Scheduler::ParkCurrentFiberIfNotReady(WaitToken token, std::coroutine_handle<> h,
                                               std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx || !token.Valid() || !h)
            return false;

        const auto parkStart = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(s_Ctx->waitMutex);
            if (token.Slot >= s_Ctx->waitSlots.size())
                return false;
            auto& slot = s_Ctx->waitSlots[token.Slot];
            if (!slot.inUse || slot.generation != token.Generation)
                return false;

            if (slot.ready)
                return false;

            uint32_t parkedNodeIndex = Detail::SchedulerContext::InvalidParkedNode;
            if (!s_Ctx->freeParkedNodes.empty())
            {
                parkedNodeIndex = s_Ctx->freeParkedNodes.back();
                s_Ctx->freeParkedNodes.pop_back();
            }
            else
            {
                parkedNodeIndex = static_cast<uint32_t>(s_Ctx->parkedNodes.size());
                s_Ctx->parkedNodes.emplace_back();
            }

            auto& parkedNode = s_Ctx->parkedNodes[parkedNodeIndex];
            parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
            parkedNode.continuation = Detail::SchedulerContext::ParkedContinuation{
                .Handle = h,
                .Alive = std::move(alive),
                .ParkedAt = parkStart,
            };

            if (slot.parkedTail == Detail::SchedulerContext::InvalidParkedNode)
            {
                slot.parkedHead = parkedNodeIndex;
                slot.parkedTail = parkedNodeIndex;
            }
            else
            {
                s_Ctx->parkedNodes[slot.parkedTail].next = parkedNodeIndex;
                slot.parkedTail = parkedNodeIndex;
            }

            slot.parkedCount += 1;
            s_Ctx->parkCount.fetch_add(1, std::memory_order_relaxed);
            s_Ctx->parkCount.notify_all();
        }

        const auto parkNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - parkStart).count();
        s_Ctx->parkLatencyTotalNs.fetch_add(static_cast<uint64_t>(parkNs), std::memory_order_relaxed);
        Detail::RecordLatencySample(s_Ctx->parkLatencyHistogram, static_cast<uint64_t>(parkNs));
        return true;
    }

    uint32_t Scheduler::UnparkReady(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return 0;

        std::vector<Detail::SchedulerContext::ParkedContinuation> continuations;
        {
            std::lock_guard lock(s_Ctx->waitMutex);
            if (token.Slot >= s_Ctx->waitSlots.size())
                return 0;
            auto& slot = s_Ctx->waitSlots[token.Slot];
            if (!slot.inUse || slot.generation != token.Generation)
                return 0;

            slot.ready = true;
            if (slot.parkedHead == Detail::SchedulerContext::InvalidParkedNode)
                return 0;

            continuations.reserve(slot.parkedCount);
            uint32_t node = slot.parkedHead;
            const uint32_t safetyLimit = slot.parkedCount + 1;
            uint32_t iterations = 0;
            while (node != Detail::SchedulerContext::InvalidParkedNode)
            {
                if (++iterations > safetyLimit)
                    break;
                auto& parkedNode = s_Ctx->parkedNodes[node];
                const uint32_t next = parkedNode.next;
                parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
                continuations.push_back(std::move(parkedNode.continuation));
                parkedNode.continuation = {};
                s_Ctx->freeParkedNodes.push_back(node);
                node = next;
            }

            slot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
            slot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
            slot.parkedCount = 0;
        }

        if (continuations.empty())
            return 0;

        const auto now = std::chrono::steady_clock::now();
        for (auto& continuation : continuations)
        {
            const auto unparkNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - continuation.ParkedAt).count();
            s_Ctx->unparkLatencyTotalNs.fetch_add(static_cast<uint64_t>(unparkNs), std::memory_order_relaxed);
            s_Ctx->unparkCount.fetch_add(1, std::memory_order_relaxed);
            Detail::RecordLatencySample(s_Ctx->unparkLatencyHistogram, static_cast<uint64_t>(unparkNs));

            Reschedule(continuation.Handle, std::move(continuation.Alive));
        }

        return static_cast<uint32_t>(continuations.size());
    }

    void Scheduler::MarkWaitTokenNotReady(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return;

        std::lock_guard lock(s_Ctx->waitMutex);
        if (token.Slot >= s_Ctx->waitSlots.size())
            return;

        auto& slot = s_Ctx->waitSlots[token.Slot];
        if (!slot.inUse || slot.generation != token.Generation)
            return;

        slot.ready = false;
    }
}
