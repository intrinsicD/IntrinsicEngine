module;

#include <cstdint>
#include <span>

export module Extrinsic.RHI.Bindless;

import Extrinsic.RHI.Handles;

// ============================================================
// RHI.Bindless — API-agnostic bindless resource heap interface.
//
// In Vulkan this maps to a large VkDescriptorSet with
// VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT.
// In DX12 this would map to a Shader-Visible Descriptor Heap.
//
// Consumers only ever see BindlessIndex (a uint32_t slot number)
// and use it directly in push constants / SSBOs to access
// textures/samplers in shaders without per-draw binding changes.
//
// Slot 0 is reserved for the engine's default/error texture.
// It is never freed and is automatically bound at init time.
// ============================================================

export namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // Slot index type — what the shader sees (e.g. nonuniformEXT).
    // ----------------------------------------------------------
    using BindlessIndex = std::uint32_t;
    constexpr BindlessIndex kInvalidBindlessIndex = 0; // slot 0 = default texture

    // ----------------------------------------------------------
    // IBindlessHeap — per-device bindless resource heap.
    //
    // Obtained via IDevice::GetBindlessHeap().
    // Lifetime is tied to the IDevice.
    // Thread-safety: EnqueueUpdate is thread-safe (lock-protected);
    // FlushPending must be called on the render/main thread once per frame.
    // ----------------------------------------------------------
    class IBindlessHeap
    {
    public:
        virtual ~IBindlessHeap() = default;

        // ---- Texture slots -------------------------------------------
        // Register a texture + sampler into the bindless heap.
        // Returns the stable slot index the shader uses to sample the texture.
        // Slot 0 is reserved — never returned by Allocate.
        [[nodiscard]] virtual BindlessIndex AllocateTextureSlot(TextureHandle texture,
                                                                SamplerHandle sampler) = 0;

        // Re-bind an existing slot to a new texture/sampler (e.g. streaming update).
        // Thread-safe: queued and applied during FlushPending().
        virtual void UpdateTextureSlot(BindlessIndex slot,
                                       TextureHandle texture,
                                       SamplerHandle sampler) = 0;

        // Free a previously allocated slot.
        // The slot reverts to the default descriptor (slot 0 binding) on the next FlushPending().
        virtual void FreeSlot(BindlessIndex slot) = 0;

        // ---- Frame maintenance ---------------------------------------
        // Apply all queued updates to the GPU descriptor heap.
        // Call once per frame on the main/render thread, before submitting draw work.
        virtual void FlushPending() = 0;

        // ---- Queries -------------------------------------------------
        [[nodiscard]] virtual std::uint32_t GetCapacity()            const = 0;
        [[nodiscard]] virtual std::uint32_t GetAllocatedSlotCount()  const = 0;
    };
}

