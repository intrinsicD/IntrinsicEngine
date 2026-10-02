// Managed GPU scene buffers and residency identities for renderer and runtime consumers.
module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.GpuWorld;

import Extrinsic.Graphics.SceneHandles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    // Same nonzero, least-significant-byte-first FNV-1a identity as retained
    // surface indices, including an empty stream. Zero denotes an absent channel.
    [[nodiscard]] std::uint64_t FingerprintSurfaceIndices(
        std::span<const std::uint32_t> indices) noexcept;

    struct GpuGeometryResidencyView;

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
            std::span<const std::byte> PositionBytes;
            std::span<const std::byte> TexcoordBytes;
            std::span<const std::byte> NormalBytes;
            std::span<const std::uint32_t> SurfaceIndices;
            std::span<const std::uint32_t> LineIndices;
            std::uint32_t VertexCount = 0;
            RHI::GpuBounds LocalBounds{};
            const char* DebugName = nullptr;
            std::span<const std::uint32_t> PackedVertexColors;
        };

        struct GeometryChannelUpdateMask
        {
            bool Position = false;
            bool Texcoord = false;
            bool Normal = false;
            bool Color = false;

            [[nodiscard]] bool Any() const noexcept
            {
                return Position || Texcoord || Normal || Color;
            }
        };

        enum class GeometryChannelUpdateStatus : std::uint8_t
        {
            Updated,
            NoChannels,
            FullUploadRequired,
            InvalidHandle,
            InvalidInput,
        };

        struct GeometryChannelUpdateResult
        {
            GeometryChannelUpdateStatus Status = GeometryChannelUpdateStatus::NoChannels;
            GeometryChannelUpdateMask UploadedChannels{};
            bool GeometryRecordUpdated = false;

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == GeometryChannelUpdateStatus::Updated ||
                       Status == GeometryChannelUpdateStatus::NoChannels;
            }
        };

        // ADR 0030 decision 5 (GRAPHICS-156): a GPU method's position ring front that the
        // renderer shows instead of the block's CPU bytes. The front holds float3 rows at
        // stride 12 (`SourceRowCount` of them from `SourceOffsetBytes`). A 1:1 block (point
        // clouds, graphs, canonical mesh views) is copied; a seam-split mesh block gathers
        // through `GatherMap` (one source row per GPU vertex, uploaded once per
        // `GatherStamp`). The copy is recorded at the head of the culling pass, only when
        // `Stamp` or `GatherStamp` changed or the block was rewritten, and marks the block's
        // CPU shadow stale so compaction and rebuild replay never write old bytes over it.
        struct GeometryPositionPreviewDesc
        {
            RHI::BufferHandle Source{};
            std::uint64_t SourceOffsetBytes = 0;
            std::uint32_t SourceRowCount = 0;
            std::uint64_t Stamp = 0;
            std::span<const std::uint32_t> GatherMap{};
            std::uint64_t GatherStamp = 0;
        };

        enum class GeometryPositionPreviewStatus : std::uint8_t
        {
            Accepted,
            InvalidHandle,
            InvalidInput, // row count or gather map does not fit the block
        };

        // ADR 0030 decision 6 (RUNTIME-293): the accepted positions of a 1:1 block. The CPU
        // published `PositionBytes` (float3 rows at stride 12, one per GPU vertex: the bytes of
        // the ring front that became the canonical slot); the shadow of the position range is
        // patched to them, the other channels are untouched, and the fingerprint and content
        // revision are refreshed without an upload. `Source` names the front so a block that
        // does not hold it yet (the preview was never copied, or a newer front was published
        // after the last copy) gets one copy front -> block at the next culling head instead of
        // a CPU upload; when a block already holds the front, nothing is recorded.
        struct GeometryPositionCommitDesc
        {
            std::span<const std::byte> PositionBytes{};
            RHI::BufferHandle Source{};
            std::uint64_t SourceOffsetBytes = 0;
            std::uint32_t SourceRowCount = 0;
            std::uint64_t Stamp = 0;
            // Keeps `Source` from being rewritten or freed (a residency ring lease). A pending
            // copy holds it until the copy's frame is complete; GpuWorld only stores it.
            std::shared_ptr<const void> SourceLease{};
        };

        enum class GeometryPositionCommitStatus : std::uint8_t
        {
            Committed,     // the block holds the bytes; the preview ended
            CopyPending,   // one copy of `Source` into the block at the next culling head
            InvalidHandle,
            InvalidInput,  // bytes do not fit the block, the block is seam-split, or no source to copy
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
        [[nodiscard]] GeometryChannelUpdateResult UpdateGeometryChannels(
            GpuGeometryHandle geometry,
            const GeometryUploadDesc& desc,
            GeometryChannelUpdateMask channels);
        void FreeGeometry(GpuGeometryHandle geometry);

        // Shows `desc.Source` in the block until cleared; each call refreshes the front. The
        // next position upload (`UpdateGeometryChannels` with Position, or a full upload)
        // restores the shadow's authority; Clear alone leaves the shadow stale until then.
        [[nodiscard]] GeometryPositionPreviewStatus SetGeometryPositionPreview(
            GpuGeometryHandle geometry,
            const GeometryPositionPreviewDesc& desc);
        void ClearGeometryPositionPreview(GpuGeometryHandle geometry);
        // Accept of GPU-authored positions (see `GeometryPositionCommitDesc`). A later position
        // upload supersedes a pending copy.
        [[nodiscard]] GeometryPositionCommitStatus CommitGeometryPositions(
            GpuGeometryHandle geometry,
            const GeometryPositionCommitDesc& desc);

        void SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry);
        [[nodiscard]] GpuGeometryHandle GetInstanceGeometry(GpuInstanceHandle instance) const noexcept;
        void SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot);
        void SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags);
        void SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel);
        void SetEntityConfig(GpuInstanceHandle instance, const RHI::GpuEntityConfig& config);
        [[nodiscard]] RHI::GpuEntityConfig GetEntityConfigForTest(
            GpuInstanceHandle instance) const noexcept;
        void SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds);
        [[nodiscard]] RHI::GpuBounds GetBoundsForTest(GpuInstanceHandle instance) const noexcept;

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
        [[nodiscard]] bool TryGetGeometryRecord(
            GpuGeometryHandle geometry,
            RHI::GpuGeometryRecord& outRecord) const noexcept;
        [[nodiscard]] bool TryGetGeometryResidencyView(
            GpuGeometryHandle geometry,
            GpuGeometryResidencyView& outView) const noexcept;

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

    // CPU-visible facts for the exact bytes retained by GpuWorld. This keeps
    // content/lane eligibility out of the shader-facing GpuGeometryRecord
    // while allowing runtime providers to validate the live allocation before
    // recording commands that read its managed buffers.
    struct GpuGeometryResidencyView
    {
        RHI::GpuGeometryRecord Record{};
        RHI::BufferHandle IndexBuffer{};

        std::uint64_t ContentRevision = 0u;
        // True while the device block holds copied preview positions that the CPU shadow
        // (and the position fingerprint below) does not describe (ADR 0030 decision 5).
        bool PositionShadowStale = false;
        // FNV-1a-64 over uint32 words in least-significant-byte-first order;
        // float streams canonicalize -0 to +0. Only absent optional channels
        // use fingerprint zero.
        std::uint64_t PositionFingerprint = 0u;
        std::uint64_t SurfaceIndexFingerprint = 0u;
        std::uint64_t TexcoordFingerprint = 0u;
        std::uint64_t NormalFingerprint = 0u;

        std::uint64_t PositionByteCount = 0u;
        std::uint64_t SurfaceIndexByteCount = 0u;
        std::uint64_t TexcoordByteCount = 0u;
        std::uint64_t NormalByteCount = 0u;
        std::uint32_t VertexCount = 0u;
        std::uint32_t SurfaceIndexCount = 0u;

        RHI::Format PositionFormat = RHI::Format::Undefined;
        RHI::Format TexcoordFormat = RHI::Format::Undefined;
        RHI::Format NormalFormat = RHI::Format::Undefined;
        RHI::Format SurfaceIndexFormat = RHI::Format::Undefined;
        std::uint32_t PositionElementBytes = 0u;
        std::uint32_t PositionStrideBytes = 0u;
        std::uint32_t TexcoordElementBytes = 0u;
        std::uint32_t TexcoordStrideBytes = 0u;
        std::uint32_t NormalElementBytes = 0u;
        std::uint32_t NormalStrideBytes = 0u;
        std::uint32_t SurfaceIndexElementBytes = 0u;
        std::uint32_t SurfaceIndexStrideBytes = 0u;
    };
}
