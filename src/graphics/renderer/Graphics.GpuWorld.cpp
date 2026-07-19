module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.GpuWorld;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kInvalidSlot = 0xFFFF'FFFFu;
        constexpr std::uint32_t kManagedVertexBlockAlignment = 16u;
        constexpr std::uint32_t kPositionElementBytes = sizeof(float) * 3u;
        constexpr std::uint32_t kTexcoordElementBytes = sizeof(float) * 2u;
        constexpr std::uint32_t kNormalElementBytes = sizeof(float) * 3u;
        constexpr std::uint32_t kColorElementBytes = sizeof(std::uint32_t);
        constexpr std::uint32_t kPhysicalStorageScalarAddressAlignment =
            sizeof(float);
        constexpr std::uint32_t kPhysicalStorageTexcoordAddressAlignment =
            sizeof(float) * 2u;
        constexpr std::uint64_t kFnv1aOffset64 = 14695981039346656037ull;
        constexpr std::uint64_t kFnv1aPrime64 = 1099511628211ull;

        void FingerprintUint32(
            std::uint64_t& fingerprint,
            const std::uint32_t value) noexcept
        {
            for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
            {
                fingerprint ^= static_cast<std::uint64_t>(
                    static_cast<std::uint8_t>(value >> shift));
                fingerprint *= kFnv1aPrime64;
            }
        }

        [[nodiscard]] std::uint64_t FingerprintFloat32Bytes(
            const std::span<const std::byte> bytes) noexcept
        {
            assert((bytes.size_bytes() % sizeof(std::uint32_t)) == 0u);
            std::uint64_t fingerprint = kFnv1aOffset64;
            for (std::size_t offset = 0u;
                 offset < bytes.size_bytes();
                 offset += sizeof(std::uint32_t))
            {
                std::uint32_t bits = 0u;
                std::memcpy(
                    &bits,
                    bytes.data() + static_cast<std::ptrdiff_t>(offset),
                    sizeof(bits));
                if (bits == 0x8000'0000u)
                {
                    bits = 0u;
                }
                FingerprintUint32(fingerprint, bits);
            }
            return fingerprint == 0u ? 1u : fingerprint;
        }

        [[nodiscard]] std::uint64_t FingerprintUint32Stream(
            const std::span<const std::uint32_t> values) noexcept
        {
            std::uint64_t fingerprint = kFnv1aOffset64;
            for (const std::uint32_t value : values)
            {
                FingerprintUint32(fingerprint, value);
            }
            return fingerprint == 0u ? 1u : fingerprint;
        }

        [[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint32_t alignment) noexcept
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const std::uint64_t remainder = value % static_cast<std::uint64_t>(alignment);
            return remainder == 0u ? value : value + (static_cast<std::uint64_t>(alignment) - remainder);
        }

        struct SlotMeta
        {
            std::uint32_t Generation = 1;
            bool Live = false;
        };

        struct PendingFreeSlot
        {
            std::uint32_t Index = kInvalidSlot;
            std::uint64_t ReuseFrame = 0;
        };

        struct ManagedGeometryAllocation
        {
            bool Live = false;
            std::uint32_t Generation = 0;
            std::uint64_t VertexByteOffset = 0;
            std::uint64_t VertexByteCount = 0;
            std::uint64_t PositionByteOffset = 0;
            std::uint64_t PositionByteCount = 0;
            std::uint64_t TexcoordByteOffset = 0;
            std::uint64_t TexcoordByteCount = 0;
            std::uint64_t NormalByteOffset = 0;
            std::uint64_t NormalByteCount = 0;
            std::uint64_t ColorByteOffset = 0;
            std::uint64_t ColorByteCount = 0;
            std::uint64_t IndexByteOffset = 0;
            std::uint64_t SurfaceIndexByteCount = 0;
            std::uint64_t LineIndexByteCount = 0;
            std::uint32_t VertexCount = 0;
            std::uint32_t VertexElementOffset = 0;
            std::uint64_t ContentRevision = 0;
            std::uint64_t PositionFingerprint = 0;
            std::uint64_t SurfaceIndexFingerprint = 0;
            std::uint64_t TexcoordFingerprint = 0;
            std::uint64_t NormalFingerprint = 0;
            GpuWorld::GeometryStorageLane StorageLane =
                GpuWorld::GeometryStorageLane::UniformSoA;
            std::vector<std::byte> VertexBytes;
            std::vector<std::uint32_t> SurfaceIndices;
            std::vector<std::uint32_t> LineIndices;

            [[nodiscard]] std::uint64_t IndexByteCount() const noexcept
            {
                return SurfaceIndexByteCount + LineIndexByteCount;
            }
        };

        [[nodiscard]] std::span<const std::byte> StoredChannelBytes(
            const ManagedGeometryAllocation& allocation,
            const std::uint64_t byteOffset,
            const std::uint64_t byteCount) noexcept
        {
            if (byteCount == 0u)
            {
                return {};
            }

            assert(byteOffset + byteCount <= allocation.VertexBytes.size());
            return std::span<const std::byte>{
                allocation.VertexBytes.data() +
                    static_cast<std::ptrdiff_t>(byteOffset),
                static_cast<std::size_t>(byteCount),
            };
        }

        void RefreshResidencyFingerprints(
            ManagedGeometryAllocation& allocation) noexcept
        {
            const std::span<const std::byte> positionBytes =
                StoredChannelBytes(
                    allocation,
                    allocation.PositionByteOffset,
                    allocation.PositionByteCount);
            const std::span<const std::byte> texcoordBytes =
                StoredChannelBytes(
                    allocation,
                    allocation.TexcoordByteOffset,
                    allocation.TexcoordByteCount);
            const std::span<const std::byte> normalBytes =
                StoredChannelBytes(
                    allocation,
                    allocation.NormalByteOffset,
                    allocation.NormalByteCount);
            allocation.PositionFingerprint =
                FingerprintFloat32Bytes(positionBytes);
            allocation.SurfaceIndexFingerprint =
                FingerprintUint32Stream(
                    std::span<const std::uint32_t>{
                        allocation.SurfaceIndices});
            allocation.TexcoordFingerprint = texcoordBytes.empty()
                ? 0u
                : FingerprintFloat32Bytes(texcoordBytes);
            allocation.NormalFingerprint = normalBytes.empty()
                ? 0u
                : FingerprintFloat32Bytes(normalBytes);
        }

        void RefreshUpdatedResidencyFingerprints(
            ManagedGeometryAllocation& allocation,
            const GpuWorld::GeometryChannelUpdateMask channels) noexcept
        {
            if (channels.Position)
            {
                allocation.PositionFingerprint =
                    FingerprintFloat32Bytes(
                        StoredChannelBytes(
                            allocation,
                            allocation.PositionByteOffset,
                            allocation.PositionByteCount));
            }
            if (channels.Texcoord)
            {
                const std::span<const std::byte> bytes =
                    StoredChannelBytes(
                        allocation,
                        allocation.TexcoordByteOffset,
                        allocation.TexcoordByteCount);
                allocation.TexcoordFingerprint = bytes.empty()
                    ? 0u
                    : FingerprintFloat32Bytes(bytes);
            }
            if (channels.Normal)
            {
                const std::span<const std::byte> bytes =
                    StoredChannelBytes(
                        allocation,
                        allocation.NormalByteOffset,
                        allocation.NormalByteCount);
                allocation.NormalFingerprint = bytes.empty()
                    ? 0u
                    : FingerprintFloat32Bytes(bytes);
            }
        }

        template <typename Tag>
        struct SlotAllocator
        {
            using Handle = Core::StrongHandle<Tag>;

            std::vector<SlotMeta> Meta;
            std::vector<std::uint32_t> FreeList;
            std::vector<PendingFreeSlot> PendingFree;
            std::uint32_t NextFresh = 0;
            std::uint32_t LiveCount = 0;
            std::uint32_t OverflowCount = 0;
            std::uint32_t InvalidHandleCount = 0;
            std::uint32_t StaleHandleCount = 0;

            void Reset(std::uint32_t capacity)
            {
                Meta.assign(capacity, {});
                FreeList.clear();
                PendingFree.clear();
                NextFresh = 0;
                LiveCount = 0;
                OverflowCount = 0;
                InvalidHandleCount = 0;
                StaleHandleCount = 0;
            }

            void RetirePending(std::uint64_t frame)
            {
                for (std::size_t i = 0; i < PendingFree.size();)
                {
                    if (PendingFree[i].ReuseFrame > frame)
                    {
                        ++i;
                        continue;
                    }
                    FreeList.push_back(PendingFree[i].Index);
                    PendingFree.erase(PendingFree.begin() + static_cast<std::ptrdiff_t>(i));
                }
            }

            [[nodiscard]] Handle Allocate()
            {
                std::uint32_t idx = kInvalidSlot;
                if (!FreeList.empty())
                {
                    idx = FreeList.back();
                    FreeList.pop_back();
                }
                else if (NextFresh < Meta.size())
                {
                    idx = NextFresh++;
                }

                if (idx == kInvalidSlot)
                {
                    ++OverflowCount;
                    return {};
                }

                auto& meta = Meta[idx];
                meta.Live = true;
                ++LiveCount;
                return Handle{idx, meta.Generation};
            }

            [[nodiscard]] bool Resolve(Handle h) const
            {
                if (!h.IsValid() || h.Index >= Meta.size())
                {
                    return false;
                }

                const auto& meta = Meta[h.Index];
                return meta.Live && meta.Generation == h.Generation;
            }

            [[nodiscard]] bool ResolveForUse(Handle h)
            {
                if (!h.IsValid() || h.Index >= Meta.size())
                {
                    ++InvalidHandleCount;
                    return false;
                }

                const auto& meta = Meta[h.Index];
                if (!meta.Live || meta.Generation != h.Generation)
                {
                    ++StaleHandleCount;
                    return false;
                }
                return true;
            }

            bool Free(Handle h, std::uint64_t reuseFrame)
            {
                if (!ResolveForUse(h))
                {
                    return false;
                }

                auto& meta = Meta[h.Index];
                meta.Live = false;
                ++meta.Generation;
                PendingFree.push_back(PendingFreeSlot{.Index = h.Index, .ReuseFrame = reuseFrame});
                if (LiveCount > 0)
                {
                    --LiveCount;
                }
                return true;
            }

            [[nodiscard]] GpuWorld::PoolDiagnostics Diagnostics() const noexcept
            {
                return GpuWorld::PoolDiagnostics{
                    .Capacity = static_cast<std::uint32_t>(Meta.size()),
                    .LiveCount = LiveCount,
                    .ReusableCount = static_cast<std::uint32_t>(FreeList.size()),
                    .PendingFreeCount = static_cast<std::uint32_t>(PendingFree.size()),
                    .OverflowCount = OverflowCount,
                    .InvalidHandleCount = InvalidHandleCount,
                    .StaleHandleCount = StaleHandleCount,
                };
            }
        };

        template <class T>
        [[nodiscard]] bool FlushDirtyRuns(RHI::IDevice& device,
                            RHI::BufferHandle dst,
                            std::vector<T>& cpu,
                            std::vector<bool>& dirty)
        {
            if (!dst.IsValid() || cpu.empty() || cpu.size() != dirty.size())
            {
                return false;
            }

            bool flushed = false;
            std::size_t i = 0;
            while (i < dirty.size())
            {
                if (!dirty[i])
                {
                    ++i;
                    continue;
                }

                const std::size_t begin = i;
                while (i < dirty.size() && dirty[i])
                {
                    dirty[i] = false;
                    ++i;
                }

                const std::size_t count = i - begin;
                device.WriteBuffer(dst,
                                   cpu.data() + begin,
                                   static_cast<std::uint64_t>(count * sizeof(T)),
                                   static_cast<std::uint64_t>(begin * sizeof(T)));
                flushed = true;
            }
            return flushed;
        }

        [[nodiscard]] float FragmentationRatio(std::uint64_t fragmented, std::uint64_t usedHighWater) noexcept
        {
            if (usedHighWater == 0)
            {
                return 0.0f;
            }
            return static_cast<float>(static_cast<double>(fragmented) / static_cast<double>(usedHighWater));
        }

        [[nodiscard]] bool ChannelSizeMatches(const std::span<const std::byte> bytes,
                                               const std::uint32_t vertexCount,
                                               const std::uint32_t elemSize) noexcept
        {
            return bytes.size_bytes() ==
                static_cast<std::uint64_t>(vertexCount) * static_cast<std::uint64_t>(elemSize);
        }

        [[nodiscard]] bool ExplicitChannelInvalid(
            const std::span<const std::byte> bytes,
            const std::uint32_t vertexCount,
            const std::uint32_t elemSize) noexcept
        {
            return !bytes.empty() && !ChannelSizeMatches(bytes, vertexCount, elemSize);
        }

        [[nodiscard]] bool PackedVertexStrideCanProvide(
            const GpuWorld::GeometryUploadDesc& desc,
            const std::uint32_t channelOffset,
            const std::uint32_t elemSize) noexcept
        {
            if (desc.VertexCount == 0u || desc.PackedVertexBytes.empty())
            {
                return false;
            }
            const std::uint64_t packedVertexSize = desc.PackedVertexBytes.size_bytes();
            if ((packedVertexSize % desc.VertexCount) != 0u)
            {
                return false;
            }
            const std::uint32_t stride =
                static_cast<std::uint32_t>(packedVertexSize / desc.VertexCount);
            return stride >= channelOffset + elemSize;
        }

        [[nodiscard]] bool UploadProvidesChannel(
            const GpuWorld::GeometryUploadDesc& desc,
            const std::span<const std::byte> explicitBytes,
            const std::uint32_t packedOffset,
            const std::uint32_t elemSize) noexcept
        {
            return !explicitBytes.empty() ||
                PackedVertexStrideCanProvide(desc, packedOffset, elemSize);
        }

        [[nodiscard]] std::vector<std::byte> DeinterleaveChannel(
            const std::span<const std::byte> packed,
            const std::uint32_t vertexCount,
            const std::uint32_t stride,
            const std::uint32_t channelOffset,
            const std::uint32_t elemSize)
        {
            std::vector<std::byte> out(
                static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(elemSize));
            for (std::uint32_t v = 0; v < vertexCount; ++v)
            {
                std::memcpy(out.data() + static_cast<std::size_t>(v) * elemSize,
                            packed.data() + static_cast<std::size_t>(v) * stride + channelOffset,
                            elemSize);
            }
            return out;
        }

        [[nodiscard]] std::uint64_t AppendChannelBytes(
            std::vector<std::byte>& dst,
            const std::span<const std::byte> src,
            std::uint64_t cursor,
            const std::uint32_t addressAlignment,
            std::uint64_t& outByteOffset,
            std::uint64_t& outByteCount)
        {
            outByteOffset = 0u;
            outByteCount = src.size_bytes();
            if (src.empty())
            {
                return cursor;
            }

            const std::uint64_t offset =
                AlignUp(cursor, addressAlignment);
            outByteOffset = offset;
            const std::uint64_t end = offset + outByteCount;
            dst.resize(static_cast<std::size_t>(end));
            std::memcpy(dst.data() + static_cast<std::ptrdiff_t>(offset),
                        src.data(),
                        static_cast<std::size_t>(outByteCount));
            return end;
        }

        struct UploadChannelBytes
        {
            std::vector<std::byte> Position;
            std::vector<std::byte> Texcoord;
            std::vector<std::byte> Normal;
            std::vector<std::byte> Color;
            bool Valid = true;
        };

        void AssignBytes(std::vector<std::byte>& dst, const std::span<const std::byte> src)
        {
            dst.assign(src.begin(), src.end());
        }

        [[nodiscard]] UploadChannelBytes BuildUploadChannelBytes(
            const GpuWorld::GeometryUploadDesc& desc)
        {
            UploadChannelBytes out{};

            std::span<const std::byte> positionBytes = desc.PositionBytes;
            std::span<const std::byte> texcoordBytes = desc.TexcoordBytes;
            std::span<const std::byte> normalBytes = desc.NormalBytes;

            const std::uint64_t packedVertexSize = desc.PackedVertexBytes.size_bytes();
            if (desc.VertexCount > 0u && !desc.PackedVertexBytes.empty())
            {
                if ((packedVertexSize % desc.VertexCount) != 0u)
                {
                    out.Valid = false;
                    return out;
                }
                const std::uint32_t legacyStride =
                    static_cast<std::uint32_t>(packedVertexSize / desc.VertexCount);
                if (positionBytes.empty() && legacyStride >= kPositionElementBytes)
                {
                    out.Position = DeinterleaveChannel(
                        desc.PackedVertexBytes,
                        desc.VertexCount,
                        legacyStride,
                        0u,
                        kPositionElementBytes);
                    positionBytes = std::span<const std::byte>{out.Position};
                }
                if (texcoordBytes.empty() &&
                    legacyStride >= kPositionElementBytes + kTexcoordElementBytes)
                {
                    out.Texcoord = DeinterleaveChannel(
                        desc.PackedVertexBytes,
                        desc.VertexCount,
                        legacyStride,
                        kPositionElementBytes,
                        kTexcoordElementBytes);
                    texcoordBytes = std::span<const std::byte>{out.Texcoord};
                }
                if (normalBytes.empty() &&
                    legacyStride >= kPositionElementBytes + kTexcoordElementBytes + kNormalElementBytes)
                {
                    out.Normal = DeinterleaveChannel(
                        desc.PackedVertexBytes,
                        desc.VertexCount,
                        legacyStride,
                        kPositionElementBytes + kTexcoordElementBytes,
                        kNormalElementBytes);
                    normalBytes = std::span<const std::byte>{out.Normal};
                }
            }

            if (out.Position.empty())
            {
                AssignBytes(out.Position, positionBytes);
            }
            if (out.Texcoord.empty())
            {
                AssignBytes(out.Texcoord, texcoordBytes);
            }
            if (out.Normal.empty())
            {
                AssignBytes(out.Normal, normalBytes);
            }
            if (!desc.PackedVertexColors.empty())
            {
                AssignBytes(out.Color, std::as_bytes(desc.PackedVertexColors));
            }

            out.Valid =
                (desc.VertexCount == 0u ||
                 (!out.Position.empty() &&
                  ChannelSizeMatches(out.Position, desc.VertexCount, kPositionElementBytes))) &&
                (out.Texcoord.empty() ||
                 ChannelSizeMatches(out.Texcoord, desc.VertexCount, kTexcoordElementBytes)) &&
                (out.Normal.empty() ||
                 ChannelSizeMatches(out.Normal, desc.VertexCount, kNormalElementBytes)) &&
                (out.Color.empty() ||
                 ChannelSizeMatches(out.Color, desc.VertexCount, kColorElementBytes));
            return out;
        }
    }

    GpuWorld::GeometryStoragePlan GpuWorld::PlanGeometryStorage(
        const GeometryUploadDesc& desc,
        const GeometryStorageHint hint) noexcept
    {
        GeometryStoragePlan plan{};
        if (hint == GeometryStorageHint::DynamicSoA)
        {
            plan.Status = GeometryStoragePlanStatus::DynamicHint;
            return plan;
        }

        if (desc.VertexCount == 0u)
        {
            plan.Status = GeometryStoragePlanStatus::MissingStaticSurfaceChannels;
            return plan;
        }

        if ((!desc.PackedVertexBytes.empty() &&
             (desc.PackedVertexBytes.size_bytes() % desc.VertexCount) != 0u) ||
            ExplicitChannelInvalid(desc.PositionBytes, desc.VertexCount, kPositionElementBytes) ||
            ExplicitChannelInvalid(desc.TexcoordBytes, desc.VertexCount, kTexcoordElementBytes) ||
            ExplicitChannelInvalid(desc.NormalBytes, desc.VertexCount, kNormalElementBytes) ||
            (!desc.PackedVertexColors.empty() &&
             desc.PackedVertexColors.size() != static_cast<std::size_t>(desc.VertexCount)))
        {
            plan.Status = GeometryStoragePlanStatus::InvalidInput;
            return plan;
        }

        const bool hasSurface = !desc.SurfaceIndices.empty();
        const bool hasPosition =
            UploadProvidesChannel(desc, desc.PositionBytes, 0u, kPositionElementBytes);
        const bool hasTexcoord =
            UploadProvidesChannel(desc, desc.TexcoordBytes, kPositionElementBytes, kTexcoordElementBytes);
        const bool hasNormal =
            UploadProvidesChannel(desc,
                                  desc.NormalBytes,
                                  kPositionElementBytes + kTexcoordElementBytes,
                                  kNormalElementBytes);

        if (!hasSurface || !hasPosition || !hasTexcoord || !hasNormal)
        {
            plan.Status = GeometryStoragePlanStatus::MissingStaticSurfaceChannels;
            return plan;
        }

        plan.Lane = GeometryStorageLane::StaticInterleavedAoS;
        plan.Status = GeometryStoragePlanStatus::SelectedStaticInterleavedAoS;
        plan.EligibleForStaticAoS = true;
        plan.RequiresPromotionOnStreamingEdit = true;
        return plan;
    }

    GpuWorld::GeometryStoragePromotionPlan GpuWorld::PlanGeometryStoragePromotion(
        const GeometryStorageLane currentLane,
        const GeometryChannelUpdateMask streamingChannels) noexcept
    {
        GeometryStoragePromotionPlan plan{};
        plan.StreamingChannels = streamingChannels;
        if (currentLane != GeometryStorageLane::StaticInterleavedAoS ||
            !streamingChannels.Any())
        {
            return plan;
        }

        plan.Status = GeometryStoragePromotionStatus::PromoteStreamingEditToSoA;
        plan.RequiresPromotion = true;
        plan.RequiresSoAConversion = true;
        plan.RequiresFullReupload = true;
        plan.RequiresInstanceRebind = true;
        return plan;
    }

    struct GpuWorld::Impl
    {
        RHI::IDevice* Device = nullptr;
        RHI::BufferManager* Buffers = nullptr;
        InitDesc Desc{};
        bool Initialized = false;

        SlotAllocator<GpuInstanceTag> InstanceSlots;
        SlotAllocator<GpuGeometryTag> GeometrySlots;

        std::vector<RHI::GpuInstanceStatic>  InstanceStaticCpu;
        std::vector<RHI::GpuInstanceDynamic> InstanceDynamicCpu;
        std::vector<RHI::GpuEntityConfig>    EntityConfigCpu;
        std::vector<RHI::GpuGeometryRecord>  GeometryRecordsCpu;
        std::vector<RHI::GpuBounds>          BoundsCpu;
        std::vector<RHI::GpuLight>           LightsCpu;
        std::vector<ManagedGeometryAllocation> GeometryAllocations;
        RHI::GpuSceneTable                   SceneTableCpu{};
        ClusterLightTableDesc                ClusterLights{};

        std::vector<bool> DirtyInstanceStatic;
        std::vector<bool> DirtyInstanceDynamic;
        std::vector<bool> DirtyEntityConfig;
        std::vector<bool> DirtyGeometryRecord;
        std::vector<bool> DirtyBounds;
        bool DirtyLights = false;
        bool DirtySceneTable = true;
        bool PendingSceneTableUploadBarrier = false;
        bool PendingInstanceStaticUploadBarrier = false;
        bool PendingInstanceDynamicUploadBarrier = false;
        bool PendingEntityConfigUploadBarrier = false;
        bool PendingGeometryRecordUploadBarrier = false;
        bool PendingBoundsUploadBarrier = false;
        bool PendingLightsUploadBarrier = false;
        bool PendingManagedVertexUploadBarrier = false;
        GpuWorld::GeometryChannelUpdateMask PendingManagedVertexChannelUploadBarriers{};
        bool PendingManagedIndexUploadBarrier = false;
        std::uint64_t FrameIndex = 0;
        std::uint32_t VertexOverflowCount = 0;
        std::uint32_t IndexOverflowCount = 0;
        std::uint32_t LightOverflowCount = 0;
        std::uint64_t ManagedCompactionBytesMoved = 0;
        std::uint32_t ManagedCompactionCount = 0;
        std::uint32_t StaleCompactionRelocationCount = 0;
        std::uint64_t NextGeometryContentRevision = 1u;

        std::uint64_t VertexBumpOffset = 0;
        std::uint32_t VertexElementBumpOffset = 0;
        std::uint64_t IndexBumpOffset  = 0;

        RHI::BufferManager::BufferLease InstanceStaticLease;
        RHI::BufferManager::BufferLease InstanceDynamicLease;
        RHI::BufferManager::BufferLease EntityConfigLease;
        RHI::BufferManager::BufferLease GeometryRecordLease;
        RHI::BufferManager::BufferLease BoundsLease;
        RHI::BufferManager::BufferLease LightLease;
        RHI::BufferManager::BufferLease SceneTableLease;
        RHI::BufferManager::BufferLease ManagedVertexLease;
        RHI::BufferManager::BufferLease ManagedIndexLease;

        RHI::BufferHandle MaterialBuffer{};
        std::uint32_t MaterialCapacity = 0;

        [[nodiscard]] std::uint64_t IssueGeometryContentRevision() noexcept
        {
            const std::uint64_t revision = NextGeometryContentRevision++;
            assert(revision != 0u &&
                   "GpuWorld geometry content revision exhausted");
            if (NextGeometryContentRevision == 0u)
            {
                // A wrap would break the monotonic residency contract. This is
                // unreachable for practical world lifetimes; retain a nonzero
                // sentinel in release builds rather than issuing revision 0.
                NextGeometryContentRevision = revision;
            }
            return revision;
        }

        [[nodiscard]] bool AllocateBuffer(RHI::BufferManager::BufferLease& outLease,
                                          const RHI::BufferDesc& desc)
        {
            auto res = Buffers->Create(desc);
            if (!res.has_value())
            {
                return false;
            }
            outLease = std::move(*res);
            return true;
        }

        [[nodiscard]] bool AllocateGpuResources()
        {
            if (!Device || !Buffers || !Device->IsOperational())
            {
                return false;
            }

            if (!AllocateBuffer(InstanceStaticLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxInstances) * sizeof(RHI::GpuInstanceStatic),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.InstanceStatic",
                })) return false;
            if (!AllocateBuffer(InstanceDynamicLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxInstances) * sizeof(RHI::GpuInstanceDynamic),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.InstanceDynamic",
                })) return false;
            if (!AllocateBuffer(EntityConfigLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxInstances) * sizeof(RHI::GpuEntityConfig),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.EntityConfig",
                })) return false;
            if (!AllocateBuffer(GeometryRecordLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxGeometryRecords) * sizeof(RHI::GpuGeometryRecord),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.GeometryRecords",
                })) return false;
            if (!AllocateBuffer(BoundsLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxInstances) * sizeof(RHI::GpuBounds),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.Bounds",
                })) return false;
            if (!AllocateBuffer(LightLease, {
                    .SizeBytes = static_cast<std::uint64_t>(Desc.MaxLights) * sizeof(RHI::GpuLight),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.Lights",
                })) return false;
            if (!AllocateBuffer(SceneTableLease, {
                    .SizeBytes = sizeof(RHI::GpuSceneTable),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.SceneTable",
                })) return false;
            if (!AllocateBuffer(ManagedVertexLease, {
                    .SizeBytes = Desc.VertexBufferBytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.ManagedVertexBuffer0",
                })) return false;
            if (!AllocateBuffer(ManagedIndexLease, {
                    .SizeBytes = Desc.IndexBufferBytes,
                    .Usage = RHI::BufferUsage::Index | RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.ManagedIndexBuffer0",
                })) return false;

            RefreshSceneTable();
            return true;
        }

        void ReleaseGpuResources()
        {
            InstanceStaticLease = {};
            InstanceDynamicLease = {};
            EntityConfigLease = {};
            GeometryRecordLease = {};
            BoundsLease = {};
            LightLease = {};
            SceneTableLease = {};
            ManagedVertexLease = {};
            ManagedIndexLease = {};
        }

        void MarkAllDirty()
        {
            std::fill(DirtyInstanceStatic.begin(), DirtyInstanceStatic.end(), true);
            std::fill(DirtyInstanceDynamic.begin(), DirtyInstanceDynamic.end(), true);
            std::fill(DirtyEntityConfig.begin(), DirtyEntityConfig.end(), true);
            std::fill(DirtyGeometryRecord.begin(), DirtyGeometryRecord.end(), true);
            std::fill(DirtyBounds.begin(), DirtyBounds.end(), true);
            DirtyLights = true;
            DirtySceneTable = true;
        }

        void RefreshSceneTable()
        {
            if (!Device)
            {
                return;
            }

            SceneTableCpu.InstanceStaticBDA  = Device->GetBufferDeviceAddress(InstanceStaticLease.GetHandle());
            SceneTableCpu.InstanceDynamicBDA = Device->GetBufferDeviceAddress(InstanceDynamicLease.GetHandle());
            SceneTableCpu.EntityConfigBDA    = Device->GetBufferDeviceAddress(EntityConfigLease.GetHandle());
            SceneTableCpu.GeometryRecordBDA  = Device->GetBufferDeviceAddress(GeometryRecordLease.GetHandle());
            SceneTableCpu.BoundsBDA          = Device->GetBufferDeviceAddress(BoundsLease.GetHandle());
            SceneTableCpu.MaterialBDA        = Device->GetBufferDeviceAddress(MaterialBuffer);
            SceneTableCpu.LightBDA           = Device->GetBufferDeviceAddress(LightLease.GetHandle());
            SceneTableCpu.ClusterLightHeaderBDA =
                Device->GetBufferDeviceAddress(ClusterLights.HeaderBuffer);
            SceneTableCpu.ClusterLightIndexBDA =
                Device->GetBufferDeviceAddress(ClusterLights.IndexBuffer);
            SceneTableCpu.InstanceCapacity   = Desc.MaxInstances;
            SceneTableCpu.GeometryCapacity   = Desc.MaxGeometryRecords;
            SceneTableCpu.MaterialCapacity   = MaterialCapacity;
            SceneTableCpu.LightCount         = static_cast<std::uint32_t>(LightsCpu.size());
            SceneTableCpu.ClusterTilePx      = ClusterLights.TilePx;
            SceneTableCpu.ClusterTilesX      = ClusterLights.TilesX;
            SceneTableCpu.ClusterTilesY      = ClusterLights.TilesY;
            SceneTableCpu.ClusterSlicesZ     = ClusterLights.SlicesZ;
            SceneTableCpu.ClusterCellCount   = ClusterLights.CellCount;
            SceneTableCpu.ClusterMaxLightsPerCell = ClusterLights.MaxLightsPerCell;
            SceneTableCpu.ClusterNearZ       = ClusterLights.NearZ;
            SceneTableCpu.ClusterFarZ        = ClusterLights.FarZ;
            SceneTableCpu.ClusterProjectionScaleX = ClusterLights.ProjectionScaleX;
            SceneTableCpu.ClusterProjectionScaleY = ClusterLights.ProjectionScaleY;
            DirtySceneTable = true;
        }

        [[nodiscard]] std::uint64_t LiveVertexBytes() const noexcept
        {
            std::uint64_t bytes = 0;
            for (const auto& allocation : GeometryAllocations)
            {
                if (allocation.Live)
                {
                    bytes += allocation.VertexByteCount;
                }
            }
            return bytes;
        }

        [[nodiscard]] std::uint64_t LiveIndexBytes() const noexcept
        {
            std::uint64_t bytes = 0;
            for (const auto& allocation : GeometryAllocations)
            {
                if (allocation.Live)
                {
                    bytes += allocation.IndexByteCount();
                }
            }
            return bytes;
        }

        [[nodiscard]] ManagedBufferFragmentation VertexFragmentation() const noexcept
        {
            const std::uint64_t liveBytes = LiveVertexBytes();
            const std::uint64_t fragmented = VertexBumpOffset >= liveBytes ? VertexBumpOffset - liveBytes : 0;
            return ManagedBufferFragmentation{
                .CapacityBytes = Desc.VertexBufferBytes,
                .UsedHighWaterBytes = VertexBumpOffset,
                .LiveBytes = liveBytes,
                .FragmentedBytes = fragmented,
                .FragmentationRatio = FragmentationRatio(fragmented, VertexBumpOffset),
            };
        }

        [[nodiscard]] ManagedBufferFragmentation IndexFragmentation() const noexcept
        {
            const std::uint64_t liveBytes = LiveIndexBytes();
            const std::uint64_t fragmented = IndexBumpOffset >= liveBytes ? IndexBumpOffset - liveBytes : 0;
            return ManagedBufferFragmentation{
                .CapacityBytes = Desc.IndexBufferBytes,
                .UsedHighWaterBytes = IndexBumpOffset,
                .LiveBytes = liveBytes,
                .FragmentedBytes = fragmented,
                .FragmentationRatio = FragmentationRatio(fragmented, IndexBumpOffset),
            };
        }

        [[nodiscard]] std::uint32_t VertexOffsetUnits(const ManagedGeometryAllocation& allocation) const noexcept
        {
            return allocation.VertexElementOffset;
        }

        [[nodiscard]] std::uint32_t SurfaceFirstIndex(const ManagedGeometryAllocation& allocation) const noexcept
        {
            return static_cast<std::uint32_t>(allocation.IndexByteOffset / sizeof(std::uint32_t));
        }

        [[nodiscard]] std::uint32_t LineFirstIndex(const ManagedGeometryAllocation& allocation) const noexcept
        {
            return static_cast<std::uint32_t>((allocation.IndexByteOffset + allocation.SurfaceIndexByteCount) / sizeof(std::uint32_t));
        }

        void RewriteGeometryRecord(std::uint32_t slot)
        {
            auto& allocation = GeometryAllocations[slot];
            auto& rec = GeometryRecordsCpu[slot];
            rec = {};
            const std::uint64_t vertexBaseBda =
                Device ? Device->GetBufferDeviceAddress(ManagedVertexLease.GetHandle()) : 0;
            rec.VertexBufferBDA = allocation.PositionByteCount > 0u
                ? vertexBaseBda + allocation.VertexByteOffset + allocation.PositionByteOffset
                : 0u;
            rec.IndexBufferBDA = Device ? Device->GetBufferDeviceAddress(ManagedIndexLease.GetHandle()) : 0;
            rec.TexcoordBufferBDA = allocation.TexcoordByteCount > 0u
                ? vertexBaseBda + allocation.VertexByteOffset + allocation.TexcoordByteOffset
                : 0u;
            rec.NormalBufferBDA = allocation.NormalByteCount > 0u
                ? vertexBaseBda + allocation.VertexByteOffset + allocation.NormalByteOffset
                : 0u;
            const std::uint32_t vertexOffset = VertexOffsetUnits(allocation);
            rec.VertexOffset = vertexOffset;
            rec.VertexCount = allocation.VertexCount;
            rec.SurfaceFirstIndex = SurfaceFirstIndex(allocation);
            rec.SurfaceIndexCount = static_cast<std::uint32_t>(allocation.SurfaceIndices.size());
            rec.LineFirstIndex = LineFirstIndex(allocation);
            rec.LineIndexCount = static_cast<std::uint32_t>(allocation.LineIndices.size());
            rec.PointFirstVertex = vertexOffset;
            rec.PointVertexCount = allocation.VertexCount;
            rec.ColorBufferBDA = allocation.ColorByteCount > 0u
                ? vertexBaseBda + allocation.VertexByteOffset + allocation.ColorByteOffset
                : 0u;
            DirtyGeometryRecord[slot] = true;
        }

        void ReplayManagedUpload(const ManagedGeometryAllocation& allocation)
        {
            if (!Device || !Device->IsOperational())
            {
                return;
            }

            if (!allocation.VertexBytes.empty())
            {
                Device->WriteBuffer(ManagedVertexLease.GetHandle(),
                                    allocation.VertexBytes.data(),
                                    static_cast<std::uint64_t>(allocation.VertexBytes.size()),
                                    allocation.VertexByteOffset);
                PendingManagedVertexUploadBarrier = true;
            }
            if (!allocation.SurfaceIndices.empty())
            {
                Device->WriteBuffer(ManagedIndexLease.GetHandle(),
                                    allocation.SurfaceIndices.data(),
                                    allocation.SurfaceIndexByteCount,
                                    allocation.IndexByteOffset);
                PendingManagedIndexUploadBarrier = true;
            }
            if (!allocation.LineIndices.empty())
            {
                Device->WriteBuffer(ManagedIndexLease.GetHandle(),
                                    allocation.LineIndices.data(),
                                    allocation.LineIndexByteCount,
                                    allocation.IndexByteOffset + allocation.SurfaceIndexByteCount);
                PendingManagedIndexUploadBarrier = true;
            }
        }
    };

    GpuWorld::GpuWorld()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    GpuWorld::~GpuWorld()
    {
        if (m_Impl && m_Impl->Initialized)
        {
            Shutdown();
        }
    }

    bool GpuWorld::Initialize(RHI::IDevice& device, RHI::BufferManager& buffers, const InitDesc& desc)
    {
        assert(!m_Impl->Initialized && "GpuWorld::Initialize called twice");
        m_Impl->Device = &device;
        m_Impl->Buffers = &buffers;
        m_Impl->Desc = desc;

        m_Impl->InstanceSlots.Reset(desc.MaxInstances);
        m_Impl->GeometrySlots.Reset(desc.MaxGeometryRecords);

        m_Impl->InstanceStaticCpu.assign(desc.MaxInstances, {});
        m_Impl->InstanceDynamicCpu.assign(desc.MaxInstances, {});
        m_Impl->EntityConfigCpu.assign(desc.MaxInstances, {});
        m_Impl->BoundsCpu.assign(desc.MaxInstances, {});
        m_Impl->GeometryRecordsCpu.assign(desc.MaxGeometryRecords, {});
        m_Impl->GeometryAllocations.assign(desc.MaxGeometryRecords, {});
        m_Impl->LightsCpu.clear();
        m_Impl->LightsCpu.reserve(desc.MaxLights);

        m_Impl->DirtyInstanceStatic.assign(desc.MaxInstances, true);
        m_Impl->DirtyInstanceDynamic.assign(desc.MaxInstances, true);
        m_Impl->DirtyEntityConfig.assign(desc.MaxInstances, true);
        m_Impl->DirtyBounds.assign(desc.MaxInstances, true);
        m_Impl->DirtyGeometryRecord.assign(desc.MaxGeometryRecords, true);
        m_Impl->DirtyLights = true;
        m_Impl->NextGeometryContentRevision = 1u;

        if (device.IsOperational() && !m_Impl->AllocateGpuResources())
        {
            return false;
        }

        m_Impl->Initialized = true;
        m_Impl->RefreshSceneTable();
        return true;
    }

    bool GpuWorld::Initialize(RHI::IDevice& device, RHI::BufferManager& buffers)
    {
        return Initialize(device, buffers, InitDesc{});
    }

    bool GpuWorld::RebuildGpuResources(RHI::IDevice& device, RHI::BufferManager& buffers)
    {
        if (!m_Impl->Initialized || !device.IsOperational())
        {
            return false;
        }

        m_Impl->Device = &device;
        m_Impl->Buffers = &buffers;
        m_Impl->ReleaseGpuResources();
        if (!m_Impl->AllocateGpuResources())
        {
            return false;
        }

        for (std::uint32_t slot = 0; slot < m_Impl->GeometryAllocations.size(); ++slot)
        {
            auto& allocation = m_Impl->GeometryAllocations[slot];
            if (!allocation.Live)
            {
                continue;
            }
            m_Impl->ReplayManagedUpload(allocation);
            m_Impl->RewriteGeometryRecord(slot);
        }

        m_Impl->MarkAllDirty();
        return true;
    }

    void GpuWorld::Shutdown()
    {
        if (!m_Impl->Initialized)
        {
            return;
        }

        m_Impl->ReleaseGpuResources();

        m_Impl->InstanceStaticCpu.clear();
        m_Impl->InstanceDynamicCpu.clear();
        m_Impl->EntityConfigCpu.clear();
        m_Impl->GeometryRecordsCpu.clear();
        m_Impl->GeometryAllocations.clear();
        m_Impl->BoundsCpu.clear();
        m_Impl->LightsCpu.clear();
        m_Impl->DirtyInstanceStatic.clear();
        m_Impl->DirtyInstanceDynamic.clear();
        m_Impl->DirtyEntityConfig.clear();
        m_Impl->DirtyGeometryRecord.clear();
        m_Impl->DirtyBounds.clear();

        m_Impl->VertexBumpOffset = 0;
        m_Impl->VertexElementBumpOffset = 0;
        m_Impl->IndexBumpOffset = 0;
        m_Impl->PendingManagedVertexUploadBarrier = false;
        m_Impl->PendingManagedVertexChannelUploadBarriers = {};
        m_Impl->PendingManagedIndexUploadBarrier = false;
        m_Impl->FrameIndex = 0;
        m_Impl->VertexOverflowCount = 0;
        m_Impl->IndexOverflowCount = 0;
        m_Impl->LightOverflowCount = 0;
        m_Impl->ManagedCompactionBytesMoved = 0;
        m_Impl->ManagedCompactionCount = 0;
        m_Impl->StaleCompactionRelocationCount = 0;
        m_Impl->NextGeometryContentRevision = 1u;
        m_Impl->MaterialBuffer = {};
        m_Impl->MaterialCapacity = 0;

        m_Impl->InstanceSlots.Reset(0);
        m_Impl->GeometrySlots.Reset(0);

        m_Impl->Device = nullptr;
        m_Impl->Buffers = nullptr;
        m_Impl->Initialized = false;
    }

    bool GpuWorld::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    GpuInstanceHandle GpuWorld::AllocateInstance(std::uint32_t entityId)
    {
        auto h = m_Impl->InstanceSlots.Allocate();
        if (!h.IsValid())
        {
            return {};
        }

        auto& st = m_Impl->InstanceStaticCpu[h.Index];
        st = {};
        st.EntityID = entityId;
        st.ConfigSlot = h.Index;
        m_Impl->InstanceDynamicCpu[h.Index] = {};
        m_Impl->EntityConfigCpu[h.Index] = {};
        m_Impl->BoundsCpu[h.Index] = {};

        m_Impl->DirtyInstanceStatic[h.Index] = true;
        m_Impl->DirtyInstanceDynamic[h.Index] = true;
        m_Impl->DirtyEntityConfig[h.Index] = true;
        m_Impl->DirtyBounds[h.Index] = true;
        return h;
    }

    void GpuWorld::FreeInstance(GpuInstanceHandle instance)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index] = {};
        m_Impl->InstanceDynamicCpu[instance.Index] = {};
        m_Impl->EntityConfigCpu[instance.Index] = {};
        m_Impl->BoundsCpu[instance.Index] = {};

        m_Impl->DirtyInstanceStatic[instance.Index] = true;
        m_Impl->DirtyInstanceDynamic[instance.Index] = true;
        m_Impl->DirtyEntityConfig[instance.Index] = true;
        m_Impl->DirtyBounds[instance.Index] = true;

        m_Impl->InstanceSlots.Free(instance, m_Impl->FrameIndex + m_Impl->Desc.DeferredFreeFrames);
    }

    GpuGeometryHandle GpuWorld::UploadGeometry(const GeometryUploadDesc& desc)
    {
        auto h = m_Impl->GeometrySlots.Allocate();
        if (!h.IsValid())
        {
            return {};
        }

        std::vector<std::byte> deinterleavedPositions;
        std::vector<std::byte> deinterleavedTexcoords;
        std::vector<std::byte> deinterleavedNormals;

        std::span<const std::byte> positionBytes = desc.PositionBytes;
        std::span<const std::byte> texcoordBytes = desc.TexcoordBytes;
        std::span<const std::byte> normalBytes = desc.NormalBytes;

        const std::uint64_t packedVertexSize = desc.PackedVertexBytes.size_bytes();
        if (desc.VertexCount > 0u && !desc.PackedVertexBytes.empty())
        {
            assert((packedVertexSize % desc.VertexCount) == 0u);
            const std::uint32_t legacyStride =
                static_cast<std::uint32_t>(packedVertexSize / desc.VertexCount);
            if (positionBytes.empty() && legacyStride >= kPositionElementBytes)
            {
                deinterleavedPositions = DeinterleaveChannel(
                    desc.PackedVertexBytes,
                    desc.VertexCount,
                    legacyStride,
                    0u,
                    kPositionElementBytes);
                positionBytes = std::span<const std::byte>{deinterleavedPositions};
            }
            if (texcoordBytes.empty() &&
                legacyStride >= kPositionElementBytes + kTexcoordElementBytes)
            {
                deinterleavedTexcoords = DeinterleaveChannel(
                    desc.PackedVertexBytes,
                    desc.VertexCount,
                    legacyStride,
                    kPositionElementBytes,
                    kTexcoordElementBytes);
                texcoordBytes = std::span<const std::byte>{deinterleavedTexcoords};
            }
            if (normalBytes.empty() &&
                legacyStride >= kPositionElementBytes + kTexcoordElementBytes + kNormalElementBytes)
            {
                deinterleavedNormals = DeinterleaveChannel(
                    desc.PackedVertexBytes,
                    desc.VertexCount,
                    legacyStride,
                    kPositionElementBytes + kTexcoordElementBytes,
                    kNormalElementBytes);
                normalBytes = std::span<const std::byte>{deinterleavedNormals};
            }
        }

        const std::uint64_t surfSize = desc.SurfaceIndices.size_bytes();
        const std::uint64_t lineSize = desc.LineIndices.size_bytes();

        if (desc.VertexCount > 0u &&
            (positionBytes.empty() ||
             !ChannelSizeMatches(positionBytes, desc.VertexCount, kPositionElementBytes) ||
             (!texcoordBytes.empty() &&
              !ChannelSizeMatches(texcoordBytes, desc.VertexCount, kTexcoordElementBytes)) ||
             (!normalBytes.empty() &&
              !ChannelSizeMatches(normalBytes, desc.VertexCount, kNormalElementBytes))))
        {
            m_Impl->GeometrySlots.Free(h, m_Impl->FrameIndex);
            return {};
        }
        if (!desc.PackedVertexColors.empty() &&
            desc.PackedVertexColors.size() != static_cast<std::size_t>(desc.VertexCount))
        {
            m_Impl->GeometrySlots.Free(h, m_Impl->FrameIndex);
            return {};
        }

        std::vector<std::byte> managedVertexBytes;
        std::uint64_t cursor = 0u;
        std::uint64_t positionOffset = 0u;
        std::uint64_t texcoordOffset = 0u;
        std::uint64_t normalOffset = 0u;
        std::uint64_t colorOffset = 0u;
        std::uint64_t positionByteCount = 0u;
        std::uint64_t texcoordByteCount = 0u;
        std::uint64_t normalByteCount = 0u;
        std::uint64_t colorByteCount = 0u;
        cursor = AppendChannelBytes(
            managedVertexBytes,
            positionBytes,
            cursor,
            kPhysicalStorageScalarAddressAlignment,
            positionOffset,
            positionByteCount);
        cursor = AppendChannelBytes(
            managedVertexBytes,
            texcoordBytes,
            cursor,
            kPhysicalStorageTexcoordAddressAlignment,
            texcoordOffset,
            texcoordByteCount);
        cursor = AppendChannelBytes(
            managedVertexBytes,
            normalBytes,
            cursor,
            kPhysicalStorageScalarAddressAlignment,
            normalOffset,
            normalByteCount);
        if (!desc.PackedVertexColors.empty())
        {
            const std::uint64_t colorSize =
                static_cast<std::uint64_t>(desc.PackedVertexColors.size()) * kColorElementBytes;
            const std::uint64_t alignedColorOffset = AlignUp(cursor, alignof(std::uint32_t));
            colorOffset = alignedColorOffset;
            const std::uint64_t end = alignedColorOffset + colorSize;
            managedVertexBytes.resize(static_cast<std::size_t>(end));
            std::memcpy(managedVertexBytes.data() + static_cast<std::ptrdiff_t>(alignedColorOffset),
                        desc.PackedVertexColors.data(),
                        static_cast<std::size_t>(colorSize));
            cursor = end;
            colorByteCount = colorSize;
        }
        const std::uint64_t managedVertexSize = cursor;

        const std::uint64_t vbOffset = AlignUp(m_Impl->VertexBumpOffset, kManagedVertexBlockAlignment);
        const std::uint64_t surfOffset = m_Impl->IndexBumpOffset;
        const std::uint64_t lineOffset = surfOffset + surfSize;

        if (vbOffset + managedVertexSize > m_Impl->Desc.VertexBufferBytes ||
            lineOffset + lineSize > m_Impl->Desc.IndexBufferBytes)
        {
            if (vbOffset + managedVertexSize > m_Impl->Desc.VertexBufferBytes)
            {
                ++m_Impl->VertexOverflowCount;
            }
            if (lineOffset + lineSize > m_Impl->Desc.IndexBufferBytes)
            {
                ++m_Impl->IndexOverflowCount;
            }
            m_Impl->GeometrySlots.Free(h, m_Impl->FrameIndex);
            return {};
        }

        if (m_Impl->Device->IsOperational())
        {
            if (managedVertexSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedVertexBuffer(),
                                            managedVertexBytes.data(),
                                            managedVertexSize,
                                            vbOffset);
                m_Impl->PendingManagedVertexUploadBarrier = true;
            }
            if (surfSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedIndexBuffer(),
                                            desc.SurfaceIndices.data(),
                                            surfSize,
                                            surfOffset);
                m_Impl->PendingManagedIndexUploadBarrier = true;
            }
            if (lineSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedIndexBuffer(),
                                            desc.LineIndices.data(),
                                            lineSize,
                                            lineOffset);
                m_Impl->PendingManagedIndexUploadBarrier = true;
            }
        }

        auto& allocation = m_Impl->GeometryAllocations[h.Index];
        allocation = {};
        allocation.Live = true;
        allocation.Generation = h.Generation;
        allocation.VertexByteOffset = vbOffset;
        allocation.VertexByteCount = managedVertexSize;
        allocation.PositionByteOffset = positionOffset;
        allocation.PositionByteCount = positionByteCount;
        allocation.TexcoordByteOffset = texcoordOffset;
        allocation.TexcoordByteCount = texcoordByteCount;
        allocation.NormalByteOffset = normalOffset;
        allocation.NormalByteCount = normalByteCount;
        allocation.ColorByteOffset = colorOffset;
        allocation.ColorByteCount = colorByteCount;
        allocation.IndexByteOffset = surfOffset;
        allocation.SurfaceIndexByteCount = surfSize;
        allocation.LineIndexByteCount = lineSize;
        allocation.VertexCount = desc.VertexCount;
        allocation.VertexElementOffset = m_Impl->VertexElementBumpOffset;
        allocation.StorageLane = GeometryStorageLane::UniformSoA;
        allocation.VertexBytes = std::move(managedVertexBytes);
        allocation.SurfaceIndices.assign(desc.SurfaceIndices.begin(), desc.SurfaceIndices.end());
        allocation.LineIndices.assign(desc.LineIndices.begin(), desc.LineIndices.end());
        RefreshResidencyFingerprints(allocation);
        allocation.ContentRevision =
            m_Impl->IssueGeometryContentRevision();

        m_Impl->RewriteGeometryRecord(h.Index);

        m_Impl->VertexBumpOffset = vbOffset + managedVertexSize;
        m_Impl->VertexElementBumpOffset += desc.VertexCount;
        m_Impl->IndexBumpOffset = lineOffset + lineSize;
        return h;
    }

    GpuWorld::GeometryChannelUpdateResult GpuWorld::UpdateGeometryChannels(
        const GpuGeometryHandle geometry,
        const GeometryUploadDesc& desc,
        const GeometryChannelUpdateMask channels)
    {
        GeometryChannelUpdateResult result{};
        if (!channels.Any())
        {
            result.Status = GeometryChannelUpdateStatus::NoChannels;
            return result;
        }
        if (!m_Impl->GeometrySlots.ResolveForUse(geometry) ||
            geometry.Index >= m_Impl->GeometryAllocations.size())
        {
            result.Status = GeometryChannelUpdateStatus::InvalidHandle;
            return result;
        }

        auto& allocation = m_Impl->GeometryAllocations[geometry.Index];
        if (!allocation.Live || allocation.Generation != geometry.Generation)
        {
            result.Status = GeometryChannelUpdateStatus::InvalidHandle;
            return result;
        }

        const std::uint64_t surfSize = desc.SurfaceIndices.size_bytes();
        const std::uint64_t lineSize = desc.LineIndices.size_bytes();
        if (desc.VertexCount != allocation.VertexCount ||
            surfSize != allocation.SurfaceIndexByteCount ||
            lineSize != allocation.LineIndexByteCount)
        {
            result.Status = GeometryChannelUpdateStatus::FullUploadRequired;
            return result;
        }
        if (!std::equal(
                desc.SurfaceIndices.begin(),
                desc.SurfaceIndices.end(),
                allocation.SurfaceIndices.begin()) ||
            !std::equal(
                desc.LineIndices.begin(),
                desc.LineIndices.end(),
                allocation.LineIndices.begin()))
        {
            result.Status = GeometryChannelUpdateStatus::FullUploadRequired;
            return result;
        }

        const UploadChannelBytes upload = BuildUploadChannelBytes(desc);
        if (!upload.Valid)
        {
            result.Status = GeometryChannelUpdateStatus::InvalidInput;
            return result;
        }

        bool wroteAnyChannel = false;
        bool rewroteRecord = false;
        bool requiresFullUpload = false;

        const auto requiredChannelCanUpdate =
            [](const bool requested,
               const std::span<const std::byte> bytes,
               const std::uint64_t byteCount) noexcept
        {
            return !requested ||
                (!bytes.empty() && byteCount != 0u && bytes.size_bytes() == byteCount);
        };
        const auto optionalChannelCanUpdate =
            [](const bool requested,
               const std::span<const std::byte> bytes,
               const std::uint64_t byteCount) noexcept
        {
            return !requested ||
                bytes.empty() ||
                (byteCount != 0u && bytes.size_bytes() == byteCount);
        };

        if (!requiredChannelCanUpdate(channels.Position,
                                      std::span<const std::byte>{upload.Position},
                                      allocation.PositionByteCount) ||
            !optionalChannelCanUpdate(channels.Texcoord,
                                      std::span<const std::byte>{upload.Texcoord},
                                      allocation.TexcoordByteCount) ||
            !optionalChannelCanUpdate(channels.Normal,
                                      std::span<const std::byte>{upload.Normal},
                                      allocation.NormalByteCount) ||
            !optionalChannelCanUpdate(channels.Color,
                                      std::span<const std::byte>{upload.Color},
                                      allocation.ColorByteCount))
        {
            result.Status = GeometryChannelUpdateStatus::FullUploadRequired;
            return result;
        }

        const auto writeRequiredChannel =
            [&](const bool requested,
                const std::span<const std::byte> bytes,
                const std::uint64_t byteOffset,
                const std::uint64_t byteCount,
                bool& uploadedFlag)
        {
            if (!requested)
            {
                return;
            }
            if (bytes.empty() || byteCount == 0u || bytes.size_bytes() != byteCount)
            {
                requiresFullUpload = true;
                return;
            }
            std::memcpy(allocation.VertexBytes.data() +
                            static_cast<std::ptrdiff_t>(byteOffset),
                        bytes.data(),
                        static_cast<std::size_t>(byteCount));
            if (m_Impl->Device != nullptr && m_Impl->Device->IsOperational())
            {
                m_Impl->Device->WriteBuffer(GetManagedVertexBuffer(),
                                            bytes.data(),
                                            byteCount,
                                            allocation.VertexByteOffset + byteOffset);
                uploadedFlag = true;
                wroteAnyChannel = true;
            }
        };

        const auto writeOptionalChannel =
            [&](const bool requested,
                const std::span<const std::byte> bytes,
                const std::uint64_t byteOffset,
                std::uint64_t& byteCount,
                bool& uploadedFlag)
        {
            if (!requested)
            {
                return;
            }
            if (bytes.empty())
            {
                if (byteCount != 0u)
                {
                    byteCount = 0u;
                    rewroteRecord = true;
                }
                return;
            }
            if (byteCount == 0u || bytes.size_bytes() != byteCount)
            {
                requiresFullUpload = true;
                return;
            }
            std::memcpy(allocation.VertexBytes.data() +
                            static_cast<std::ptrdiff_t>(byteOffset),
                        bytes.data(),
                        static_cast<std::size_t>(byteCount));
            if (m_Impl->Device != nullptr && m_Impl->Device->IsOperational())
            {
                m_Impl->Device->WriteBuffer(GetManagedVertexBuffer(),
                                            bytes.data(),
                                            byteCount,
                                            allocation.VertexByteOffset + byteOffset);
                uploadedFlag = true;
                wroteAnyChannel = true;
            }
        };

        writeRequiredChannel(channels.Position,
                             std::span<const std::byte>{upload.Position},
                             allocation.PositionByteOffset,
                             allocation.PositionByteCount,
                             result.UploadedChannels.Position);
        writeOptionalChannel(channels.Texcoord,
                             std::span<const std::byte>{upload.Texcoord},
                             allocation.TexcoordByteOffset,
                             allocation.TexcoordByteCount,
                             result.UploadedChannels.Texcoord);
        writeOptionalChannel(channels.Normal,
                             std::span<const std::byte>{upload.Normal},
                             allocation.NormalByteOffset,
                             allocation.NormalByteCount,
                             result.UploadedChannels.Normal);
        writeOptionalChannel(channels.Color,
                             std::span<const std::byte>{upload.Color},
                             allocation.ColorByteOffset,
                             allocation.ColorByteCount,
                             result.UploadedChannels.Color);

        if (requiresFullUpload)
        {
            result.Status = GeometryChannelUpdateStatus::FullUploadRequired;
            result.UploadedChannels = {};
            return result;
        }

        if (rewroteRecord)
        {
            m_Impl->RewriteGeometryRecord(geometry.Index);
            result.GeometryRecordUpdated = true;
        }
        if (wroteAnyChannel)
        {
            m_Impl->PendingManagedVertexChannelUploadBarriers.Position =
                m_Impl->PendingManagedVertexChannelUploadBarriers.Position ||
                result.UploadedChannels.Position;
            m_Impl->PendingManagedVertexChannelUploadBarriers.Texcoord =
                m_Impl->PendingManagedVertexChannelUploadBarriers.Texcoord ||
                result.UploadedChannels.Texcoord;
            m_Impl->PendingManagedVertexChannelUploadBarriers.Normal =
                m_Impl->PendingManagedVertexChannelUploadBarriers.Normal ||
                result.UploadedChannels.Normal;
            m_Impl->PendingManagedVertexChannelUploadBarriers.Color =
                m_Impl->PendingManagedVertexChannelUploadBarriers.Color ||
                result.UploadedChannels.Color;
        }
        RefreshUpdatedResidencyFingerprints(allocation, channels);
        allocation.ContentRevision =
            m_Impl->IssueGeometryContentRevision();
        result.Status = GeometryChannelUpdateStatus::Updated;
        return result;
    }

    void GpuWorld::FreeGeometry(GpuGeometryHandle geometry)
    {
        if (!m_Impl->GeometrySlots.ResolveForUse(geometry))
        {
            return;
        }

        for (std::uint32_t i = 0; i < m_Impl->Desc.MaxInstances; ++i)
        {
            const auto& instanceMeta = m_Impl->InstanceSlots.Meta[i];
            if (!instanceMeta.Live)
            {
                continue;
            }
            auto& instanceStatic = m_Impl->InstanceStaticCpu[i];
            if (instanceStatic.GeometrySlot == geometry.Index)
            {
                instanceStatic.GeometrySlot = RHI::GpuInstanceStatic::InvalidGeometrySlot;
                m_Impl->DirtyInstanceStatic[i] = true;
            }
        }

        m_Impl->GeometryRecordsCpu[geometry.Index] = {};
        if (geometry.Index < m_Impl->GeometryAllocations.size())
        {
            m_Impl->GeometryAllocations[geometry.Index].Live = false;
        }
        m_Impl->DirtyGeometryRecord[geometry.Index] = true;
        m_Impl->GeometrySlots.Free(geometry, m_Impl->FrameIndex + m_Impl->Desc.DeferredFreeFrames);
    }

    void GpuWorld::SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }
        if (geometry.IsValid() && !m_Impl->GeometrySlots.ResolveForUse(geometry))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].GeometrySlot =
            geometry.IsValid() ? geometry.Index : RHI::GpuInstanceStatic::InvalidGeometrySlot;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    GpuGeometryHandle GpuWorld::GetInstanceGeometry(GpuInstanceHandle instance) const noexcept
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return {};
        }
        const std::uint32_t geometrySlot = m_Impl->InstanceStaticCpu[instance.Index].GeometrySlot;
        if (geometrySlot == RHI::GpuInstanceStatic::InvalidGeometrySlot ||
            geometrySlot >= m_Impl->GeometrySlots.Meta.size())
        {
            return {};
        }
        const auto& meta = m_Impl->GeometrySlots.Meta[geometrySlot];
        if (!meta.Live)
        {
            return {};
        }
        return GpuGeometryHandle{geometrySlot, meta.Generation};
    }

    void GpuWorld::SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].MaterialSlot = materialSlot;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    void GpuWorld::SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].RenderFlags = flags;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    void GpuWorld::SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        auto& dyn = m_Impl->InstanceDynamicCpu[instance.Index];
        dyn.Model = model;
        dyn.PrevModel = prevModel;
        m_Impl->DirtyInstanceDynamic[instance.Index] = true;
    }

    void GpuWorld::SetEntityConfig(GpuInstanceHandle instance, const RHI::GpuEntityConfig& config)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        m_Impl->EntityConfigCpu[instance.Index] = config;
        m_Impl->DirtyEntityConfig[instance.Index] = true;
    }

    RHI::GpuEntityConfig GpuWorld::GetEntityConfigForTest(
        const GpuInstanceHandle instance) const noexcept
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return {};
        }
        return m_Impl->EntityConfigCpu[instance.Index];
    }

    void GpuWorld::SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds)
    {
        if (!m_Impl->InstanceSlots.ResolveForUse(instance))
        {
            return;
        }

        m_Impl->BoundsCpu[instance.Index] = bounds;
        m_Impl->DirtyBounds[instance.Index] = true;
    }

    void GpuWorld::SetMaterialBuffer(RHI::BufferHandle materialBuffer, std::uint32_t materialCapacity)
    {
        m_Impl->MaterialBuffer = materialBuffer;
        m_Impl->MaterialCapacity = materialCapacity;
        m_Impl->RefreshSceneTable();
    }

    void GpuWorld::SetCamera(const RHI::CameraUBO& camera)
    {
        m_Impl->SceneTableCpu.CameraView = camera.View;
        m_Impl->SceneTableCpu.CameraProj = camera.Proj;
        m_Impl->SceneTableCpu.CameraViewProj = camera.ViewProj;
        m_Impl->SceneTableCpu.CameraInvView = camera.InvView;
        m_Impl->SceneTableCpu.CameraInvProj = camera.InvProj;
        m_Impl->SceneTableCpu.CameraPosition = camera.CameraPosition;
        m_Impl->SceneTableCpu.CameraDirection = camera.CameraDirection;
        m_Impl->SceneTableCpu.CameraViewportWidth = camera.ViewportWidth;
        m_Impl->SceneTableCpu.CameraViewportHeight = camera.ViewportHeight;
        m_Impl->SceneTableCpu.CameraNearPlane = camera.NearPlane;
        m_Impl->SceneTableCpu.CameraFarPlane = camera.FarPlane;
        m_Impl->SceneTableCpu.CameraFrameIndex = camera.FrameIndex;
        m_Impl->SceneTableCpu.CameraCullingFlags = camera.CullingFlags;
        m_Impl->DirtySceneTable = true;
    }

    void GpuWorld::SetLights(std::span<const RHI::GpuLight> lights)
    {
        const std::size_t capped = std::min<std::size_t>(lights.size(), m_Impl->Desc.MaxLights);
        if (lights.size() > capped)
        {
            ++m_Impl->LightOverflowCount;
        }
        m_Impl->LightsCpu.assign(lights.begin(), lights.begin() + capped);
        m_Impl->DirtyLights = true;
        m_Impl->RefreshSceneTable();
    }

    void GpuWorld::SetClusterLightTable(const ClusterLightTableDesc& desc)
    {
        m_Impl->ClusterLights = desc;
        m_Impl->RefreshSceneTable();
    }

    void GpuWorld::ClearClusterLightTable()
    {
        m_Impl->ClusterLights = {};
        m_Impl->RefreshSceneTable();
    }

    GpuWorld::CompactionPlan GpuWorld::PlanManagedBufferCompaction() const
    {
        return PlanManagedBufferCompaction(CompactionPlanDesc{});
    }

    GpuWorld::CompactionPlan GpuWorld::PlanManagedBufferCompaction(const CompactionPlanDesc& desc) const
    {
        CompactionPlan plan{};
        plan.Enabled = desc.Enabled;
        plan.Vertex = m_Impl->VertexFragmentation();
        plan.Index = m_Impl->IndexFragmentation();
        plan.RecoverableBytes = plan.Vertex.FragmentedBytes + plan.Index.FragmentedBytes;

        if (!desc.Enabled)
        {
            return plan;
        }

        plan.BlockedByPendingFrees = !desc.AllowWhilePendingFrees &&
            (!m_Impl->GeometrySlots.PendingFree.empty() || !m_Impl->InstanceSlots.PendingFree.empty());

        std::uint64_t nextVertexOffset = 0;
        std::uint64_t nextIndexOffset = 0;
        for (std::uint32_t slot = 0; slot < m_Impl->GeometryAllocations.size(); ++slot)
        {
            const auto& allocation = m_Impl->GeometryAllocations[slot];
            if (!allocation.Live)
            {
                continue;
            }

            const std::uint64_t oldVertexOffset = allocation.VertexByteOffset;
            const std::uint64_t oldIndexOffset = allocation.IndexByteOffset;
            const std::uint64_t oldLineOffset = oldIndexOffset + allocation.SurfaceIndexByteCount;
            const std::uint64_t newVertexOffset = AlignUp(nextVertexOffset, kManagedVertexBlockAlignment);
            const std::uint64_t newIndexOffset = nextIndexOffset;
            const std::uint64_t newLineOffset = newIndexOffset + allocation.SurfaceIndexByteCount;

            if (oldVertexOffset != newVertexOffset || oldIndexOffset != newIndexOffset)
            {
                if (oldVertexOffset != newVertexOffset)
                {
                    plan.BytesToMove += allocation.VertexByteCount;
                }
                if (oldIndexOffset != newIndexOffset)
                {
                    plan.BytesToMove += allocation.IndexByteCount();
                }

                plan.Relocations.push_back(GeometryRelocation{
                    .Geometry = GpuGeometryHandle{slot, allocation.Generation},
                    .OldVertexByteOffset = oldVertexOffset,
                    .NewVertexByteOffset = newVertexOffset,
                    .VertexByteCount = allocation.VertexByteCount,
                    .OldIndexByteOffset = oldIndexOffset,
                    .NewIndexByteOffset = newIndexOffset,
                    .IndexByteCount = allocation.IndexByteCount(),
                    .OldVertexOffset = allocation.VertexElementOffset,
                    .NewVertexOffset = allocation.VertexElementOffset,
                    .OldSurfaceFirstIndex = static_cast<std::uint32_t>(oldIndexOffset / sizeof(std::uint32_t)),
                    .NewSurfaceFirstIndex = static_cast<std::uint32_t>(newIndexOffset / sizeof(std::uint32_t)),
                    .OldLineFirstIndex = static_cast<std::uint32_t>(oldLineOffset / sizeof(std::uint32_t)),
                    .NewLineFirstIndex = static_cast<std::uint32_t>(newLineOffset / sizeof(std::uint32_t)),
                });
            }

            nextVertexOffset = newVertexOffset + allocation.VertexByteCount;
            nextIndexOffset += allocation.IndexByteCount();
        }

        const float maxFragmentation = std::max(plan.Vertex.FragmentationRatio, plan.Index.FragmentationRatio);
        plan.ShouldCompact = !plan.BlockedByPendingFrees &&
            !plan.Relocations.empty() &&
            plan.RecoverableBytes >= desc.MinRecoverableBytes &&
            maxFragmentation >= desc.MinFragmentationRatio;
        return plan;
    }

    GpuWorld::CompactionResult GpuWorld::ApplyManagedBufferCompaction(const CompactionPlan& plan)
    {
        CompactionResult result{};
        result.RelocationCount = static_cast<std::uint32_t>(plan.Relocations.size());
        result.BytesMoved = plan.BytesToMove;

        if (!plan.Enabled || !plan.ShouldCompact)
        {
            result.Skipped = true;
            return result;
        }

        for (const auto& relocation : plan.Relocations)
        {
            if (!m_Impl->GeometrySlots.Resolve(relocation.Geometry) ||
                relocation.Geometry.Index >= m_Impl->GeometryAllocations.size())
            {
                ++result.StaleRelocationCount;
                continue;
            }

            const auto& allocation = m_Impl->GeometryAllocations[relocation.Geometry.Index];
            if (!allocation.Live ||
                allocation.Generation != relocation.Geometry.Generation ||
                allocation.VertexByteOffset != relocation.OldVertexByteOffset ||
                allocation.VertexByteCount != relocation.VertexByteCount ||
                allocation.IndexByteOffset != relocation.OldIndexByteOffset ||
                allocation.IndexByteCount() != relocation.IndexByteCount)
            {
                ++result.StaleRelocationCount;
            }
        }

        if (result.StaleRelocationCount > 0u)
        {
            result.RejectedStaleRelocations = true;
            m_Impl->StaleCompactionRelocationCount += result.StaleRelocationCount;
            return result;
        }

        std::uint64_t vertexHighWater = 0;
        std::uint64_t indexHighWater = 0;
        for (const auto& relocation : plan.Relocations)
        {
            auto& allocation = m_Impl->GeometryAllocations[relocation.Geometry.Index];
            allocation.VertexByteOffset = relocation.NewVertexByteOffset;
            allocation.IndexByteOffset = relocation.NewIndexByteOffset;
            m_Impl->ReplayManagedUpload(allocation);
            m_Impl->RewriteGeometryRecord(relocation.Geometry.Index);
        }

        for (const auto& allocation : m_Impl->GeometryAllocations)
        {
            if (!allocation.Live)
            {
                continue;
            }
            vertexHighWater = std::max(vertexHighWater, allocation.VertexByteOffset + allocation.VertexByteCount);
            indexHighWater = std::max(indexHighWater, allocation.IndexByteOffset + allocation.IndexByteCount());
        }

        m_Impl->VertexBumpOffset = vertexHighWater;
        m_Impl->IndexBumpOffset = indexHighWater;
        m_Impl->ManagedCompactionBytesMoved += result.BytesMoved;
        ++m_Impl->ManagedCompactionCount;
        result.Applied = true;
        return result;
    }

    void GpuWorld::SyncFrame()
    {
        ++m_Impl->FrameIndex;
        m_Impl->InstanceSlots.RetirePending(m_Impl->FrameIndex);
        m_Impl->GeometrySlots.RetirePending(m_Impl->FrameIndex);

        if (!m_Impl->Device || !m_Impl->Initialized || !m_Impl->Device->IsOperational())
        {
            return;
        }

        m_Impl->PendingInstanceStaticUploadBarrier =
            FlushDirtyRuns(*m_Impl->Device,
                           GetInstanceStaticBuffer(),
                           m_Impl->InstanceStaticCpu,
                           m_Impl->DirtyInstanceStatic) ||
            m_Impl->PendingInstanceStaticUploadBarrier;
        m_Impl->PendingInstanceDynamicUploadBarrier =
            FlushDirtyRuns(*m_Impl->Device,
                           GetInstanceDynamicBuffer(),
                           m_Impl->InstanceDynamicCpu,
                           m_Impl->DirtyInstanceDynamic) ||
            m_Impl->PendingInstanceDynamicUploadBarrier;
        m_Impl->PendingEntityConfigUploadBarrier =
            FlushDirtyRuns(*m_Impl->Device,
                           GetEntityConfigBuffer(),
                           m_Impl->EntityConfigCpu,
                           m_Impl->DirtyEntityConfig) ||
            m_Impl->PendingEntityConfigUploadBarrier;
        m_Impl->PendingGeometryRecordUploadBarrier =
            FlushDirtyRuns(*m_Impl->Device,
                           GetGeometryRecordBuffer(),
                           m_Impl->GeometryRecordsCpu,
                           m_Impl->DirtyGeometryRecord) ||
            m_Impl->PendingGeometryRecordUploadBarrier;
        m_Impl->PendingBoundsUploadBarrier =
            FlushDirtyRuns(*m_Impl->Device,
                           GetBoundsBuffer(),
                           m_Impl->BoundsCpu,
                           m_Impl->DirtyBounds) ||
            m_Impl->PendingBoundsUploadBarrier;

        if (m_Impl->DirtyLights && GetLightBuffer().IsValid())
        {
            if (!m_Impl->LightsCpu.empty())
            {
                m_Impl->Device->WriteBuffer(GetLightBuffer(),
                                            m_Impl->LightsCpu.data(),
                                            static_cast<std::uint64_t>(m_Impl->LightsCpu.size() * sizeof(RHI::GpuLight)),
                                            0);
                m_Impl->PendingLightsUploadBarrier = true;
            }
            m_Impl->DirtyLights = false;
        }

        if (m_Impl->DirtySceneTable && GetSceneTableBuffer().IsValid())
        {
            m_Impl->Device->WriteBuffer(GetSceneTableBuffer(),
                                        &m_Impl->SceneTableCpu,
                                        sizeof(RHI::GpuSceneTable),
                                        0);
            m_Impl->PendingSceneTableUploadBarrier = true;
            m_Impl->DirtySceneTable = false;
        }
    }

    void GpuWorld::SubmitPendingUploadBarriers(RHI::ICommandContext& cmd)
    {
        const auto submit = [&cmd](const RHI::BufferHandle buffer,
                                   bool& pending,
                                   const RHI::MemoryAccess after)
        {
            if (!pending || !buffer.IsValid())
            {
                return;
            }
            cmd.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite, after);
            pending = false;
        };

        submit(GetSceneTableBuffer(), m_Impl->PendingSceneTableUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetInstanceStaticBuffer(), m_Impl->PendingInstanceStaticUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetInstanceDynamicBuffer(), m_Impl->PendingInstanceDynamicUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetEntityConfigBuffer(), m_Impl->PendingEntityConfigUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetGeometryRecordBuffer(), m_Impl->PendingGeometryRecordUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetBoundsBuffer(), m_Impl->PendingBoundsUploadBarrier, RHI::MemoryAccess::ShaderRead);
        submit(GetLightBuffer(), m_Impl->PendingLightsUploadBarrier, RHI::MemoryAccess::ShaderRead);
        m_Impl->PendingManagedVertexUploadBarrier =
            m_Impl->PendingManagedVertexUploadBarrier ||
            m_Impl->PendingManagedVertexChannelUploadBarriers.Any();
        submit(GetManagedVertexBuffer(), m_Impl->PendingManagedVertexUploadBarrier, RHI::MemoryAccess::ShaderRead);
        m_Impl->PendingManagedVertexChannelUploadBarriers = {};
        submit(GetManagedIndexBuffer(),
               m_Impl->PendingManagedIndexUploadBarrier,
               RHI::MemoryAccess::IndexRead | RHI::MemoryAccess::ShaderRead);
    }

    RHI::BufferHandle GpuWorld::GetSceneTableBuffer() const noexcept { return m_Impl->SceneTableLease.GetHandle(); }
    std::uint64_t GpuWorld::GetSceneTableBDA() const noexcept
    {
        return m_Impl->Device ? m_Impl->Device->GetBufferDeviceAddress(GetSceneTableBuffer()) : 0;
    }

    RHI::BufferHandle GpuWorld::GetInstanceStaticBuffer() const noexcept { return m_Impl->InstanceStaticLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetInstanceDynamicBuffer() const noexcept { return m_Impl->InstanceDynamicLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetEntityConfigBuffer() const noexcept { return m_Impl->EntityConfigLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetGeometryRecordBuffer() const noexcept { return m_Impl->GeometryRecordLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetBoundsBuffer() const noexcept { return m_Impl->BoundsLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetLightBuffer() const noexcept { return m_Impl->LightLease.GetHandle(); }

    RHI::BufferHandle GpuWorld::GetManagedVertexBuffer() const noexcept { return m_Impl->ManagedVertexLease.GetHandle(); }
    RHI::BufferHandle GpuWorld::GetManagedIndexBuffer() const noexcept { return m_Impl->ManagedIndexLease.GetHandle(); }
    bool GpuWorld::TryGetGeometryRecord(
        const GpuGeometryHandle geometry,
        RHI::GpuGeometryRecord& outRecord) const noexcept
    {
        outRecord = {};
        if (!m_Impl->GeometrySlots.Resolve(geometry) ||
            geometry.Index >= m_Impl->GeometryRecordsCpu.size())
        {
            return false;
        }
        outRecord = m_Impl->GeometryRecordsCpu[geometry.Index];
        return true;
    }

    bool GpuWorld::TryGetGeometryResidencyView(
        const GpuGeometryHandle geometry,
        GpuGeometryResidencyView& outView) const noexcept
    {
        outView = {};
        if (!m_Impl->GeometrySlots.Resolve(geometry) ||
            geometry.Index >= m_Impl->GeometryRecordsCpu.size() ||
            geometry.Index >= m_Impl->GeometryAllocations.size())
        {
            return false;
        }

        const ManagedGeometryAllocation& allocation =
            m_Impl->GeometryAllocations[geometry.Index];
        if (!allocation.Live ||
            allocation.Generation != geometry.Generation)
        {
            return false;
        }

        outView.Record = m_Impl->GeometryRecordsCpu[geometry.Index];
        outView.IndexBuffer = GetManagedIndexBuffer();
        outView.ContentRevision = allocation.ContentRevision;
        outView.PositionFingerprint = allocation.PositionFingerprint;
        outView.SurfaceIndexFingerprint =
            allocation.SurfaceIndexFingerprint;
        outView.TexcoordFingerprint = allocation.TexcoordFingerprint;
        outView.NormalFingerprint = allocation.NormalFingerprint;
        outView.PositionByteCount = allocation.PositionByteCount;
        outView.SurfaceIndexByteCount =
            allocation.SurfaceIndexByteCount;
        outView.TexcoordByteCount = allocation.TexcoordByteCount;
        outView.NormalByteCount = allocation.NormalByteCount;
        outView.VertexCount = allocation.VertexCount;
        outView.SurfaceIndexCount =
            static_cast<std::uint32_t>(
                allocation.SurfaceIndices.size());
        outView.StorageLane = allocation.StorageLane;
        outView.SurfaceIndexFormat = RHI::Format::R32_UINT;
        outView.SurfaceIndexElementBytes = sizeof(std::uint32_t);
        outView.SurfaceIndexStrideBytes = sizeof(std::uint32_t);

        if (allocation.PositionByteCount != 0u)
        {
            outView.PositionFormat = RHI::Format::RGB32_FLOAT;
            outView.PositionElementBytes = kPositionElementBytes;
            outView.PositionStrideBytes = kPositionElementBytes;
        }
        if (allocation.TexcoordByteCount != 0u)
        {
            outView.TexcoordFormat = RHI::Format::RG32_FLOAT;
            outView.TexcoordElementBytes = kTexcoordElementBytes;
            outView.TexcoordStrideBytes = kTexcoordElementBytes;
        }
        if (allocation.NormalByteCount != 0u)
        {
            outView.NormalFormat = RHI::Format::RGB32_FLOAT;
            outView.NormalElementBytes = kNormalElementBytes;
            outView.NormalStrideBytes = kNormalElementBytes;
        }
        return true;
    }

    std::uint32_t GpuWorld::GetLiveInstanceCount() const noexcept { return m_Impl->InstanceSlots.LiveCount; }
    std::uint32_t GpuWorld::GetInstanceCapacity() const noexcept { return m_Impl->Desc.MaxInstances; }
    std::uint32_t GpuWorld::GetLiveGeometryCount() const noexcept { return m_Impl->GeometrySlots.LiveCount; }
    std::uint32_t GpuWorld::GetGeometryCapacity() const noexcept { return m_Impl->Desc.MaxGeometryRecords; }
    std::uint32_t GpuWorld::GetLightCount() const noexcept { return static_cast<std::uint32_t>(m_Impl->LightsCpu.size()); }
    std::uint32_t GpuWorld::GetLightCapacity() const noexcept { return m_Impl->Desc.MaxLights; }
    GpuWorld::Diagnostics GpuWorld::GetDiagnostics() const noexcept
    {
        return Diagnostics{
            .Instances = m_Impl->InstanceSlots.Diagnostics(),
            .Geometry = m_Impl->GeometrySlots.Diagnostics(),
            .VertexBytesUsed = m_Impl->VertexBumpOffset,
            .VertexBytesCapacity = m_Impl->Desc.VertexBufferBytes,
            .IndexBytesUsed = m_Impl->IndexBumpOffset,
            .IndexBytesCapacity = m_Impl->Desc.IndexBufferBytes,
            .VertexOverflowCount = m_Impl->VertexOverflowCount,
            .IndexOverflowCount = m_Impl->IndexOverflowCount,
            .LightOverflowCount = m_Impl->LightOverflowCount,
            .NullDevice = m_Impl->Device != nullptr && !m_Impl->Device->IsOperational(),
        };
    }

    GpuWorld::ManagedBufferDiagnostics GpuWorld::GetManagedBufferDiagnostics() const noexcept
    {
        return ManagedBufferDiagnostics{
            .Vertex = m_Impl->VertexFragmentation(),
            .Index = m_Impl->IndexFragmentation(),
            .CompactionBytesMoved = m_Impl->ManagedCompactionBytesMoved,
            .CompactionCount = m_Impl->ManagedCompactionCount,
            .StaleRelocationCount = m_Impl->StaleCompactionRelocationCount,
        };
    }
}
