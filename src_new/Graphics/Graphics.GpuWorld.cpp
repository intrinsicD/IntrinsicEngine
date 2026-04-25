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

        template <typename Tag>
        struct SlotAllocator
        {
            using Handle = Core::StrongHandle<Tag>;

            std::vector<SlotMeta> Meta;
            std::vector<std::uint32_t> FreeList;
            std::uint32_t NextFresh = 0;
            std::uint32_t LiveCount = 0;

            void Reset(std::uint32_t capacity)
            {
                Meta.assign(capacity, {});
                FreeList.clear();
                NextFresh = 0;
                LiveCount = 0;
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

            void Free(Handle h)
            {
                if (!Resolve(h))
                {
                    return;
                }

                auto& meta = Meta[h.Index];
                meta.Live = false;
                ++meta.Generation;
                FreeList.push_back(h.Index);
                if (LiveCount > 0)
                {
                    --LiveCount;
                }
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
        RHI::GpuSceneTable                   SceneTableCpu{};

        std::vector<bool> DirtyInstanceStatic;
        std::vector<bool> DirtyInstanceDynamic;
        std::vector<bool> DirtyEntityConfig;
        std::vector<bool> DirtyGeometryRecord;
        std::vector<bool> DirtyBounds;
        bool DirtyLights = false;
        bool DirtySceneTable = true;

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
        m_Impl->BoundsCpu.clear();
        m_Impl->LightsCpu.clear();
        m_Impl->DirtyInstanceStatic.clear();
        m_Impl->DirtyInstanceDynamic.clear();
        m_Impl->DirtyEntityConfig.clear();
        m_Impl->DirtyGeometryRecord.clear();
        m_Impl->DirtyBounds.clear();

        m_Impl->VertexBumpOffset = 0;
        m_Impl->IndexBumpOffset = 0;
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
        if (!m_Impl->InstanceSlots.Resolve(instance))
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

        m_Impl->InstanceSlots.Free(instance);
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
            m_Impl->GeometrySlots.Free(h);
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

        auto& rec = m_Impl->GeometryRecordsCpu[h.Index];
        rec = {};
        rec.VertexBufferBDA = m_Impl->Device->GetBufferDeviceAddress(GetManagedVertexBuffer());
        rec.IndexBufferBDA = m_Impl->Device->GetBufferDeviceAddress(GetManagedIndexBuffer());
        const std::uint32_t vertexStride =
            (desc.VertexCount > 0u) ? static_cast<std::uint32_t>(vbSize / desc.VertexCount) : 0u;
        assert(desc.VertexCount == 0u || (vbSize % desc.VertexCount) == 0u);
        assert(vertexStride == 0u || (vbOffset % vertexStride) == 0u);
        const std::uint32_t vertexOffset =
            (vertexStride > 0u) ? static_cast<std::uint32_t>(vbOffset / vertexStride) : 0u;
        rec.VertexOffset = vertexOffset;
        rec.VertexCount = desc.VertexCount;
        rec.SurfaceFirstIndex = static_cast<std::uint32_t>(surfOffset / sizeof(std::uint32_t));
        rec.SurfaceIndexCount = static_cast<std::uint32_t>(desc.SurfaceIndices.size());
        rec.LineFirstIndex = static_cast<std::uint32_t>(lineOffset / sizeof(std::uint32_t));
        rec.LineIndexCount = static_cast<std::uint32_t>(desc.LineIndices.size());
        rec.PointFirstVertex = vertexOffset;
        rec.PointVertexCount = desc.VertexCount;

        m_Impl->VertexBumpOffset += vbSize;
        m_Impl->IndexBumpOffset = lineOffset + lineSize;

        m_Impl->DirtyGeometryRecord[h.Index] = true;
        return h;
    }

    void GpuWorld::FreeGeometry(GpuGeometryHandle geometry)
    {
        if (!m_Impl->GeometrySlots.Resolve(geometry))
        {
            return;
        }

        m_Impl->GeometryRecordsCpu[geometry.Index] = {};
        m_Impl->DirtyGeometryRecord[geometry.Index] = true;
        m_Impl->GeometrySlots.Free(geometry);
        // TODO: reclaim managed vertex/index ranges via a free-list + compaction pass.
    }

    void GpuWorld::SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry)
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return;
        }
        if (geometry.IsValid() && !m_Impl->GeometrySlots.Resolve(geometry))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].GeometrySlot =
            geometry.IsValid() ? geometry.Index : RHI::GpuInstanceStatic::InvalidGeometrySlot;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    void GpuWorld::SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot)
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].MaterialSlot = materialSlot;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    void GpuWorld::SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags)
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return;
        }

        m_Impl->InstanceStaticCpu[instance.Index].RenderFlags = flags;
        m_Impl->DirtyInstanceStatic[instance.Index] = true;
    }

    void GpuWorld::SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel)
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
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
        if (!m_Impl->InstanceSlots.Resolve(instance))
        {
            return;
        }

        m_Impl->EntityConfigCpu[instance.Index] = config;
        m_Impl->DirtyEntityConfig[instance.Index] = true;
    }

    void GpuWorld::SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds)
    {
        if (!m_Impl->InstanceSlots.Resolve(instance))
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
        m_Impl->LightsCpu.assign(lights.begin(), lights.begin() + capped);
        m_Impl->DirtyLights = true;
        m_Impl->RefreshSceneTable();
    }

    void GpuWorld::SyncFrame()
    {
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
}
