module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.GpuWorld;

import Extrinsic.Core.StrongHandle;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
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
            std::uint32_t DeferredFreeFrames = 2;
            std::uint64_t VertexBufferBytes = 256ull * 1024ull * 1024ull;
            std::uint64_t IndexBufferBytes = 512ull * 1024ull * 1024ull;
        };

        struct PoolDiagnostics
        {
            std::uint32_t Capacity = 0;
            std::uint32_t LiveCount = 0;
            std::uint32_t ReusableCount = 0;
            std::uint32_t PendingFreeCount = 0;
            std::uint32_t OverflowCount = 0;
            std::uint32_t InvalidHandleCount = 0;
            std::uint32_t StaleHandleCount = 0;
        };

        struct Diagnostics
        {
            PoolDiagnostics Instances{};
            PoolDiagnostics Geometry{};
            std::uint64_t VertexBytesUsed = 0;
            std::uint64_t VertexBytesCapacity = 0;
            std::uint64_t IndexBytesUsed = 0;
            std::uint64_t IndexBytesCapacity = 0;
            std::uint32_t VertexOverflowCount = 0;
            std::uint32_t IndexOverflowCount = 0;
            std::uint32_t LightOverflowCount = 0;
            bool NullDevice = false;
        };

        struct ManagedBufferFragmentation
        {
            std::uint64_t CapacityBytes = 0;
            std::uint64_t UsedHighWaterBytes = 0;
            std::uint64_t LiveBytes = 0;
            std::uint64_t FragmentedBytes = 0;
            float FragmentationRatio = 0.0f;
        };

        struct ManagedBufferDiagnostics
        {
            ManagedBufferFragmentation Vertex{};
            ManagedBufferFragmentation Index{};
            std::uint64_t CompactionBytesMoved = 0;
            std::uint32_t CompactionCount = 0;
            std::uint32_t StaleRelocationCount = 0;
        };

        struct CompactionPlanDesc
        {
            bool Enabled = true;
            bool AllowWhilePendingFrees = false;
            float MinFragmentationRatio = 0.25f;
            std::uint64_t MinRecoverableBytes = 1;
        };

        struct ClusterLightTableDesc
        {
            RHI::BufferHandle HeaderBuffer{};
            RHI::BufferHandle IndexBuffer{};
            std::uint32_t TilePx = 0;
            std::uint32_t TilesX = 0;
            std::uint32_t TilesY = 0;
            std::uint32_t SlicesZ = 0;
            std::uint32_t CellCount = 0;
            std::uint32_t MaxLightsPerCell = 0;
            float NearZ = 0.f;
            float FarZ = 0.f;
            float ProjectionScaleX = 0.f;
            float ProjectionScaleY = 0.f;
        };

        struct GeometryRelocation
        {
            GpuGeometryHandle Geometry{};
            std::uint64_t OldVertexByteOffset = 0;
            std::uint64_t NewVertexByteOffset = 0;
            std::uint64_t VertexByteCount = 0;
            std::uint64_t OldIndexByteOffset = 0;
            std::uint64_t NewIndexByteOffset = 0;
            std::uint64_t IndexByteCount = 0;
            std::uint32_t OldVertexOffset = 0;
            std::uint32_t NewVertexOffset = 0;
            std::uint32_t OldSurfaceFirstIndex = 0;
            std::uint32_t NewSurfaceFirstIndex = 0;
            std::uint32_t OldLineFirstIndex = 0;
            std::uint32_t NewLineFirstIndex = 0;
        };

        struct CompactionPlan
        {
            bool Enabled = false;
            bool ShouldCompact = false;
            bool BlockedByPendingFrees = false;
            ManagedBufferFragmentation Vertex{};
            ManagedBufferFragmentation Index{};
            std::uint64_t RecoverableBytes = 0;
            std::uint64_t BytesToMove = 0;
            std::vector<GeometryRelocation> Relocations;
        };

        struct CompactionResult
        {
            bool Applied = false;
            bool Skipped = false;
            bool RejectedStaleRelocations = false;
            std::uint32_t RelocationCount = 0;
            std::uint32_t StaleRelocationCount = 0;
            std::uint64_t BytesMoved = 0;
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
        [[nodiscard]] bool RebuildGpuResources(RHI::IDevice& device, RHI::BufferManager& buffers);
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] GpuInstanceHandle AllocateInstance(std::uint32_t entityId);
        void FreeInstance(GpuInstanceHandle instance);

        [[nodiscard]] GpuGeometryHandle UploadGeometry(const GeometryUploadDesc& desc);
        void FreeGeometry(GpuGeometryHandle geometry);

        void SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry);
        [[nodiscard]] GpuGeometryHandle GetInstanceGeometry(GpuInstanceHandle instance) const noexcept;
        void SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot);
        void SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags);
        void SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel);
        void SetEntityConfig(GpuInstanceHandle instance, const RHI::GpuEntityConfig& config);
        [[nodiscard]] RHI::GpuEntityConfig GetEntityConfigForTest(
            GpuInstanceHandle instance) const noexcept;
        void SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds);

        void SetMaterialBuffer(RHI::BufferHandle materialBuffer, std::uint32_t materialCapacity);
        void SetCamera(const RHI::CameraUBO& camera);
        void SetLights(std::span<const RHI::GpuLight> lights);
        void SetClusterLightTable(const ClusterLightTableDesc& desc);
        void ClearClusterLightTable();

        void SyncFrame();
        void SubmitPendingUploadBarriers(RHI::ICommandContext& cmd);

        [[nodiscard]] CompactionPlan PlanManagedBufferCompaction() const;
        [[nodiscard]] CompactionPlan PlanManagedBufferCompaction(const CompactionPlanDesc& desc) const;
        [[nodiscard]] CompactionResult ApplyManagedBufferCompaction(const CompactionPlan& plan);

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
        [[nodiscard]] std::uint32_t GetLiveGeometryCount() const noexcept;
        [[nodiscard]] std::uint32_t GetGeometryCapacity() const noexcept;
        [[nodiscard]] std::uint32_t GetLightCount() const noexcept;
        [[nodiscard]] std::uint32_t GetLightCapacity() const noexcept;
        [[nodiscard]] Diagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] ManagedBufferDiagnostics GetManagedBufferDiagnostics() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
