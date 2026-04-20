module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

module Extrinsic.RHI.SamplerManager;

import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

// ============================================================
// SamplerManager — implementation
// ============================================================
// The dedup table maps a 64-bit FNV-1a hash of SamplerDesc's
// fields to a pool index.  On collision the matching desc is
// verified byte-for-byte before reuse (the assert fires on a
// true hash collision, which is an engine structural bug).
//
// Complexity:
//   GetOrCreate — O(1) average (hash lookup + optional create)
//   Retain       — O(1) lock-free
//   Release      — O(1) lock-free; O(1) locked on zero
//   GetDesc      — O(1) lock-free
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // FNV-1a over raw bytes of SamplerDesc (excluding DebugName pointer)
    // -----------------------------------------------------------------
    namespace
    {
        [[nodiscard]] std::uint64_t HashSamplerDesc(const SamplerDesc& d) noexcept
        {
            // Hash only the value fields, not the DebugName char*.
            // Layout-stable fields in declaration order:
            constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
            constexpr std::uint64_t FNV_PRIME  = 1099511628211ull;

            auto mix = [&](std::uint64_t h, const void* data, std::size_t n) -> std::uint64_t
            {
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                for (std::size_t i = 0; i < n; ++i)
                { h ^= bytes[i]; h *= FNV_PRIME; }
                return h;
            };

            std::uint64_t h = FNV_OFFSET;
            h = mix(h, &d.MagFilter,       sizeof(d.MagFilter));
            h = mix(h, &d.MinFilter,       sizeof(d.MinFilter));
            h = mix(h, &d.MipFilter,       sizeof(d.MipFilter));
            h = mix(h, &d.AddressU,        sizeof(d.AddressU));
            h = mix(h, &d.AddressV,        sizeof(d.AddressV));
            h = mix(h, &d.AddressW,        sizeof(d.AddressW));
            h = mix(h, &d.MipLodBias,      sizeof(d.MipLodBias));
            h = mix(h, &d.MinLod,          sizeof(d.MinLod));
            h = mix(h, &d.MaxLod,          sizeof(d.MaxLod));
            h = mix(h, &d.MaxAnisotropy,   sizeof(d.MaxAnisotropy));
            h = mix(h, &d.CompareEnable,   sizeof(d.CompareEnable));
            h = mix(h, &d.Compare,         sizeof(d.Compare));
            return h;
        }

        [[nodiscard]] bool SamplerDescsEqual(const SamplerDesc& a, const SamplerDesc& b) noexcept
        {
            return a.MagFilter     == b.MagFilter
                && a.MinFilter     == b.MinFilter
                && a.MipFilter     == b.MipFilter
                && a.AddressU      == b.AddressU
                && a.AddressV      == b.AddressV
                && a.AddressW      == b.AddressW
                && a.MipLodBias    == b.MipLodBias
                && a.MinLod        == b.MinLod
                && a.MaxLod        == b.MaxLod
                && a.MaxAnisotropy == b.MaxAnisotropy
                && a.CompareEnable == b.CompareEnable
                && a.Compare       == b.Compare;
        }
    } // namespace

    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct SamplerSlot
    {
        SamplerDesc               Desc{};
        SamplerHandle             DeviceHandle{};
        std::atomic<std::uint32_t> RefCount{0};
        std::uint32_t              Generation = 0;
        bool                       Live       = false;

        SamplerSlot() = default;
        SamplerSlot(const SamplerSlot&)            = delete;
        SamplerSlot& operator=(const SamplerSlot&) = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct SamplerManager::Impl
    {
        IDevice&                        Device;
        std::mutex                      Mutex;
        std::deque<SamplerSlot>         Slots;
        std::vector<std::uint32_t>      FreeList;
        // desc hash → pool index (only live entries)
        std::unordered_map<std::uint64_t, std::uint32_t> DedupTable;

        explicit Impl(IDevice& device) : Device(device) {}

        [[nodiscard]] SamplerSlot* Resolve(SamplerHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            SamplerSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }

        [[nodiscard]] const SamplerSlot* Resolve(SamplerHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            const SamplerSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }
    };

    // -----------------------------------------------------------------
    // SamplerManager
    // -----------------------------------------------------------------
    SamplerManager::SamplerManager(IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {}

    SamplerManager::~SamplerManager() = default;

    // -----------------------------------------------------------------
    SamplerManager::SamplerLease SamplerManager::GetOrCreate(const SamplerDesc& desc)
    {
        const std::uint64_t hash = HashSamplerDesc(desc);

        std::lock_guard lock{m_Impl->Mutex};

        // --- dedup lookup ---
        if (auto it = m_Impl->DedupTable.find(hash); it != m_Impl->DedupTable.end())
        {
            const std::uint32_t idx  = it->second;
            SamplerSlot&        slot = m_Impl->Slots[idx];

            assert(SamplerDescsEqual(slot.Desc, desc) &&
                   "SamplerDesc hash collision — two distinct descs mapped to the same hash");

            slot.RefCount.fetch_add(1, std::memory_order_relaxed);
            SamplerHandle poolHandle{idx, slot.Generation};
            // Lease::Adopt — we already incremented refcount above.
            return SamplerLease::Adopt(*this, poolHandle);
        }

        // --- not found: allocate new GPU sampler ---
        SamplerHandle deviceHandle = m_Impl->Device.CreateSampler(desc);
        if (!deviceHandle.IsValid())
            return {};

        std::uint32_t index;
        std::uint32_t generation;

        if (!m_Impl->FreeList.empty())
        {
            index      = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
            generation = m_Impl->Slots[index].Generation;
        }
        else
        {
            index      = static_cast<std::uint32_t>(m_Impl->Slots.size());
            generation = 0;
            m_Impl->Slots.emplace_back();
        }

        SamplerSlot& slot  = m_Impl->Slots[index];
        slot.Desc          = desc;
        slot.DeviceHandle  = deviceHandle;
        slot.Generation    = generation;
        slot.Live          = true;
        slot.RefCount.store(1, std::memory_order_relaxed);

        m_Impl->DedupTable[hash] = index;

        SamplerHandle poolHandle{index, generation};
        return SamplerLease::Adopt(*this, poolHandle);
    }

    // -----------------------------------------------------------------
    void SamplerManager::Retain(SamplerHandle handle)
    {
        SamplerSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Retain called on invalid or released handle");
        if (slot)
            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    void SamplerManager::Release(SamplerHandle handle)
    {
        SamplerSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Release called on invalid or already-freed handle");
        if (!slot) return;

        const std::uint32_t prev =
            slot->RefCount.fetch_sub(1, std::memory_order_acq_rel);

        assert(prev > 0 && "Refcount underflow");

        if (prev == 1)
        {
            m_Impl->Device.DestroySampler(slot->DeviceHandle);

            const std::uint64_t hash = HashSamplerDesc(slot->Desc);

            std::lock_guard lock{m_Impl->Mutex};
            m_Impl->DedupTable.erase(hash);
            slot->Live = false;
            slot->Generation++;
            m_Impl->FreeList.push_back(handle.Index);
        }
    }

    // -----------------------------------------------------------------
    SamplerManager::SamplerLease SamplerManager::AcquireLease(SamplerHandle handle)
    {
        return SamplerLease::RetainNew(*this, handle);
    }

    // -----------------------------------------------------------------
    const SamplerDesc* SamplerManager::GetDesc(SamplerHandle handle) const noexcept
    {
        const SamplerSlot* slot = m_Impl->Resolve(handle);
        return slot ? &slot->Desc : nullptr;
    }

    // -----------------------------------------------------------------
    std::uint32_t SamplerManager::GetLiveCount() const noexcept
    {
        std::lock_guard lock{m_Impl->Mutex};
        return static_cast<std::uint32_t>(m_Impl->DedupTable.size());
    }

} // namespace Extrinsic::RHI

