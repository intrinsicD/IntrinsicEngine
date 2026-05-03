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

import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kInvalidSlot = 0xFFFF'FFFFu;

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
            std::uint64_t IndexByteOffset = 0;
            std::uint64_t SurfaceIndexByteCount = 0;
            std::uint64_t LineIndexByteCount = 0;
            std::uint32_t VertexCount = 0;
            std::uint32_t VertexStride = 0;
            std::vector<std::byte> VertexBytes;
            std::vector<std::uint32_t> SurfaceIndices;
            std::vector<std::uint32_t> LineIndices;

            [[nodiscard]] std::uint64_t IndexByteCount() const noexcept
            {
                return SurfaceIndexByteCount + LineIndexByteCount;
            }
        };

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
        void FlushDirtyRuns(RHI::IDevice& device,
                            RHI::BufferHandle dst,
                            std::vector<T>& cpu,
                            std::vector<bool>& dirty)
        {
            if (!dst.IsValid() || cpu.empty() || cpu.size() != dirty.size())
            {
                return;
            }

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
            }
        }

        [[nodiscard]] float FragmentationRatio(std::uint64_t fragmented, std::uint64_t usedHighWater) noexcept
        {
            if (usedHighWater == 0)
            {
                return 0.0f;
            }
            return static_cast<float>(static_cast<double>(fragmented) / static_cast<double>(usedHighWater));
        }
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

        std::vector<bool> DirtyInstanceStatic;
        std::vector<bool> DirtyInstanceDynamic;
        std::vector<bool> DirtyEntityConfig;
        std::vector<bool> DirtyGeometryRecord;
        std::vector<bool> DirtyBounds;
        bool DirtyLights = false;
        bool DirtySceneTable = true;
        std::uint64_t FrameIndex = 0;
        std::uint32_t VertexOverflowCount = 0;
        std::uint32_t IndexOverflowCount = 0;
        std::uint32_t LightOverflowCount = 0;
        std::uint64_t ManagedCompactionBytesMoved = 0;
        std::uint32_t ManagedCompactionCount = 0;
        std::uint32_t StaleCompactionRelocationCount = 0;

        std::uint64_t VertexBumpOffset = 0;
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
            SceneTableCpu.InstanceCapacity   = Desc.MaxInstances;
            SceneTableCpu.GeometryCapacity   = Desc.MaxGeometryRecords;
            SceneTableCpu.MaterialCapacity   = MaterialCapacity;
            SceneTableCpu.LightCount         = static_cast<std::uint32_t>(LightsCpu.size());
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
            return allocation.VertexStride > 0u
                ? static_cast<std::uint32_t>(allocation.VertexByteOffset / allocation.VertexStride)
                : 0u;
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
            rec.VertexBufferBDA = Device ? Device->GetBufferDeviceAddress(ManagedVertexLease.GetHandle()) : 0;
            rec.IndexBufferBDA = Device ? Device->GetBufferDeviceAddress(ManagedIndexLease.GetHandle()) : 0;
            const std::uint32_t vertexOffset = VertexOffsetUnits(allocation);
            rec.VertexOffset = vertexOffset;
            rec.VertexCount = allocation.VertexCount;
            rec.SurfaceFirstIndex = SurfaceFirstIndex(allocation);
            rec.SurfaceIndexCount = static_cast<std::uint32_t>(allocation.SurfaceIndices.size());
            rec.LineFirstIndex = LineFirstIndex(allocation);
            rec.LineIndexCount = static_cast<std::uint32_t>(allocation.LineIndices.size());
            rec.PointFirstVertex = vertexOffset;
            rec.PointVertexCount = allocation.VertexCount;
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
            }
            if (!allocation.SurfaceIndices.empty())
            {
                Device->WriteBuffer(ManagedIndexLease.GetHandle(),
                                    allocation.SurfaceIndices.data(),
                                    allocation.SurfaceIndexByteCount,
                                    allocation.IndexByteOffset);
            }
            if (!allocation.LineIndices.empty())
            {
                Device->WriteBuffer(ManagedIndexLease.GetHandle(),
                                    allocation.LineIndices.data(),
                                    allocation.LineIndexByteCount,
                                    allocation.IndexByteOffset + allocation.SurfaceIndexByteCount);
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

        if (device.IsOperational())
        {
            if (!m_Impl->AllocateBuffer(m_Impl->InstanceStaticLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxInstances) * sizeof(RHI::GpuInstanceStatic),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.InstanceStatic",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->InstanceDynamicLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxInstances) * sizeof(RHI::GpuInstanceDynamic),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.InstanceDynamic",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->EntityConfigLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxInstances) * sizeof(RHI::GpuEntityConfig),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.EntityConfig",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->GeometryRecordLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxGeometryRecords) * sizeof(RHI::GpuGeometryRecord),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.GeometryRecords",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->BoundsLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxInstances) * sizeof(RHI::GpuBounds),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.Bounds",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->LightLease, {
                    .SizeBytes = static_cast<std::uint64_t>(desc.MaxLights) * sizeof(RHI::GpuLight),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.Lights",
                })) return false;
            if (!m_Impl->AllocateBuffer(m_Impl->SceneTableLease, {
                    .SizeBytes = sizeof(RHI::GpuSceneTable),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.SceneTable",
                })) return false;
            // TODO: extend to multiple managed buffers and background compaction.
            if (!m_Impl->AllocateBuffer(m_Impl->ManagedVertexLease, {
                    .SizeBytes = desc.VertexBufferBytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.ManagedVertexBuffer0",
                })) return false;
            // TODO: extend to multiple managed buffers and background compaction.
            if (!m_Impl->AllocateBuffer(m_Impl->ManagedIndexLease, {
                    .SizeBytes = desc.IndexBufferBytes,
                    .Usage = RHI::BufferUsage::Index | RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName = "GpuWorld.ManagedIndexBuffer0",
                })) return false;
        }

        m_Impl->Initialized = true;
        m_Impl->RefreshSceneTable();
        return true;
    }

    bool GpuWorld::Initialize(RHI::IDevice& device, RHI::BufferManager& buffers)
    {
        return Initialize(device, buffers, InitDesc{});
    }

    void GpuWorld::Shutdown()
    {
        if (!m_Impl->Initialized)
        {
            return;
        }

        m_Impl->InstanceStaticLease = {};
        m_Impl->InstanceDynamicLease = {};
        m_Impl->EntityConfigLease = {};
        m_Impl->GeometryRecordLease = {};
        m_Impl->BoundsLease = {};
        m_Impl->LightLease = {};
        m_Impl->SceneTableLease = {};
        m_Impl->ManagedVertexLease = {};
        m_Impl->ManagedIndexLease = {};

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
        m_Impl->IndexBumpOffset = 0;
        m_Impl->FrameIndex = 0;
        m_Impl->VertexOverflowCount = 0;
        m_Impl->IndexOverflowCount = 0;
        m_Impl->LightOverflowCount = 0;
        m_Impl->ManagedCompactionBytesMoved = 0;
        m_Impl->ManagedCompactionCount = 0;
        m_Impl->StaleCompactionRelocationCount = 0;
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

        const std::uint64_t vbSize = desc.PackedVertexBytes.size_bytes();
        const std::uint64_t surfSize = desc.SurfaceIndices.size_bytes();
        const std::uint64_t lineSize = desc.LineIndices.size_bytes();

        const std::uint64_t vbOffset = m_Impl->VertexBumpOffset;
        const std::uint64_t surfOffset = m_Impl->IndexBumpOffset;
        const std::uint64_t lineOffset = surfOffset + surfSize;

        if (vbOffset + vbSize > m_Impl->Desc.VertexBufferBytes ||
            lineOffset + lineSize > m_Impl->Desc.IndexBufferBytes)
        {
            if (vbOffset + vbSize > m_Impl->Desc.VertexBufferBytes)
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
            if (vbSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedVertexBuffer(),
                                            desc.PackedVertexBytes.data(),
                                            vbSize,
                                            vbOffset);
            }
            if (surfSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedIndexBuffer(),
                                            desc.SurfaceIndices.data(),
                                            surfSize,
                                            surfOffset);
            }
            if (lineSize > 0)
            {
                m_Impl->Device->WriteBuffer(GetManagedIndexBuffer(),
                                            desc.LineIndices.data(),
                                            lineSize,
                                            lineOffset);
            }
        }

        const std::uint32_t vertexStride =
            (desc.VertexCount > 0u) ? static_cast<std::uint32_t>(vbSize / desc.VertexCount) : 0u;
        assert(desc.VertexCount == 0u || (vbSize % desc.VertexCount) == 0u);
        assert(vertexStride == 0u || (vbOffset % vertexStride) == 0u);

        auto& allocation = m_Impl->GeometryAllocations[h.Index];
        allocation = {};
        allocation.Live = true;
        allocation.Generation = h.Generation;
        allocation.VertexByteOffset = vbOffset;
        allocation.VertexByteCount = vbSize;
        allocation.IndexByteOffset = surfOffset;
        allocation.SurfaceIndexByteCount = surfSize;
        allocation.LineIndexByteCount = lineSize;
        allocation.VertexCount = desc.VertexCount;
        allocation.VertexStride = vertexStride;
        allocation.VertexBytes.assign(desc.PackedVertexBytes.begin(), desc.PackedVertexBytes.end());
        allocation.SurfaceIndices.assign(desc.SurfaceIndices.begin(), desc.SurfaceIndices.end());
        allocation.LineIndices.assign(desc.LineIndices.begin(), desc.LineIndices.end());

        m_Impl->RewriteGeometryRecord(h.Index);

        m_Impl->VertexBumpOffset += vbSize;
        m_Impl->IndexBumpOffset = lineOffset + lineSize;
        return h;
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
            const std::uint64_t newVertexOffset = nextVertexOffset;
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
                    .OldVertexOffset = allocation.VertexStride > 0u
                        ? static_cast<std::uint32_t>(oldVertexOffset / allocation.VertexStride)
                        : 0u,
                    .NewVertexOffset = allocation.VertexStride > 0u
                        ? static_cast<std::uint32_t>(newVertexOffset / allocation.VertexStride)
                        : 0u,
                    .OldSurfaceFirstIndex = static_cast<std::uint32_t>(oldIndexOffset / sizeof(std::uint32_t)),
                    .NewSurfaceFirstIndex = static_cast<std::uint32_t>(newIndexOffset / sizeof(std::uint32_t)),
                    .OldLineFirstIndex = static_cast<std::uint32_t>(oldLineOffset / sizeof(std::uint32_t)),
                    .NewLineFirstIndex = static_cast<std::uint32_t>(newLineOffset / sizeof(std::uint32_t)),
                });
            }

            nextVertexOffset += allocation.VertexByteCount;
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

        FlushDirtyRuns(*m_Impl->Device,
                       GetInstanceStaticBuffer(),
                       m_Impl->InstanceStaticCpu,
                       m_Impl->DirtyInstanceStatic);
        FlushDirtyRuns(*m_Impl->Device,
                       GetInstanceDynamicBuffer(),
                       m_Impl->InstanceDynamicCpu,
                       m_Impl->DirtyInstanceDynamic);
        FlushDirtyRuns(*m_Impl->Device,
                       GetEntityConfigBuffer(),
                       m_Impl->EntityConfigCpu,
                       m_Impl->DirtyEntityConfig);
        FlushDirtyRuns(*m_Impl->Device,
                       GetGeometryRecordBuffer(),
                       m_Impl->GeometryRecordsCpu,
                       m_Impl->DirtyGeometryRecord);
        FlushDirtyRuns(*m_Impl->Device,
                       GetBoundsBuffer(),
                       m_Impl->BoundsCpu,
                       m_Impl->DirtyBounds);

        if (m_Impl->DirtyLights && GetLightBuffer().IsValid())
        {
            if (!m_Impl->LightsCpu.empty())
            {
                m_Impl->Device->WriteBuffer(GetLightBuffer(),
                                            m_Impl->LightsCpu.data(),
                                            static_cast<std::uint64_t>(m_Impl->LightsCpu.size() * sizeof(RHI::GpuLight)),
                                            0);
            }
            m_Impl->DirtyLights = false;
        }

        if (m_Impl->DirtySceneTable && GetSceneTableBuffer().IsValid())
        {
            m_Impl->Device->WriteBuffer(GetSceneTableBuffer(),
                                        &m_Impl->SceneTableCpu,
                                        sizeof(RHI::GpuSceneTable),
                                        0);
            m_Impl->DirtySceneTable = false;
        }
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

    std::uint32_t GpuWorld::GetLiveInstanceCount() const noexcept { return m_Impl->InstanceSlots.LiveCount; }
    std::uint32_t GpuWorld::GetInstanceCapacity() const noexcept { return m_Impl->Desc.MaxInstances; }
    std::uint32_t GpuWorld::GetLiveGeometryCount() const noexcept { return m_Impl->GeometrySlots.LiveCount; }
    std::uint32_t GpuWorld::GetGeometryCapacity() const noexcept { return m_Impl->Desc.MaxGeometryRecords; }
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
