module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.GpuWorld;

import Extrinsic.Core.StrongHandle;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

export namespace Extrinsic::Graphics
{
    using GpuInstanceHandle = Core::StrongHandle<GpuInstanceTag>;
    using GpuGeometryHandle = Core::StrongHandle<GpuGeometryTag>;

    class GpuWorld
    {
    public:
        struct InitDesc
        {
            std::uint32_t MaxInstances = 100'000;
            std::uint32_t MaxGeometryRecords = 65'536;
            std::uint32_t MaxLights = 4'096;
            std::uint64_t VertexBufferBytes = 256ull * 1024ull * 1024ull;
            std::uint64_t IndexBufferBytes = 512ull * 1024ull * 1024ull;
        };

        struct GeometryUploadDesc
        {
            std::span<const std::byte> PackedVertexBytes;
            std::span<const std::uint32_t> SurfaceIndices;
            std::span<const std::uint32_t> LineIndices;
            std::uint32_t VertexCount = 0;
            RHI::GpuBounds LocalBounds{};
            const char* DebugName = nullptr;
        };

        GpuWorld();
        ~GpuWorld();

        GpuWorld(const GpuWorld&) = delete;
        GpuWorld& operator=(const GpuWorld&) = delete;

        bool Initialize(RHI::IDevice& device, RHI::BufferManager& buffers, const InitDesc& desc);
        bool Initialize(RHI::IDevice& device, RHI::BufferManager& buffers);
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] GpuInstanceHandle AllocateInstance(std::uint32_t entityId);
        void FreeInstance(GpuInstanceHandle instance);

        [[nodiscard]] GpuGeometryHandle UploadGeometry(const GeometryUploadDesc& desc);
        void FreeGeometry(GpuGeometryHandle geometry);

        void SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry);
        void SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot);
        void SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags);
        void SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel);
        void SetEntityConfig(GpuInstanceHandle instance, const RHI::GpuEntityConfig& config);
        void SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds);

        void SetMaterialBuffer(RHI::BufferHandle materialBuffer, std::uint32_t materialCapacity);
        void SetLights(std::span<const RHI::GpuLight> lights);

        void SyncFrame();

        [[nodiscard]] RHI::BufferHandle GetSceneTableBuffer() const noexcept;
        [[nodiscard]] std::uint64_t GetSceneTableBDA() const noexcept;

        [[nodiscard]] RHI::BufferHandle GetInstanceStaticBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetInstanceDynamicBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetEntityConfigBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetGeometryRecordBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetBoundsBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetLightBuffer() const noexcept;

        [[nodiscard]] RHI::BufferHandle GetManagedVertexBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle GetManagedIndexBuffer() const noexcept;

        [[nodiscard]] std::uint32_t GetLiveInstanceCount() const noexcept;
        [[nodiscard]] std::uint32_t GetInstanceCapacity() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
