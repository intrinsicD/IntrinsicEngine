module;

#include <mutex>
#include <chrono>
#include <atomic>
#include <coroutine>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
    namespace Detail
    {
        std::vector<SchedulerContext::ParkedContinuation>
        TakeParkedContinuationsLocked(SchedulerContext::WaitShard& shard,
                                      SchedulerContext::WaitSlot& slot)
        {
            std::vector<SchedulerContext::ParkedContinuation> continuations;
            continuations.reserve(slot.parkedCount);

            uint32_t node = slot.parkedHead;
            const uint32_t safetyLimit = slot.parkedCount + 1;
            uint32_t iterations = 0;
            while (node != SchedulerContext::InvalidParkedNode)
            {
                if (++iterations > safetyLimit)
                    break;
                auto& parkedNode = shard.ParkedNodes[node];
                const uint32_t next = parkedNode.next;
                parkedNode.next = SchedulerContext::InvalidParkedNode;
                continuations.push_back(std::move(parkedNode.continuation));
                parkedNode.continuation = {};
                shard.FreeParkedNodes.push_back(node);
                node = next;
            }

            slot.parkedHead = SchedulerContext::InvalidParkedNode;
            slot.parkedTail = SchedulerContext::InvalidParkedNode;
            slot.parkedCount = 0;
            return continuations;
        }

        void DestroyParkedContinuations(
            std::vector<SchedulerContext::ParkedContinuation>& continuations) noexcept
        {
            for (auto& continuation : continuations)
            {
                if (continuation.Alive)
                    continuation.Alive->store(false, std::memory_order_release);

                const std::coroutine_handle<> handle = continuation.Handle;
                continuation.Handle = {};
                continuation.Alive.reset();
                if (handle)
                    handle.destroy();
            }
        }
    }

    namespace
    {
        [[nodiscard]] constexpr std::uint32_t WaitShardIndex(
            const Scheduler::WaitToken token) noexcept
        {
            return token.Slot %
                static_cast<std::uint32_t>(Detail::WaitShardCount);
        }

        [[nodiscard]] constexpr std::uint32_t WaitShardLocalSlot(
            const Scheduler::WaitToken token) noexcept
        {
            return token.Slot /
                static_cast<std::uint32_t>(Detail::WaitShardCount);
        }

        [[nodiscard]] constexpr std::uint32_t EncodeWaitSlot(
            const std::uint32_t shardIndex,
            const std::uint32_t localSlot) noexcept
        {
            return localSlot *
                       static_cast<std::uint32_t>(Detail::WaitShardCount) +
                   shardIndex;
        }
    }

    Scheduler::WaitToken Scheduler::AcquireWaitToken()
    {
        if (!s_Ctx)
            return {};

        const std::uint32_t shardIndex =
            s_Ctx->nextWaitShard.fetch_add(1u, std::memory_order_relaxed) %
            static_cast<std::uint32_t>(Detail::WaitShardCount);
        auto& shard = s_Ctx->waitShards[shardIndex];
        std::lock_guard lock(shard.Mutex);
        uint32_t localSlot = 0;
        if (!shard.FreeSlots.empty())
        {
            localSlot = shard.FreeSlots.back();
            shard.FreeSlots.pop_back();
        }
        else
        {
            localSlot = static_cast<uint32_t>(shard.Slots.size());
            shard.Slots.emplace_back();
        }

        auto& waitSlot = shard.Slots[localSlot];
        waitSlot.inUse = true;
        waitSlot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedCount = 0;
        waitSlot.ready = false;
        return WaitToken{
            EncodeWaitSlot(shardIndex, localSlot),
            waitSlot.generation,
            s_Ctx->instanceId,
        };
    }

    void Scheduler::ReleaseWaitToken(WaitToken token)
    {
        if (!s_Ctx || !token.Valid() || token.SchedulerInstance != s_Ctx->instanceId)
            return;

        std::vector<Detail::SchedulerContext::ParkedContinuation> abandoned;
        {
            auto& shard = s_Ctx->waitShards[WaitShardIndex(token)];
            const std::uint32_t localSlot = WaitShardLocalSlot(token);
            std::lock_guard lock(shard.Mutex);
            if (localSlot >= shard.Slots.size())
                return;

            auto& slot = shard.Slots[localSlot];
            if (!slot.inUse || slot.generation != token.Generation)
                return;

            slot.inUse = false;
            abandoned = Detail::TakeParkedContinuationsLocked(shard, slot);
            slot.ready = false;
            slot.generation++;
            if (slot.generation == 0)
                slot.generation = 1;

            shard.FreeSlots.push_back(localSlot);
        }

        Detail::DestroyParkedContinuations(abandoned);
    }

    bool Scheduler::ParkCurrentFiberIfNotReady(WaitToken token, std::coroutine_handle<> h,
                                               std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx || !token.Valid() || !h ||
            token.SchedulerInstance != s_Ctx->instanceId)
            return false;

        const auto parkStart = std::chrono::steady_clock::now();
        {
            auto& shard = s_Ctx->waitShards[WaitShardIndex(token)];
            const std::uint32_t localSlot = WaitShardLocalSlot(token);
            std::lock_guard lock(shard.Mutex);
            if (localSlot >= shard.Slots.size())
                return false;
            auto& slot = shard.Slots[localSlot];
            if (!slot.inUse || slot.generation != token.Generation)
                return false;

            if (slot.ready)
                return false;

            uint32_t parkedNodeIndex = Detail::SchedulerContext::InvalidParkedNode;
            if (!shard.FreeParkedNodes.empty())
            {
                parkedNodeIndex = shard.FreeParkedNodes.back();
                shard.FreeParkedNodes.pop_back();
            }
            else
            {
                parkedNodeIndex =
                    static_cast<uint32_t>(shard.ParkedNodes.size());
                shard.ParkedNodes.emplace_back();
            }

            auto& parkedNode = shard.ParkedNodes[parkedNodeIndex];
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
                shard.ParkedNodes[slot.parkedTail].next = parkedNodeIndex;
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
        if (!s_Ctx || !token.Valid() || token.SchedulerInstance != s_Ctx->instanceId)
            return 0;

        std::vector<Detail::SchedulerContext::ParkedContinuation> continuations;
        {
            auto& shard = s_Ctx->waitShards[WaitShardIndex(token)];
            const std::uint32_t localSlot = WaitShardLocalSlot(token);
            std::lock_guard lock(shard.Mutex);
            if (localSlot >= shard.Slots.size())
                return 0;
            auto& slot = shard.Slots[localSlot];
            if (!slot.inUse || slot.generation != token.Generation)
                return 0;

            slot.ready = true;
            if (slot.parkedHead == Detail::SchedulerContext::InvalidParkedNode)
                return 0;

            continuations = Detail::TakeParkedContinuationsLocked(shard, slot);
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
        if (!s_Ctx || !token.Valid() || token.SchedulerInstance != s_Ctx->instanceId)
            return;

        auto& shard = s_Ctx->waitShards[WaitShardIndex(token)];
        const std::uint32_t localSlot = WaitShardLocalSlot(token);
        std::lock_guard lock(shard.Mutex);
        if (localSlot >= shard.Slots.size())
            return;

        auto& slot = shard.Slots[localSlot];
        if (!slot.inUse || slot.generation != token.Generation)
            return;

        slot.ready = false;
    }
}
