//
// Created by alex on 22.04.26.
//

module;

#include <memory>
#include <span>
#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.TransformSyncSystem;

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.Types;

export namespace Extrinsic::Graphics
{
    struct TransformSyncRecord
    {
        std::uint32_t     StableId{0u};
        GpuInstanceHandle Instance{};
        glm::mat4         Model{1.f};
        std::uint32_t     RenderFlags{RHI::GpuRender_Visible | RHI::GpuRender_Opaque};
        RHI::GpuBounds    Bounds{};
        std::uint32_t     MaterialSlot{0u};
        bool              HasMaterialSlot{false};
    };

    class TransformSyncSystem
    {
    public:
        TransformSyncSystem();
        ~TransformSyncSystem();

        TransformSyncSystem(const TransformSyncSystem&)            = delete;
        TransformSyncSystem& operator=(const TransformSyncSystem&) = delete;

        void Initialize();
        void Shutdown();
        void SyncGpuBuffer(std::span<const TransformSyncRecord> records, GpuWorld& gpuWorld);

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
