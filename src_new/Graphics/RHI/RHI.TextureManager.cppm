module;

#include <cstdint>
#include <memory>

export module Extrinsic.RHI.TextureManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;

// ============================================================
// TextureManager
// ============================================================
// Owns the lifetime of every GPU texture allocated through it.
// Mirrors BufferManager in structure; adds bindless registration
// so shaders can sample any live texture by slot index with no
// per-draw descriptor binding.
//
// Bindless contract:
//   - Create() allocates a slot in IBindlessHeap (if a sampler
//     is provided) and stores the BindlessIndex in the slot.
//   - Release() at zero frees the bindless slot, then destroys
//     the texture.  The sampler is NOT destroyed — the manager
//     treats samplers as externally owned (they are typically
//     long-lived and shared across many textures).
//   - GetBindlessIndex() returns kInvalidBindlessIndex when the
//     texture was created without a sampler or the handle is stale.
//
// Thread-safety contract: identical to BufferManager.
//   - Create() — render thread only (calls IDevice).
//   - Retain() / Release() — atomic, any thread.
//   - Release() at zero — render thread (calls IDevice::DestroyTexture).
//   - GetDesc() / GetBindlessIndex() — lock-free read, any thread.
//
// Lifetime contract: identical to BufferManager — every TextureLease
// issued by this manager must be destroyed before the manager itself,
// or Release() will use-after-free into the freed Impl. Asserted in
// Debug; documented here for Release builds.
//
// Usage:
//
//   TextureManager mgr{device, device.GetBindlessHeap()};
//
//   auto lease = mgr.Create(
//       {.Width=1024, .Height=1024, .Fmt=Format::RGBA8_SRGB,
//        .Usage=TextureUsage::Sampled|TextureUsage::TransferDst,
//        .DebugName="Albedo"},
//       linearSampler);                       // registers into bindless heap
//
//   BindlessIndex idx = mgr.GetBindlessIndex(lease.GetHandle());
//   // push idx as a push-constant; shader does texture2D(Textures[idx], uv)
//
//   auto lease2 = lease.Share();              // refcount → 2, same bindless slot
//   // lease and lease2 go out of scope → bindless slot freed, texture destroyed.
// ============================================================

export namespace Extrinsic::RHI
{
    class TextureManager
    {
    public:
        using TextureLease = Core::Lease<TextureHandle, TextureManager>;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------

        /// Both references must outlive the manager.
        explicit TextureManager(IDevice& device, IBindlessHeap& bindlessHeap);
        ~TextureManager();

        TextureManager(const TextureManager&)            = delete;
        TextureManager& operator=(const TextureManager&) = delete;

        // -----------------------------------------------------------------
        // Allocation
        // -----------------------------------------------------------------

        /// Allocate a GPU texture and optionally register it in the bindless heap.
        /// Pass a valid SamplerHandle to get a non-zero BindlessIndex for shader access.
        /// Pass an invalid SamplerHandle ({}) to skip bindless registration.
        /// Returns a Core::ErrorCode on failure:
        ///   - OutOfDeviceMemory: IDevice::CreateTexture returned an invalid handle.
        [[nodiscard]] Core::Expected<TextureLease> Create(const TextureDesc& desc,
                                                          SamplerHandle       sampler = {});

        // -----------------------------------------------------------------
        // LeasableManager concept requirements
        // -----------------------------------------------------------------
        void Retain(TextureHandle handle);
        void Release(TextureHandle handle);

        // -----------------------------------------------------------------
        // Secondary lease — called by TextureLease::Share()
        // -----------------------------------------------------------------
        [[nodiscard]] TextureLease AcquireLease(TextureHandle handle);

        // -----------------------------------------------------------------
        // Data access
        // -----------------------------------------------------------------

        /// Descriptor the texture was created with, or nullptr for stale handles.
        [[nodiscard]] const TextureDesc* GetDesc(TextureHandle handle) const noexcept;

        /// Bindless slot index for shader access.
        /// Returns kInvalidBindlessIndex if the texture has no sampler or handle is stale.
        [[nodiscard]] BindlessIndex GetBindlessIndex(TextureHandle handle) const noexcept;

        /// Update the bindless slot to point at a new device texture + sampler
        /// (e.g. streaming mip replacement).  The pool handle and its refcount
        /// are unchanged — only the GPU descriptor is patched.
        /// No-op when the handle is stale or has no bindless slot.
        void Reupload(TextureHandle handle, TextureHandle newDeviceHandle,
                      SamplerHandle newSampler);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

