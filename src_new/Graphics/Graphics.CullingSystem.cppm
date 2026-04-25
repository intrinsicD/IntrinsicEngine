module;

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

export module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Graphics
{
    struct CullingTag;
    using CullingHandle = Core::StrongHandle<CullingTag>;

    export struct GpuDrawBucket
    {
        RHI::BufferHandle IndexedArgsBuffer{};
        RHI::BufferHandle NonIndexedArgsBuffer{};
        RHI::BufferHandle CountBuffer{};
        std::uint32_t Capacity = 0;
        bool Indexed = true;
    };

    class CullingSystem
    {
    public:
        CullingSystem();
        ~CullingSystem();

        CullingSystem(const CullingSystem&)            = delete;
        CullingSystem& operator=(const CullingSystem&) = delete;

        void Initialize(RHI::IDevice&         device,
                        RHI::BufferManager&   bufferMgr,
                        RHI::PipelineManager& pipelineMgr,
                        std::string_view      cullShaderPath);

        void Shutdown();

        [[nodiscard]] CullingHandle Register(const RHI::BoundingSphere& sphere,
                                             const RHI::GpuDrawCommand&  drawTemplate);
        void Unregister(CullingHandle handle);
        void UpdateBounds(CullingHandle handle, const RHI::BoundingSphere& sphere);
        void SetDrawTemplate(CullingHandle handle, const RHI::GpuDrawCommand& cmd);

        void SyncGpuBuffer();
        void ResetCounters(RHI::ICommandContext& cmd);
        void DispatchCull(RHI::ICommandContext& cmd,
                          const RHI::CameraUBO& camera,
                          const GpuWorld&       gpuWorld);

        [[nodiscard]] const GpuDrawBucket& GetBucket(RHI::GpuDrawBucketKind kind) const;

        [[nodiscard]] RHI::BufferHandle GetDrawCommandBuffer()     const noexcept;
        [[nodiscard]] RHI::BufferHandle GetVisibilityCountBuffer() const noexcept;

        [[nodiscard]] std::uint32_t GetRegisteredCount() const noexcept;
        [[nodiscard]] std::uint32_t GetCapacity()        const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
