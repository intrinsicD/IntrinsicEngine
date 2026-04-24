module;

#include <cstdint>
#include <memory>
#include <entt/entity/registry.hpp>

export module Extrinsic.Graphics.VisualizationSyncSystem;

import Extrinsic.RHI.Device;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.GpuWorld;

// ============================================================
// VisualizationSyncSystem — visualization-config → material sync.
//
// OWNER: manages one MaterialLease per entity that carries an
//        active VisualizationConfig (the "override lease").
//
// Responsibilities:
//   1. For each entity with MaterialInstance + GpuSceneSlot:
//        a. No VisualizationConfig → EffectiveSlot = base material.
//        b. VisualizationConfig present → allocate/reuse an override
//           material lease (kMaterialTypeID_SciVis), patch it with
//           shading-mode constants, then write per-entity BDA/config
//           into GpuWorld::GpuEntityConfig and set EffectiveSlot
//           to the override slot.
//      Additionally applies TintOverride to the base material when set.
//
//   2. Clean up override leases when VisualizationConfig is removed.
//
//   3. CPU-baking path for Line and Point passes:
//        ScalarField/PerDomain colours are baked from CPU data into a
//        "vis_colors_baked" buffer in GpuSceneSlot so that line/point
//        shaders can read them through the existing colour-buffer BDA
//        without any push-constant budget changes.
//
// Call order within a frame:
//   1. MaterialSystem::SyncGpuBuffer()     — flush dirty base materials
//   2. VisualizationSyncSystem::Sync()     — patch override materials
//   3. MaterialSystem::SyncGpuBuffer()     — flush dirty overrides
//   4. TransformSyncSystem::SyncGpuBuffer()— write EffectiveSlot → GPU
//
// SciVis material type registration:
//   Initialize() calls MaterialSystem::RegisterType("SciVis") to
//   ensure the type gets TypeID = kMaterialTypeID_SciVis (1).
//   Must be called before any other custom type is registered.
//
// Thread-safety:
//   All methods — render thread only.
// ============================================================

export namespace Extrinsic::Graphics
{
    class VisualizationSyncSystem
    {
    public:
        VisualizationSyncSystem();
        ~VisualizationSyncSystem();

        VisualizationSyncSystem(const VisualizationSyncSystem&)            = delete;
        VisualizationSyncSystem& operator=(const VisualizationSyncSystem&) = delete;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------

        /// Register the SciVis material type and connect to the device.
        /// Must be called BEFORE any other custom material type is registered
        /// to guarantee kMaterialTypeID_SciVis = 1.
        void Initialize(MaterialSystem& matSys, RHI::IDevice& device);

        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        // -----------------------------------------------------------------
        // Per-frame sync
        // -----------------------------------------------------------------

        /// Iterate all entities carrying MaterialInstance + GpuSceneSlot
        /// and resolve their EffectiveSlot:
        ///   - No VisualizationConfig → EffectiveSlot = base material slot.
        ///   - VisualizationConfig present → create/patch override material
        ///     (kMaterialTypeID_SciVis), write EffectiveSlot = override slot.
        ///
        /// colormapSys must be initialised before Sync() is called so that
        /// GetBindlessIndex() returns valid slot indices.
        void Sync(entt::registry& registry,
                  MaterialSystem& matSys,
                  ColormapSystem& colormapSys,
                  GpuWorld& gpuWorld);

        // -----------------------------------------------------------------
        // Diagnostics
        // -----------------------------------------------------------------
        [[nodiscard]] std::uint32_t GetOverrideLeaseCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
