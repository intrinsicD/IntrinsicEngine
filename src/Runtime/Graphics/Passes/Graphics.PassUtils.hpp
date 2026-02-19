#pragma once

// =============================================================================
// PassUtils — shared implementation helpers for render passes.
//
// This header is intended for inclusion in render pass .cpp global module
// fragments only — it is not part of any exported module interface.
// =============================================================================

#include <cstdint>
#include <memory>
#include <string_view>

// EnsurePerFrameBuffer<T>
// -----------------------
// Grows a per-frame host-visible SSBO array to hold at least `required`
// elements of type T.  Returns true on success, false on allocation failure.
//
// Parameters:
//   device        — Vulkan device (borrowed).
//   buffers       — array of FRAMES unique_ptr<VulkanBuffer>, indexed by frame.
//   capacity      — in/out: current capacity in elements (updated on growth).
//   required      — minimum element count needed this frame.
//   minCapacity   — initial allocation floor (avoids tiny first allocations).
//   passName      — used in error log messages.
//
// Growth strategy: next power of 2 >= required, floored at minCapacity.
template<typename T, uint32_t FRAMES>
bool EnsurePerFrameBuffer(RHI::VulkanDevice& device,
                          std::unique_ptr<RHI::VulkanBuffer> (&buffers)[FRAMES],
                          uint32_t& capacity,
                          uint32_t  required,
                          uint32_t  minCapacity,
                          std::string_view passName)
{
    if (required <= capacity && buffers[0] != nullptr)
        return true;

    uint32_t newCapacity = minCapacity;
    while (newCapacity < required)
        newCapacity *= 2;

    const size_t byteSize = static_cast<size_t>(newCapacity) * sizeof(T);

    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        buffers[i] = std::make_unique<RHI::VulkanBuffer>(
            device,
            byteSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        if (!buffers[i]->GetMappedData())
        {
            Core::Log::Error("{}: Failed to allocate SSBO ({} bytes)", passName, byteSize);
            return false;
        }
    }

    capacity = newCapacity;
    return true;
}
