module;

#include <cstdint>
#include <memory>

export module Extrinsic.RHI.SamplerManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

// ============================================================
// SamplerManager
// ============================================================
// Owns every GPU sampler object and deduplicates them by
// descriptor content — identical SamplerDescs always return a
// lease to the same underlying GPU object.
//
// Motivation:
//   Samplers are few (typically < 20 per application) but are
//   referenced by many textures.  Creating duplicates wastes
//   GPU descriptor heap slots and driver-side objects.
//   GetOrCreate() makes deduplication transparent to callers.
//
// Deduplication:
//   SamplerDesc is hashed field-by-field (FNV-1a over the raw
//   bytes).  A collision would produce a wrong sampler — this
//   is a structural bug, not a runtime error, and is caught
//   by the assert inside GetOrCreate().
//
// Thread-safety contract:
//   - GetOrCreate() — render thread only (may call IDevice).
//   - Retain() / Release() — atomic, any thread.
//   - Release() at zero — render thread (calls IDevice::DestroySampler).
//
// Lifetime contract: identical to BufferManager — every SamplerLease
// issued by this manager must be destroyed before the manager itself,
// or Release() will use-after-free into the freed Impl. Asserted in
// Debug; documented here for Release builds.
//
// Usage:
//
//   SamplerManager mgr{device};
//
//   auto linear = mgr.GetOrCreate({
//       .MagFilter = FilterMode::Linear,
//       .MinFilter = FilterMode::Linear,
//       .MipFilter = MipmapMode::Linear,
//       .DebugName = "Linear"});
//
//   auto linear2 = mgr.GetOrCreate({...same desc...}); // returns same GPU object
//
//   // Pass the raw handle to TextureManager::Create() for bindless registration.
//   auto tex = texMgr.Create(texDesc, linear.GetHandle());
// ============================================================

export namespace Extrinsic::RHI
{
    class SamplerManager
    {
    public:
        using SamplerLease = Core::Lease<SamplerHandle, SamplerManager>;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        explicit SamplerManager(IDevice& device);
        ~SamplerManager();

        SamplerManager(const SamplerManager&)            = delete;
        SamplerManager& operator=(const SamplerManager&) = delete;

        // -----------------------------------------------------------------
        // Allocation — deduplicated
        // -----------------------------------------------------------------

        /// Return a lease to a sampler matching desc.
        /// Creates the GPU sampler on first call for a given desc;
        /// subsequent calls with the same desc increment the refcount and
        /// return a new lease to the existing object.
        /// Returns a Core::ErrorCode on failure:
        ///   - OutOfDeviceMemory: IDevice::CreateSampler returned an invalid handle.
        [[nodiscard]] Core::Expected<SamplerLease> GetOrCreate(const SamplerDesc& desc);

        // -----------------------------------------------------------------
        // LeasableManager concept requirements
        // -----------------------------------------------------------------
        void Retain(SamplerHandle handle);
        void Release(SamplerHandle handle);

        // -----------------------------------------------------------------
        // Secondary lease — called by SamplerLease::Share()
        // -----------------------------------------------------------------
        [[nodiscard]] SamplerLease AcquireLease(SamplerHandle handle);

        // -----------------------------------------------------------------
        // Data access
        // -----------------------------------------------------------------

        /// Descriptor the sampler was created from, or nullptr for stale handles.
        [[nodiscard]] const SamplerDesc* GetDesc(SamplerHandle handle) const noexcept;

        /// Current number of unique GPU sampler objects alive.
        [[nodiscard]] std::uint32_t GetLiveCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

