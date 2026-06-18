module;

#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.Graphics.VisualizationSyncSystem;

import Extrinsic.RHI.Device;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;

// ============================================================
// VisualizationSyncSystem — visualization-config → material sync.
//
// OWNER: manages one MaterialLease per runtime-extracted renderable record that
//        carries an active VisualizationConfig (the "override lease").
//
// Responsibilities:
//   1. For each extracted record with MaterialInstance + GpuSceneSlot:
//        a. No VisualizationConfig → EffectiveSlot = base material.
//        b. VisualizationConfig present → allocate/reuse an override
//           material lease (kMaterialTypeID_SciVis), patch it with
//           shading-mode constants, then write per-renderable BDA/config
//           into GpuWorld::GpuEntityConfig and set EffectiveSlot
//           to the override slot.
//      Line and point render hints populate the per-domain
//      GpuEntityConfig::Line / ::Point blocks on the same record.
//      Additionally applies TintOverride to the base material when set.
//
//   2. Clean up override leases when VisualizationConfig is removed.
//
//   3. GpuScene visualization config:
//        ScalarField/PerDomain colour sources publish BDA pointers and metadata
//        into GpuWorld::GpuEntityConfig. Promoted surface shaders resolve these
//        GPU-side through common/gpu_scene.glsl; line/point parity is tracked by
//        GRAPHICS-091 and must use the same BDA path rather than CPU colour
//        baking.
//
// Call order within a frame:
//   1. MaterialSystem::SyncGpuBuffer()     — flush dirty base materials
//   2. VisualizationSyncSystem::Sync()     — patch override materials
//   3. MaterialSystem::SyncGpuBuffer()     — flush dirty overrides
//   4. TransformSyncSystem::SyncGpuBuffer()— write EffectiveSlot → GPU
//
// SciVis material type registration:
//   MaterialSystem::Initialize() registers the SciVis type with
//   TypeID = kMaterialTypeID_SciVis (1) alongside the other built-in
//   types. Initialize() here just looks up the registered handle via
//   MaterialSystem::FindType(kMaterialTypeName_SciVis).
//
// Thread-safety:
//   All methods — render thread only.
// ============================================================

export namespace Extrinsic::Graphics
{
    struct VisualizationSyncRecord
    {
        std::uint32_t StableId{0u};
        Components::MaterialInstance* Material{nullptr};
        const Components::GpuSceneSlot* GpuSlot{nullptr};
        const Components::VisualizationConfig* Visualization{nullptr};
        const Components::RenderEdges* Edges{nullptr};
        const Components::RenderPoints* Points{nullptr};
    };

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

        /// Look up the SciVis material type (registered by
        /// MaterialSystem::Initialize() with the well-known
        /// kMaterialTypeID_SciVis = 1) and connect to the device.
        void Initialize(MaterialSystem& matSys, RHI::IDevice& device);

        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        // -----------------------------------------------------------------
        // Per-frame sync
        // -----------------------------------------------------------------

        /// Iterate all runtime-extracted records carrying MaterialInstance + GpuSceneSlot
        /// and resolve their EffectiveSlot:
        ///   - No VisualizationConfig → EffectiveSlot = base material slot.
        ///   - VisualizationConfig present → create/patch override material
        ///     (kMaterialTypeID_SciVis), write EffectiveSlot = override slot.
        ///
        /// colormapSys must be initialised before Sync() is called so that
        /// GetBindlessIndex() returns valid slot indices.
        void Sync(std::span<VisualizationSyncRecord> records,
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
