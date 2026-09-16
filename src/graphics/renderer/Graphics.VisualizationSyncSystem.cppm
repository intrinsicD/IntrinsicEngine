// Publishes extracted visualization settings to GPU entity config while preserving authored materials.
module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>

export module Extrinsic.Graphics.VisualizationSyncSystem;

import Extrinsic.RHI.Device;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;

// Render-thread only. Sync writes scalar/color buffers, colormap/isoline settings
// and line/point hints to GpuEntityConfig. It resolves the existing material slot
// and applies an optional tint without allocating a visualization material.
// Call before the material-buffer upload and transform sync for the frame.

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
        GpuInstanceHandle TargetInstance{};
        std::string ScalarPropertyBufferSourceKey{};
        std::string ColorPropertyBufferSourceKey{};
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

        void Initialize(RHI::IDevice& device);

        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        // -----------------------------------------------------------------
        // Per-frame sync
        // -----------------------------------------------------------------

        /// Colormaps must be initialized before Sync to publish valid bindless indices.
        void Sync(std::span<VisualizationSyncRecord> records,
                  MaterialSystem& matSys,
                  ColormapSystem& colormapSys,
                  GpuWorld& gpuWorld,
                  std::span<const VisualizationPropertyBufferAddress> propertyBufferAddresses = {},
                  std::span<const ScalarAttributePacket> scalarPackets = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
