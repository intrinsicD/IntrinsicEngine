module;

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

export module Extrinsic.Graphics.MaterialSystem;

import Extrinsic.Core.Lease;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.Material;

// ============================================================
// MaterialSystem
// ============================================================
// Manages all material types and instances for GPU rendering.
//
// GPU layout:
//   One persistent SSBO (set 3, binding 0) holding an array of
//   GpuMaterialSlot (128 bytes each).  GpuInstanceData::MaterialSlot
//   is an index into this array.  The CPU side mirrors the SSBO in
//   a std::vector that is uploaded once per frame when dirty.
//
//   Slot 0 is the engine default material (opaque white PBR).
//   It is pre-populated at Initialize() and never freed.
//
// Material types:
//   Types are registered once (RegisterType).  Each type gets a
//   unique MaterialTypeID stored in GpuMaterialSlot::MaterialTypeID.
//   The shader reads this ID and branches to the correct shading
//   model.  Type 0 = StandardPBR (registered automatically).
//
// Custom materials:
//   Register a MaterialTypeDesc with up to 4 named CustomParam
//   slots.  Fill them via MaterialParams::CustomData[0..3].
//   The shader receives those values in GpuMaterialSlot::CustomData.
//
// Thread-safety:
//   - RegisterType / CreateInstance / SetParams — render thread only.
//   - SyncGpuBuffer — render thread only (once per frame).
//   - GetMaterialSlot / GetTypeDesc — lock-free read after creation.
//
// Usage:
//
//   MaterialSystem matSys;
//   matSys.Initialize(device, bufferMgr);
//
//   // Register a custom water type once at startup:
//   auto waterType = matSys.RegisterType({"Water", {
//       {"WaveAmplitude", "Wave height",    {0.1f,0,0,0}},
//       {"WaveFrequency", "Wave tiling UV", {8.f,8.f,0,0}},
//   }});
//
//   // Create an instance:
//   MaterialParams p;
//   p.BaseColorFactor = {0.0f, 0.3f, 0.8f, 1.0f};
//   p.CustomData[0]   = {0.2f, 0,   0,    0};   // WaveAmplitude
//   auto lease = matSys.CreateInstance(waterType, p);
//
//   // Each frame (before draw submission):
//   matSys.SyncGpuBuffer();
//
//   // Bind: pass matSys.GetBuffer() to the descriptor set builder.
//   // Pass matSys.GetMaterialSlot(lease.GetHandle()) in push constants.
// ============================================================

export namespace Extrinsic::Graphics
{
    class MaterialSystem
    {
    public:
        using MaterialLease = Core::Lease<MaterialHandle, MaterialSystem>;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        MaterialSystem();
        ~MaterialSystem();

        MaterialSystem(const MaterialSystem&)            = delete;
        MaterialSystem& operator=(const MaterialSystem&) = delete;

        /// Call once before any other method.
        /// Allocates the GPU material SSBO via bufferMgr and registers
        /// the default StandardPBR type (TypeID = 0, slot 0).
        void Initialize(RHI::IDevice& device, RHI::BufferManager& bufferMgr);

        /// Release the SSBO lease and all instance slots.
        void Shutdown();

        // -----------------------------------------------------------------
        // Type registration
        // -----------------------------------------------------------------

        /// Register a new material type.  Returns an invalid handle on
        /// duplicate names (names must be unique across the system).
        /// Type 0 ("StandardPBR") is pre-registered — do not re-register it.
        [[nodiscard]] MaterialTypeHandle RegisterType(const MaterialTypeDesc& desc);

        /// Retrieve a type handle by name.  Returns invalid if not found.
        [[nodiscard]] MaterialTypeHandle FindType(std::string_view name) const noexcept;

        /// Descriptor for a registered type, or nullptr if handle is invalid.
        [[nodiscard]] const MaterialTypeDesc* GetTypeDesc(MaterialTypeHandle type) const noexcept;

        // -----------------------------------------------------------------
        // Instance management
        // -----------------------------------------------------------------

        /// Allocate a new material instance of the given type.
        /// Returns an empty lease on failure (system not initialized,
        /// invalid type, or SSBO capacity exceeded).
        [[nodiscard]] MaterialLease CreateInstance(MaterialTypeHandle  type,
                                                   const MaterialParams& params = {});

        /// Update all parameters of an existing instance.  Marks it dirty.
        /// No-op on stale handles.
        void SetParams(MaterialHandle handle, const MaterialParams& params);

        /// Update a subset of params — only the fields explicitly set in
        /// the lambda are written.  Useful for animation / per-frame tweaks.
        ///   matSys.Patch(h, [](MaterialParams& p){ p.MetallicFactor = 1.f; });
        template <typename Fn>
        void Patch(MaterialHandle handle, Fn&& fn)
        {
            MaterialParams p = GetParams(handle);
            fn(p);
            SetParams(handle, p);
        }

        /// Current CPU-side params for an instance (for editor reads).
        [[nodiscard]] MaterialParams GetParams(MaterialHandle handle) const noexcept;

        // -----------------------------------------------------------------
        // LeasableManager concept requirements
        // -----------------------------------------------------------------
        void Retain(MaterialHandle handle);
        void Release(MaterialHandle handle);

        // -----------------------------------------------------------------
        // Secondary lease — called by MaterialLease::Share()
        // -----------------------------------------------------------------
        [[nodiscard]] MaterialLease AcquireLease(MaterialHandle handle);

        // -----------------------------------------------------------------
        // GPU sync — call once per frame on the render thread
        // -----------------------------------------------------------------

        /// Upload all dirty instances to the GPU material SSBO.
        /// No-op when nothing has changed since the last call.
        void SyncGpuBuffer();

        // -----------------------------------------------------------------
        // GPU binding helpers
        // -----------------------------------------------------------------

        /// SSBO buffer handle to bind at set 3, binding 0.
        /// Invalid before Initialize().
        [[nodiscard]] RHI::BufferHandle GetBuffer() const noexcept;

        /// SSBO slot index for GpuInstanceData::MaterialSlot.
        /// Returns 0 (default material) for stale handles.
        [[nodiscard]] std::uint32_t GetMaterialSlot(MaterialHandle handle) const noexcept;

        // -----------------------------------------------------------------
        // Diagnostics
        // -----------------------------------------------------------------
        [[nodiscard]] std::uint32_t GetLiveInstanceCount()  const noexcept;
        [[nodiscard]] std::uint32_t GetRegisteredTypeCount() const noexcept;
        [[nodiscard]] std::uint32_t GetCapacity()           const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

