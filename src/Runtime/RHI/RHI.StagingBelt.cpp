module;
#include <cassert>
#include <algorithm>
#include <mutex>
#include <cstddef>
#include <memory>
#include "RHI.Vulkan.hpp"

module RHI:StagingBelt.Impl;

import :StagingBelt;
import Core;

namespace
{
    [[nodiscard]] constexpr bool IsPowerOfTwo(size_t v) noexcept { return v && ((v & (v - 1)) == 0); }
}

namespace RHI
{
    size_t StagingBelt::AlignUp(size_t value, size_t alignment)
    {
        if (alignment == 0) return value;
        // Vulkan alignments are typically powers of two.
        if (IsPowerOfTwo(alignment))
            return (value + alignment - 1) & ~(alignment - 1);
        const size_t rem = value % alignment;
        return rem == 0 ? value : (value + (alignment - rem));
    }

    StagingBelt::StagingBelt(std::shared_ptr<VulkanDevice> device, size_t capacityBytes)
        : m_Device(std::move(device))
        , m_Capacity(capacityBytes)
    {
        // We want persistent mapped, sequential write.
        // This repo's VulkanBuffer sets VMA mapped flag for VMA_MEMORY_USAGE_CPU_TO_GPU or AUTO_PREFER_HOST.
        // CPU_ONLY does not request persistent mapping.
        m_Buffer = std::make_unique<VulkanBuffer>(m_Device, m_Capacity,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_MappedBase = m_Buffer->Map();
        m_PendingBegin = 0;
        m_PendingEnd = 0;
        m_HasPending = false;

        Core::Log::Info("StagingBelt created: {} bytes", m_Capacity);
    }

    StagingBelt::~StagingBelt()
    {
        // Ensure we don't keep a mapping alive during destruction.
        if (m_Buffer)
            m_Buffer->Unmap();
    }

    bool StagingBelt::HasSpaceContiguous(size_t alignedTail, size_t sizeBytes) const
    {
        if (sizeBytes > m_Capacity) return false;

        if (m_Head <= alignedTail)
        {
            // Free space is [alignedTail, cap) and [0, head)
            if (alignedTail + sizeBytes <= m_Capacity)
                return true;
            // Wrap case will be handled by caller.
            return sizeBytes <= m_Head;
        }

        // head > tail: free space is [alignedTail, head)
        return alignedTail + sizeBytes <= m_Head;
    }

    StagingBelt::Allocation StagingBelt::Allocate(size_t sizeBytes, size_t alignment)
    {
        std::lock_guard lock(m_Mutex);

        if (sizeBytes == 0)
            return {};

        // We can't safely reuse regions until GC processes.
        // If ring is full, we fail-fast; caller may fall back to dedicated staging.
        size_t alignedTail = AlignUp(m_Tail, std::max<size_t>(alignment, 1));

        if (!HasSpaceContiguous(alignedTail, sizeBytes))
        {
            // Try wrapping to 0
            alignedTail = 0;
            alignedTail = AlignUp(alignedTail, std::max<size_t>(alignment, 1));

            if (!HasSpaceContiguous(alignedTail, sizeBytes))
            {
                Core::Log::Warn("StagingBelt out of space (req {} bytes, align {}). Capacity={} Head={} Tail={} InFlight={}",
                               sizeBytes, alignment, m_Capacity, m_Head, m_Tail, m_InFlight.size());
                return {};
            }

            m_Tail = 0;
        }

        const size_t begin = alignedTail;
        const size_t end = begin + sizeBytes;

        m_Tail = end;

        // Merge into pending range (for the next Retire)
        if (!m_HasPending)
        {
            m_PendingBegin = begin;
            m_PendingEnd = end;
            m_HasPending = true;
        }
        else
        {
            // Pending can be disjoint if we wrapped mid-batch. In that case we conservatively
            // close previous pending and start a new one by forcing caller to Retire between wraps.
            // For now, disallow wrap while pending.
            if (begin < m_PendingBegin)
            {
                Core::Log::Warn("StagingBelt allocation wrapped during pending batch. Call Retire() before wrap-heavy allocations.");
                // Treat as new pending chunk
                m_PendingBegin = begin;
                m_PendingEnd = end;
            }
            else
            {
                m_PendingEnd = std::max(m_PendingEnd, end);
            }
        }

        Allocation out;
        out.Buffer = m_Buffer->GetHandle();
        out.Offset = begin;
        out.MappedPtr = static_cast<std::byte*>(m_MappedBase) + begin;
        out.Size = sizeBytes;
        return out;
    }

    void StagingBelt::Retire(uint64_t retireValue)
    {
        if (retireValue == 0)
            return;

        std::lock_guard lock(m_Mutex);
        if (!m_HasPending)
            return;

        Region r;
        r.Begin = m_PendingBegin;
        r.End = m_PendingEnd;
        r.RetireValue = retireValue;
        m_InFlight.push_back(r);

        m_HasPending = false;
        m_PendingBegin = m_PendingEnd = 0;
    }

    void StagingBelt::GarbageCollect(uint64_t completedValue)
    {
        std::lock_guard lock(m_Mutex);

        while (!m_InFlight.empty() && m_InFlight.front().RetireValue <= completedValue)
        {
            const Region r = m_InFlight.front();
            m_InFlight.pop_front();

            // Advance head to end of region if it matches.
            // Because regions are retired in allocation order, this is safe.
            m_Head = r.End;
            if (m_Head >= m_Capacity)
                m_Head = 0;
        }

        // If everything is free, compact pointers.
        if (m_InFlight.empty() && !m_HasPending)
        {
            m_Head = m_Tail;
        }
    }

    StagingBelt::Allocation StagingBelt::AllocateForImageUpload(size_t sizeBytes,
                                                                size_t texelBlockSize,
                                                                size_t rowPitchBytes,
                                                                size_t optimalBufferCopyOffsetAlignment,
                                                                size_t optimalBufferCopyRowPitchAlignment)
    {
        // Vulkan spec: bufferOffset must be multiple of optimalBufferCopyOffsetAlignment.
        // Additionally, if bufferRowLength is 0 (tightly packed), the implicit row pitch is
        // imageWidth * texelBlockSize and should satisfy optimalBufferCopyRowPitchAlignment on some impls.
        // We conservatively fold rowPitch alignment into the allocation alignment.
        size_t alignment = 1;
        alignment = std::max(alignment, optimalBufferCopyOffsetAlignment);

        // If caller provides a known rowPitch, align to satisfy row pitch limits.
        // If rowPitchBytes is 0, we ignore it.
        if (rowPitchBytes != 0)
        {
            // Row pitch must be a multiple of rowPitchAlignment. Ensure our base offset is also aligned
            // to that to avoid misaligned rows when tightly packed.
            alignment = std::max(alignment, optimalBufferCopyRowPitchAlignment);
        }

        // Also keep texel block size alignment for safety (esp. compressed formats).
        alignment = std::max(alignment, texelBlockSize);

        return Allocate(sizeBytes, alignment);
    }
}
