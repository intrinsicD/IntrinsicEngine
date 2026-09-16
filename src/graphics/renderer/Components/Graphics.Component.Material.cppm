// Owns a renderable material lease and its resolved GPU slot and tint.
module;

#include <cstdint>
#include <optional>
#include <glm/glm.hpp>

export module Extrinsic.Graphics.Component.Material;

import Extrinsic.Graphics.MaterialSystem;

// ============================================================
// MaterialInstance — per-renderable material ownership sidecar.
//
// Owned by runtime extraction sidecars or graphics snapshots for renderables
// that participate in rendering. Owns an RAII lease into MaterialSystem so
// that the GPU material slot remains valid for exactly as long as the
// renderable-sidecar record is alive.
//
// EffectiveSlot lifecycle:
//   Written each frame by VisualizationSyncSystem::Sync():
//     - Existing lease slot, independent of visualization mode
//   TransformSyncSystem reads EffectiveSlot to fill
//   GpuInstanceData::MaterialSlot in the per-instance SSBO.
//
// Usage:
//   auto lease = matSys.CreateInstance(matSys.FindType("StandardPBR"), params);
//   Components::MaterialInstance material{.Lease = std::move(lease)};
// ============================================================

export namespace Extrinsic::Graphics::Components
{
    struct MaterialInstance
    {
        /// RAII ownership into MaterialSystem.  Non-copyable, move-only.
        /// When this component is destroyed, the slot is returned to the
        /// MaterialSystem free-list automatically.
        MaterialSystem::MaterialLease Lease;

        /// Optional per-entity colour tint applied on top of the template.
        /// When set, VisualizationSyncSystem calls MaterialSystem::Patch()
        /// to write it into BaseColorFactor during frame preparation.
        std::optional<glm::vec4> TintOverride;

        /// Resolved GPU material-slot index consumed by TransformSyncSystem
        /// to fill GpuInstanceData::MaterialSlot.  Written each frame by
        /// VisualizationSyncSystem before TransformSyncSystem runs.
        std::uint32_t EffectiveSlot = 0u;
    };
}