// Owns material leases and the GPU material buffer consumed through the scene table.
module;

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

export module Extrinsic.Graphics.MaterialSystem;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Material;

// Material types describe custom shader parameters; ShadingModel independently
// selects lighting. Slot 0 is the permanent purple, unlit invalid-handle material.
// GPU slots are reached through the scene-table BDA. SyncGpuBuffer uploads dirty
// slots before rendering. Creation, mutation and upload are render-thread-only;
// read-only slot/type queries require that their lifetime remains stable.

export namespace Extrinsic::Graphics
{
    struct MaterialSystemDiagnostics
    {
        std::uint32_t DuplicateTypeNameCount = 0;
        std::uint32_t IncompatibleLayoutCount = 0;
        std::uint32_t InvalidCreateTypeCount = 0;
        std::uint32_t CapacityFailureCount = 0;
        std::uint32_t FallbackSlotResolveCount = 0;
        std::uint32_t LastUploadRangeCount = 0;
        std::uint32_t LastUploadedSlotCount = 0;
        std::uint32_t TextureAssetResolveCount = 0;
        std::uint32_t TextureAssetFallbackResolveCount = 0;
        std::uint32_t TextureAssetResolveFailureCount = 0;
        std::uint32_t InvalidTextureAssetBindingCount = 0;
        // Per-frame substitution counters (reset on BeginFrame / SubmitRuntimeSnapshots).
        // Decision 7 path-(b) snapshot-consumption substitution to slot 0.
        std::uint32_t MissingMaterialFallbackCount = 0;
        std::uint32_t InvalidMaterialSlotCount = 0;
        std::uint32_t DefaultDebugSurfaceUses = 0;
        std::uint32_t DirtySlotCount = 0;
        std::uint32_t LiveInstanceCount = 0;
        std::uint32_t RegisteredTypeCount = 0;
        std::uint32_t Capacity = 0;
    };

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

        /// Recreate the material SSBO after a previously non-operational
        /// device becomes operational. Preserves CPU material/type/slot state
        /// and uploads the current mirror into the new GPU buffer.
        [[nodiscard]] bool RebuildGpuResources(RHI::IDevice& device,
                                               RHI::BufferManager& bufferMgr);

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

        /// Resolve AssetId-backed texture bindings through the graphics-owned
        /// GPU asset cache and write the resulting bindless indices into the
        /// material params. Missing/pending/failed texture assets may resolve
        /// to the cache fallback texture; unavailable fallbacks are reported as
        /// deterministic failures. No asset service or runtime state is read.
        Core::Result ResolveTextureAssetBindings(MaterialHandle handle,
                                                 const MaterialTextureAssetBindings& bindings,
                                                 GpuAssetCache& assets,
                                                 const ColormapSystem* colormaps = nullptr);

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

        /// Canonical layout consumed by renderer passes and runtime extraction
        /// sidecars. This is CPU-visible metadata only; backend descriptor
        /// allocation remains owned by graphics/RHI wiring.
        [[nodiscard]] MaterialLayoutContract GetLayoutContract() const noexcept;

        // -----------------------------------------------------------------
        // Diagnostics
        // -----------------------------------------------------------------
        [[nodiscard]] std::uint32_t GetLiveInstanceCount()  const noexcept;
        [[nodiscard]] std::uint32_t GetRegisteredTypeCount() const noexcept;
        [[nodiscard]] std::uint32_t GetCapacity()           const noexcept;
        [[nodiscard]] MaterialSystemDiagnostics GetDiagnostics() const noexcept;

        // -----------------------------------------------------------------
        // Per-frame substitution counters (GRAPHICS-031B Decision 7 path-(b))
        // -----------------------------------------------------------------
        // The renderer's snapshot-copy step substitutes missing/invalid
        // material slots with `kDefaultMaterialSlotIndex` (slot 0) and
        // records the substitution category through these helpers. The
        // counters are reset at the per-frame cadence defined by Decision 8
        // via `ResetPerFrameSubstitutionCounters()`.
        void RecordMissingMaterialFallback() noexcept;
        void RecordInvalidMaterialSlot() noexcept;
        void RecordDefaultDebugSurfaceUse() noexcept;
        void ResetPerFrameSubstitutionCounters() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
