module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.MaterialSystem;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Asset.Registry;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;

// ============================================================
// MaterialSystem — implementation
// ============================================================
//
// Data model:
//
//   m_Types[]          — registered MaterialTypeDesc + assigned TypeID
//   m_Slots[]          — per-instance GpuMaterialSlot (CPU mirror)
//   m_InstanceMeta[]   — per-slot: refcount, generation, dirty flag, params
//   m_FreeList[]        — recycled slot indices
//   m_DirtySet[]        — set of slots needing GPU upload this frame
//   m_Buffer            — BufferManager lease for the GPU SSBO
//   m_GpuCapacity       — max slots the current SSBO can hold
//
// Slot 0 is always the default material and is never freed.
//
// GPU upload:
//   SyncGpuBuffer() calls IDevice::WriteBuffer() for each dirty
//   slot range (ideally contiguous runs for fewer API calls).
//   In a future iteration this could use IDevice::UploadBuffer()
//   (async staging) — the interface supports it.
// ============================================================

namespace Extrinsic::Graphics
{
    // -----------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------
    static constexpr std::uint32_t kInitialCapacity     = 256;
    static constexpr std::uint32_t kGrowthFactor        = 2;

    [[nodiscard]] RHI::BindlessIndex* SelectTextureSlot(
        MaterialParams& params,
        MaterialTextureSemantic semantic) noexcept
    {
        switch (semantic)
        {
        case MaterialTextureSemantic::Albedo:            return &params.AlbedoID;
        case MaterialTextureSemantic::Normal:            return &params.NormalID;
        case MaterialTextureSemantic::MetallicRoughness: return &params.MetallicRoughnessID;
        case MaterialTextureSemantic::Emissive:          return &params.EmissiveID;
        }
        return nullptr;
    }

    // -----------------------------------------------------------------
    // Registered type record
    // -----------------------------------------------------------------
    struct TypeRecord
    {
        MaterialTypeDesc Desc;
        std::uint32_t    TypeID = 0; // monotonically assigned
    };

    // -----------------------------------------------------------------
    // Per-instance metadata (not uploaded to GPU)
    // -----------------------------------------------------------------
    struct InstanceMeta
    {
        MaterialParams             Params{};
        MaterialTypeHandle         Type{};
        std::atomic<std::uint32_t> RefCount{0};
        std::uint32_t              Generation = 0;
        bool                       Live       = false;
        bool                       Dirty      = false;

        InstanceMeta() = default;
        InstanceMeta(const InstanceMeta&) = delete;
        InstanceMeta& operator=(const InstanceMeta&) = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct MaterialSystem::Impl
    {
        // Device + buffer manager (borrowed, not owned)
        RHI::IDevice*        Device      = nullptr;
        RHI::BufferManager*  BufferMgr   = nullptr;

        // GPU SSBO lease
        RHI::BufferManager::BufferLease Buffer{};
        std::uint32_t                   GpuCapacity = 0;

        // Type registry
        std::vector<TypeRecord>                       Types;
        std::unordered_map<std::string, std::uint32_t> TypeNameIndex; // name → Types[] index

        // Instance pool — deque for stable addresses (atomic member)
        std::deque<InstanceMeta>        Meta;  // parallel to GpuSlots
        std::vector<RHI::GpuMaterialSlot> GpuSlots; // CPU mirror of SSBO
        std::vector<std::uint32_t>       FreeList;
        std::vector<bool>                DirtySet;
        MaterialSystemDiagnostics        Diagnostics{};

        // -----------------------------------------------------------------
        [[nodiscard]] InstanceMeta* Resolve(MaterialHandle h) noexcept
        {
            if (!h.IsValid() || h.Index >= static_cast<std::uint32_t>(Meta.size()))
                return nullptr;
            InstanceMeta& m = Meta[h.Index];
            if (!m.Live || m.Generation != h.Generation) return nullptr;
            return &m;
        }

        [[nodiscard]] const InstanceMeta* Resolve(MaterialHandle h) const noexcept
        {
            if (!h.IsValid() || h.Index >= static_cast<std::uint32_t>(Meta.size()))
                return nullptr;
            const InstanceMeta& m = Meta[h.Index];
            if (!m.Live || m.Generation != h.Generation) return nullptr;
            return &m;
        }

        // Fill a GpuMaterialSlot from params + typeID
        static void PackSlot(RHI::GpuMaterialSlot& slot,
                             const MaterialParams&  p,
                             std::uint32_t          typeID) noexcept
        {
            slot.BaseColorFactor       = p.BaseColorFactor;
            slot.MetallicFactor        = p.MetallicFactor;
            slot.RoughnessFactor       = p.RoughnessFactor;
            slot.AlbedoID              = p.AlbedoID;
            slot.NormalID              = p.NormalID;
            slot.MetallicRoughnessID   = p.MetallicRoughnessID;
            slot.EmissiveID            = p.EmissiveID;
            slot.MaterialTypeID        = typeID;
            slot.Flags                 = static_cast<std::uint32_t>(p.Flags);
            for (int i = 0; i < 4; ++i)
                slot.CustomData[i] = p.CustomData[i];
        }

        // Allocate (or grow) the GPU SSBO to hold at least `needed` slots.
        bool EnsureCapacity(std::uint32_t needed)
        {
            if (needed <= GpuCapacity) return true;

            const std::uint32_t newCap =
                std::max(needed, GpuCapacity * kGrowthFactor);
            if (!Device || !BufferMgr)
            {
                return false;
            }
            if (!Device->IsOperational())
            {
                // Non-operational backends cannot allocate the material SSBO yet.
                // Keep CPU-side slot mirrors/capacity coherent so extraction and
                // default CPU tests can proceed; GRAPHICS-018R owns rebuilding
                // this GPU buffer when a Vulkan device later becomes operational.
                GpuCapacity = newCap;
                GpuSlots.resize(newCap, RHI::GpuMaterialSlot{});
                DirtySet.assign(newCap, false);
                return true;
            }

            const std::uint64_t newSizeBytes =
                newCap * static_cast<std::uint64_t>(sizeof(RHI::GpuMaterialSlot));

            RHI::BufferDesc desc{
                .SizeBytes  = newSizeBytes,
                .Usage      = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName  = "MaterialSSBO",
            };

            auto newBufferOr = BufferMgr->Create(desc);
            if (!newBufferOr.has_value()) return false;
            auto newBuffer = std::move(*newBufferOr);

            // If we had a previous buffer, upload the existing slots first
            // so the new SSBO has valid data from slot 0 upward.
            if (!GpuSlots.empty())
            {
                Device->WriteBuffer(
                    newBuffer.GetHandle(),
                    GpuSlots.data(),
                    GpuSlots.size() * sizeof(RHI::GpuMaterialSlot),
                    0);
            }

            Buffer     = std::move(newBuffer);
            GpuCapacity = newCap;
            GpuSlots .resize(newCap, RHI::GpuMaterialSlot{});
            DirtySet .assign(newCap, false);
            return true;
        }

        bool RecreateGpuBuffer(std::uint32_t capacity)
        {
            if (!Device || !BufferMgr || !Device->IsOperational())
            {
                return false;
            }

            capacity = std::max(capacity, kInitialCapacity);
            const std::uint64_t newSizeBytes =
                capacity * static_cast<std::uint64_t>(sizeof(RHI::GpuMaterialSlot));

            auto newBufferOr = BufferMgr->Create({
                .SizeBytes = newSizeBytes,
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName = "MaterialSSBO",
            });
            if (!newBufferOr.has_value())
            {
                return false;
            }

            if (GpuSlots.size() < capacity)
            {
                GpuSlots.resize(capacity, RHI::GpuMaterialSlot{});
            }

            auto newBuffer = std::move(*newBufferOr);
            Device->WriteBuffer(newBuffer.GetHandle(),
                                GpuSlots.data(),
                                static_cast<std::uint64_t>(GpuSlots.size() * sizeof(RHI::GpuMaterialSlot)),
                                0);
            Buffer = std::move(newBuffer);
            GpuCapacity = capacity;
            DirtySet.assign(capacity, false);
            Diagnostics.LastUploadRangeCount = GpuSlots.empty() ? 0u : 1u;
            Diagnostics.LastUploadedSlotCount = static_cast<std::uint32_t>(GpuSlots.size());
            return true;
        }
    };

    // -----------------------------------------------------------------
    // MaterialSystem
    // -----------------------------------------------------------------
    MaterialSystem::MaterialSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    MaterialSystem::~MaterialSystem() = default;

    // -----------------------------------------------------------------
    void MaterialSystem::Initialize(RHI::IDevice& device, RHI::BufferManager& bufferMgr)
    {
        m_Impl->Device    = &device;
        m_Impl->BufferMgr = &bufferMgr;

        // Allocate the initial SSBO.
        [[maybe_unused]] bool ok = m_Impl->EnsureCapacity(kInitialCapacity);
        assert(ok && "MaterialSystem: initial SSBO allocation failed");

        // Register the built-in type triplet in a fixed order so the
        // well-known TypeIDs (StandardPBR=0, SciVis=1, DefaultDebugSurface=2)
        // are reserved before any subsystem-specific registration runs.
        const auto registerBuiltIn = [&](std::string_view name,
                                         std::vector<CustomParamDesc> customParams,
                                         std::uint32_t expectedTypeID)
        {
            TypeRecord rec;
            rec.TypeID = static_cast<std::uint32_t>(m_Impl->Types.size());
            rec.Desc   = {std::string{name}, std::move(customParams)};
            assert(rec.TypeID == expectedTypeID && "Built-in MaterialType registration order changed");
            m_Impl->TypeNameIndex[rec.Desc.Name] = rec.TypeID;
            m_Impl->Types.push_back(std::move(rec));
        };

        registerBuiltIn(kMaterialTypeName_StandardPBR, {}, kMaterialTypeID_StandardPBR);
        registerBuiltIn(kMaterialTypeName_SciVis,
            {
                {"ColormapAndDomain",  "colourmap bindless idx, domain, rangeMin, rangeMax"},
                {"IsolinesAndBins",    "isolineCount, packedColor, isolineWidth, binCount"},
                {"ScalarBDA",          "BDA lo/hi, elementCount, colorSourceMode"},
                {"Reserved",           "reserved for future use"},
            },
            kMaterialTypeID_SciVis);
        registerBuiltIn(kMaterialTypeName_DefaultDebugSurface, {}, kMaterialTypeID_DefaultDebugSurface);

        // Reserve slot 0 for the default material without going through
        // the normal Create path so its index is always 0. Slot 0 carries
        // the DefaultDebugSurface params (purple unlit) so any invalid
        // MaterialHandle resolves to a visible missing-material surface.
        MaterialParams defaultParams{};
        defaultParams.BaseColorFactor = glm::vec4{
            kDefaultDebugSurfaceBaseColor[0],
            kDefaultDebugSurfaceBaseColor[1],
            kDefaultDebugSurfaceBaseColor[2],
            kDefaultDebugSurfaceBaseColor[3],
        };
        defaultParams.Flags = MaterialFlags::Unlit;

        m_Impl->Meta.emplace_back();
        InstanceMeta& defaultMeta    = m_Impl->Meta.back();
        defaultMeta.Generation       = 0;
        defaultMeta.Live             = true;
        defaultMeta.RefCount.store(1, std::memory_order_relaxed); // never freed
        defaultMeta.Params           = defaultParams;
        defaultMeta.Type             = MaterialTypeHandle{kMaterialTypeID_DefaultDebugSurface, 0};

        Impl::PackSlot(m_Impl->GpuSlots[0], defaultParams, kMaterialTypeID_DefaultDebugSurface);
        m_Impl->DirtySet[0] = true;
    }

    bool MaterialSystem::RebuildGpuResources(RHI::IDevice& device, RHI::BufferManager& bufferMgr)
    {
        m_Impl->Device = &device;
        m_Impl->BufferMgr = &bufferMgr;
        const std::uint32_t capacity = std::max<std::uint32_t>(
            m_Impl->GpuCapacity,
            static_cast<std::uint32_t>(m_Impl->GpuSlots.size()));
        return m_Impl->RecreateGpuBuffer(capacity);
    }

    // -----------------------------------------------------------------
    void MaterialSystem::Shutdown()
    {
        m_Impl->Buffer = {};   // releases the SSBO lease → BufferManager destroys it
        m_Impl->Meta.clear();
        m_Impl->GpuSlots.clear();
        m_Impl->FreeList.clear();
        m_Impl->DirtySet.clear();
        m_Impl->Types.clear();
        m_Impl->TypeNameIndex.clear();
        m_Impl->GpuCapacity = 0;
        m_Impl->Device      = nullptr;
        m_Impl->BufferMgr   = nullptr;
    }

    // -----------------------------------------------------------------
    MaterialTypeHandle MaterialSystem::RegisterType(const MaterialTypeDesc& desc)
    {
        assert(m_Impl->Device && "RegisterType called before Initialize()");

        if (m_Impl->TypeNameIndex.contains(desc.Name))
        {
            ++m_Impl->Diagnostics.DuplicateTypeNameCount;
            return {}; // duplicate name
        }

        if (desc.Name.empty() || desc.CustomParams.size() > kMaterialCustomDataSlotCount)
        {
            ++m_Impl->Diagnostics.IncompatibleLayoutCount;
            return {};
        }

        const std::uint32_t typeID = static_cast<std::uint32_t>(m_Impl->Types.size());
        const std::uint32_t index  = typeID; // same as typeID for our simple vector

        TypeRecord rec;
        rec.TypeID = typeID;
        rec.Desc   = desc;
        m_Impl->Types.push_back(std::move(rec));
        m_Impl->TypeNameIndex[desc.Name] = index;

        return MaterialTypeHandle{index, 0}; // generation 0 — types are permanent
    }

    // -----------------------------------------------------------------
    MaterialTypeHandle MaterialSystem::FindType(std::string_view name) const noexcept
    {
        auto it = m_Impl->TypeNameIndex.find(std::string{name});
        if (it == m_Impl->TypeNameIndex.end()) return {};
        return MaterialTypeHandle{it->second, 0};
    }

    // -----------------------------------------------------------------
    const MaterialTypeDesc* MaterialSystem::GetTypeDesc(MaterialTypeHandle type) const noexcept
    {
        if (!type.IsValid() || type.Index >= m_Impl->Types.size()) return nullptr;
        return &m_Impl->Types[type.Index].Desc;
    }

    // -----------------------------------------------------------------
    MaterialSystem::MaterialLease MaterialSystem::CreateInstance(
        MaterialTypeHandle    type,
        const MaterialParams& params)
    {
        assert(m_Impl->Device && "CreateInstance called before Initialize()");

        if (!type.IsValid() || type.Index >= m_Impl->Types.size())
        {
            ++m_Impl->Diagnostics.InvalidCreateTypeCount;
            return {};
        }

        const std::uint32_t typeID = m_Impl->Types[type.Index].TypeID;

        // Allocate a slot index
        std::uint32_t index;
        std::uint32_t generation;

        if (!m_Impl->FreeList.empty())
        {
            index      = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
            generation = m_Impl->Meta[index].Generation;
        }
        else
        {
            index      = static_cast<std::uint32_t>(m_Impl->Meta.size());
            generation = 0;
            m_Impl->Meta.emplace_back();

            // Grow GPU-side arrays if needed (keeps them parallel to Meta).
            if (!m_Impl->EnsureCapacity(index + 1))
            {
                ++m_Impl->Diagnostics.CapacityFailureCount;
                return {};
            }
        }

        InstanceMeta& meta  = m_Impl->Meta[index];
        meta.Params          = params;
        meta.Type            = type;
        meta.Generation      = generation;
        meta.Live            = true;
        meta.Dirty           = true;
        meta.RefCount.store(1, std::memory_order_relaxed);

        Impl::PackSlot(m_Impl->GpuSlots[index], params, typeID);
        m_Impl->DirtySet[index] = true;

        MaterialHandle handle{index, generation};
        return MaterialLease::Adopt(*this, handle);
    }

    // -----------------------------------------------------------------
    void MaterialSystem::SetParams(MaterialHandle handle, const MaterialParams& params)
    {
        InstanceMeta* meta = m_Impl->Resolve(handle);
        if (!meta) return;

        meta->Params = params;
        meta->Dirty  = true;

        const std::uint32_t typeID =
            (meta->Type.IsValid() && meta->Type.Index < m_Impl->Types.size())
            ? m_Impl->Types[meta->Type.Index].TypeID
            : 0u;

        Impl::PackSlot(m_Impl->GpuSlots[handle.Index], params, typeID);
        m_Impl->DirtySet[handle.Index] = true;
    }

    // -----------------------------------------------------------------
    MaterialParams MaterialSystem::GetParams(MaterialHandle handle) const noexcept
    {
        const InstanceMeta* meta = m_Impl->Resolve(handle);
        return meta ? meta->Params : MaterialParams{};
    }

    // -----------------------------------------------------------------
    Core::Result MaterialSystem::ResolveTextureAssetBindings(
        MaterialHandle handle,
        const MaterialTextureAssetBindings& bindings,
        GpuAssetCache& assets)
    {
        InstanceMeta* meta = m_Impl->Resolve(handle);
        if (!meta)
        {
            ++m_Impl->Diagnostics.InvalidTextureAssetBindingCount;
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        struct Binding
        {
            Assets::AssetId Id{};
            MaterialTextureSemantic Semantic = MaterialTextureSemantic::Albedo;
        };

        const Binding requested[] = {
            Binding{.Id = bindings.Albedo, .Semantic = MaterialTextureSemantic::Albedo},
            Binding{.Id = bindings.Normal, .Semantic = MaterialTextureSemantic::Normal},
            Binding{.Id = bindings.MetallicRoughness, .Semantic = MaterialTextureSemantic::MetallicRoughness},
            Binding{.Id = bindings.Emissive, .Semantic = MaterialTextureSemantic::Emissive},
        };

        MaterialParams params = meta->Params;
        bool anyFailure = false;

        for (const Binding& binding : requested)
        {
            if (!binding.Id.IsValid())
                continue;

            RHI::BindlessIndex* target = SelectTextureSlot(params, binding.Semantic);
            if (target == nullptr)
            {
                anyFailure = true;
                ++m_Impl->Diagnostics.TextureAssetResolveFailureCount;
                continue;
            }

            ++m_Impl->Diagnostics.TextureAssetResolveCount;
            auto resolved = assets.GetViewOrFallback(binding.Id);
            if (!resolved.has_value() ||
                resolved->View.Kind != GpuAssetKind::Texture ||
                resolved->View.BindlessIdx == RHI::kInvalidBindlessIndex)
            {
                anyFailure = true;
                *target = RHI::kInvalidBindlessIndex;
                ++m_Impl->Diagnostics.TextureAssetResolveFailureCount;
                continue;
            }

            if (resolved->UsedFallback)
                ++m_Impl->Diagnostics.TextureAssetFallbackResolveCount;

            *target = resolved->View.BindlessIdx;
        }

        SetParams(handle, params);
        return anyFailure
            ? Core::Err(Core::ErrorCode::ResourceNotFound)
            : Core::Ok();
    }

    // -----------------------------------------------------------------
    void MaterialSystem::Retain(MaterialHandle handle)
    {
        InstanceMeta* meta = m_Impl->Resolve(handle);
        assert(meta && "Retain on invalid handle");
        if (meta) meta->RefCount.fetch_add(1, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    void MaterialSystem::Release(MaterialHandle handle)
    {
        InstanceMeta* meta = m_Impl->Resolve(handle);
        assert(meta && "Release on invalid handle");
        if (!meta) return;

        const std::uint32_t prev =
            meta->RefCount.fetch_sub(1, std::memory_order_acq_rel);
        assert(prev > 0 && "Refcount underflow");

        if (prev == 1)
        {
            // Zero out the GPU slot so freed indices don't render stale data.
            m_Impl->GpuSlots[handle.Index] = RHI::GpuMaterialSlot{};
            m_Impl->DirtySet[handle.Index] = true;

            meta->Live = false;
            meta->Generation++;
            m_Impl->FreeList.push_back(handle.Index);
        }
    }

    // -----------------------------------------------------------------
    MaterialSystem::MaterialLease MaterialSystem::AcquireLease(MaterialHandle handle)
    {
        return MaterialLease::RetainNew(*this, handle);
    }

    // -----------------------------------------------------------------
    void MaterialSystem::SyncGpuBuffer()
    {
        if (!m_Impl->Device || !m_Impl->Buffer.IsValid()) return;

        // Coalesce dirty slots into contiguous upload ranges to minimise
        // the number of WriteBuffer calls.
        const std::uint32_t n = static_cast<std::uint32_t>(m_Impl->GpuSlots.size());
        constexpr std::uint64_t kStride = sizeof(RHI::GpuMaterialSlot);

        std::uint32_t rangeStart = UINT32_MAX;
        m_Impl->Diagnostics.LastUploadRangeCount = 0;
        m_Impl->Diagnostics.LastUploadedSlotCount = 0;

        auto flush = [&](std::uint32_t end)
        {
            if (rangeStart == UINT32_MAX) return;
            const std::uint64_t byteOffset = rangeStart * kStride;
            const std::uint64_t byteSize   = (end - rangeStart) * kStride;
            ++m_Impl->Diagnostics.LastUploadRangeCount;
            m_Impl->Diagnostics.LastUploadedSlotCount += end - rangeStart;
            m_Impl->Device->WriteBuffer(
                m_Impl->Buffer.GetHandle(),
                m_Impl->GpuSlots.data() + rangeStart,
                byteSize,
                byteOffset);
            rangeStart = UINT32_MAX;
        };

        for (std::uint32_t i = 0; i < n; ++i)
        {
            if (m_Impl->DirtySet[i])
            {
                if (rangeStart == UINT32_MAX) rangeStart = i;
                m_Impl->DirtySet[i] = false;
            }
            else
            {
                flush(i);
            }
        }
        flush(n); // trailing run
    }

    // -----------------------------------------------------------------
    RHI::BufferHandle MaterialSystem::GetBuffer() const noexcept
    {
        return m_Impl->Buffer.GetHandle();
    }

    // -----------------------------------------------------------------
    std::uint32_t MaterialSystem::GetMaterialSlot(MaterialHandle handle) const noexcept
    {
        const InstanceMeta* meta = m_Impl->Resolve(handle);
        if (!meta)
        {
            ++m_Impl->Diagnostics.FallbackSlotResolveCount;
            return kDefaultMaterialSlotIndex;
        }
        return handle.Index;
    }

    // -----------------------------------------------------------------
    MaterialLayoutContract MaterialSystem::GetLayoutContract() const noexcept
    {
        return GetCanonicalMaterialLayoutContract();
    }

    // -----------------------------------------------------------------
    std::uint32_t MaterialSystem::GetLiveInstanceCount() const noexcept
    {
        std::uint32_t count = 0;
        for (const InstanceMeta& m : m_Impl->Meta)
            if (m.Live) ++count;
        return count;
    }

    std::uint32_t MaterialSystem::GetRegisteredTypeCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_Impl->Types.size());
    }

    std::uint32_t MaterialSystem::GetCapacity() const noexcept
    {
        return m_Impl->GpuCapacity;
    }

    MaterialSystemDiagnostics MaterialSystem::GetDiagnostics() const noexcept
    {
        MaterialSystemDiagnostics diagnostics = m_Impl->Diagnostics;
        for (const bool dirty : m_Impl->DirtySet)
        {
            if (dirty) ++diagnostics.DirtySlotCount;
        }
        diagnostics.LiveInstanceCount = GetLiveInstanceCount();
        diagnostics.RegisteredTypeCount = GetRegisteredTypeCount();
        diagnostics.Capacity = GetCapacity();
        return diagnostics;
    }

} // namespace Extrinsic::Graphics

