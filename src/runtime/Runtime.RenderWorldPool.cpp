module;

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

module Extrinsic.Runtime.RenderWorldPool;

namespace Extrinsic::Runtime
{
    RenderWorldPool::RenderWorldPool(std::uint32_t bufferCount) noexcept(false)
    {
        const std::uint32_t clamped =
            std::clamp(bufferCount, kMinBuffers, kMaxBuffers);
        // Value-initialize the slots in place; `Slot` holds atomics and is
        // neither copyable nor movable, so the count constructor (which
        // default-inserts) plus a vector move-assign is the only safe path.
        m_Slots = std::vector<Slot>(static_cast<std::size_t>(clamped));
    }

    std::uint32_t RenderWorldPool::RefCount(std::uint32_t slot) const noexcept
    {
        if (slot >= m_Slots.size())
            return 0u;
        return m_Slots[slot].RefCount.load(std::memory_order_acquire);
    }

    std::uint32_t RenderWorldPool::FindFreeSlot() const noexcept
    {
        const std::uint32_t front = m_Front.load(std::memory_order_acquire);
        for (std::uint32_t i = 0u; i < m_Slots.size(); ++i)
        {
            const Slot& slot = m_Slots[i];
            if (slot.PendingReclaim)
                continue;
            if (i == front || i == m_Back)
                continue;
            if (slot.RefCount.load(std::memory_order_acquire) != 0u)
                continue;
            return i;
        }
        return kInvalidSlot;
    }

    std::uint32_t RenderWorldPool::FreeSlotCount() const noexcept
    {
        const std::uint32_t front = m_Front.load(std::memory_order_acquire);
        std::uint32_t count = 0u;
        for (std::uint32_t i = 0u; i < m_Slots.size(); ++i)
        {
            const Slot& slot = m_Slots[i];
            if (slot.PendingReclaim)
                continue;
            if (i == front || i == m_Back)
                continue;
            if (slot.RefCount.load(std::memory_order_acquire) != 0u)
                continue;
            ++count;
        }
        return count;
    }

    void RenderWorldPool::DrainReclamations() noexcept
    {
        const std::uint32_t front = m_Front.load(std::memory_order_acquire);
        for (std::uint32_t i = 0u; i < m_Slots.size(); ++i)
        {
            Slot& slot = m_Slots[i];
            if (!slot.PendingReclaim)
                continue;
            // A reclaimable slot returns to the free list only once it is no
            // longer referenced and no longer the published front
            // (`GRAPHICS-036` decision 4).
            if (i != front && slot.RefCount.load(std::memory_order_acquire) == 0u)
                slot.PendingReclaim = false;
        }
    }

    std::uint32_t RenderWorldPool::AcquireBack(std::uint64_t frameIndex) noexcept
    {
        // Synchronous mode (decision 6): one logical buffer, overwritten in
        // place every frame. No back-pressure counter fires; reuse is by design.
        if (m_Slots.size() == 1u)
        {
            m_Back                     = 0u;
            m_Slots[0].PendingReclaim  = false;
            m_Slots[0].PublishedFrame  = frameIndex;
            return 0u;
        }

        DrainReclamations();

        std::uint32_t slot = FindFreeSlot();
        if (slot == kInvalidSlot)
        {
            // Producer faster than consumer (decision 5): no slot is free.
            ++m_Diagnostics.ExtractionSkipCount;
            // Overwrite the still-unpublished back slot when one exists: it was
            // never published, so the consumer never took a reference on it
            // (its refcount is 0). Otherwise every remaining slot is a published
            // front the consumer/GPU still holds in flight (refcount > 0), so
            // the pool is exhausted: reusing any of them would let this
            // extraction overwrite storage an in-flight frame still references,
            // violating the refcount lifecycle the pool guarantees. In that
            // case fail closed — return kInvalidSlot so the producer skips this
            // extraction and keeps the previous front current — instead of
            // handing out a referenced slot.
            if (m_Back != kInvalidSlot &&
                m_Slots[m_Back].RefCount.load(std::memory_order_acquire) == 0u)
            {
                slot = m_Back;
            }
            else
            {
                return kInvalidSlot;
            }
        }

        m_Back                    = slot;
        m_Slots[slot].PendingReclaim = false;
        m_Slots[slot].PublishedFrame = frameIndex;
        return slot;
    }

    void RenderWorldPool::PublishFront(std::uint32_t slot) noexcept
    {
        if (slot >= m_Slots.size())
            return;
        const std::uint32_t previous = m_Front.load(std::memory_order_acquire);
        m_PreviousFront.store(previous, std::memory_order_release);
        // Slot contents are fully written by the producer before this release
        // store, so a consumer that acquires the index never sees a torn slot.
        m_Front.store(slot, std::memory_order_release);
        // Publish-sequence bump is the "new snapshot available" signal; it is
        // index-independent, so a synchronous pool that re-publishes the same
        // slot every frame is not mistaken for a consumer stall.
        m_PublishSeq.fetch_add(1u, std::memory_order_release);
        if (slot == m_Back)
            m_Back = kInvalidSlot;
    }

    std::uint32_t RenderWorldPool::AcquireFront(std::uint64_t frameIndex) noexcept
    {
        const std::uint32_t front = m_Front.load(std::memory_order_acquire);
        if (front == kInvalidSlot)
            return kInvalidSlot; // nothing published yet

        // Consumer faster than producer (decision 5): no new front published
        // since the last consume -> reuse the current front and count a stall.
        const std::uint64_t publishSeq = m_PublishSeq.load(std::memory_order_acquire);
        if (publishSeq == m_LastConsumedSeq)
            ++m_Diagnostics.PipelineStallCount;
        m_LastConsumedSeq = publishSeq;

        m_Slots[front].RefCount.fetch_add(1u, std::memory_order_acq_rel);

        const std::uint64_t published = m_Slots[front].PublishedFrame;
        m_Diagnostics.LastConsumedFrameAge =
            (frameIndex >= published) ? (frameIndex - published) : 0u;
        return front;
    }

    std::uint32_t RenderWorldPool::AcquirePreviousFront(std::uint64_t frameIndex) noexcept
    {
        std::uint32_t front = m_PreviousFront.load(std::memory_order_acquire);
        if (front == kInvalidSlot)
            front = m_Front.load(std::memory_order_acquire);
        if (front == kInvalidSlot || front >= m_Slots.size())
            return kInvalidSlot;

        // This is the intentional render-N-1 consume path, not a
        // consumer-faster-than-producer stall. Mark the current publish
        // sequence consumed so a later normal AcquireFront() call observes the
        // same baseline.
        m_LastConsumedSeq = m_PublishSeq.load(std::memory_order_acquire);
        m_Slots[front].RefCount.fetch_add(1u, std::memory_order_acq_rel);

        const std::uint64_t published = m_Slots[front].PublishedFrame;
        m_Diagnostics.LastConsumedFrameAge =
            (frameIndex >= published) ? (frameIndex - published) : 0u;
        return front;
    }

    void RenderWorldPool::ReleaseFront(std::uint32_t slot) noexcept
    {
        if (slot >= m_Slots.size())
            return;

        const std::uint32_t prev =
            m_Slots[slot].RefCount.load(std::memory_order_acquire);
        if (prev == 0u)
            return; // double release; nothing to do

        const std::uint32_t now =
            m_Slots[slot].RefCount.fetch_sub(1u, std::memory_order_acq_rel) - 1u;
        if (now != 0u)
            return;

        // Refcount reached zero: mark for reclamation unless it is still the
        // published front (a still-current front stays consumable).
        if (slot != m_Front.load(std::memory_order_acquire))
            m_Slots[slot].PendingReclaim = true;
    }
}
