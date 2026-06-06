module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

module Extrinsic.RHI.SamplerManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

// ============================================================
// SamplerManager — implementation
// ============================================================
// The dedup table maps a 64-bit FNV-1a hash of SamplerDesc's fields to the
// IDevice-issued sampler handle. The lease published to callers is that same
// device handle, so downstream APIs such as TextureManager::Create can pass it
// through to backend bindless resolvers without a manager-local translation
// step. On collision the matching desc is verified byte-for-byte before reuse
// (the assert fires on a true hash collision, which is an engine structural
// bug).
//
// Complexity:
//   GetOrCreate — O(1) average (hash lookup + optional create)
//   Retain       — O(1) average (handle lookup + refcount increment)
//   Release      — O(1) average (handle lookup + optional device destroy)
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
            h = mix(h, &d.BorderColor,     sizeof(d.BorderColor));
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
                && a.BorderColor   == b.BorderColor
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

        SamplerSlot() = default;
        SamplerSlot(const SamplerSlot&)            = delete;
        SamplerSlot& operator=(const SamplerSlot&) = delete;
        SamplerSlot(SamplerSlot&&)                 = delete;
        SamplerSlot& operator=(SamplerSlot&&)      = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct SamplerManager::Impl
    {
        IDevice&                        Device;
        std::mutex                      Mutex;
        std::unordered_map<SamplerHandle, SamplerSlot,
                           Core::StrongHandleHash<SamplerTag>> Slots;
        // desc hash -> device sampler handle (only live entries)
        std::unordered_map<std::uint64_t, SamplerHandle> DedupTable;

        // Outstanding SamplerLease instances. See BufferManager.cpp for the
        // F2 rationale.
        std::atomic<std::uint32_t>      LiveLeaseCount{0};

        explicit Impl(IDevice& device) : Device(device) {}

        [[nodiscard]] SamplerSlot* Resolve(SamplerHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }

        [[nodiscard]] const SamplerSlot* Resolve(SamplerHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }
    };

    // -----------------------------------------------------------------
    // SamplerManager
    // -----------------------------------------------------------------
    SamplerManager::SamplerManager(IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {}

    SamplerManager::~SamplerManager()
    {
        const auto alive = m_Impl->LiveLeaseCount.load(std::memory_order_acquire);
        assert(alive == 0 &&
               "SamplerManager destroyed while leases are still alive — drop "
               "all SamplerLease instances before destroying this manager.");
        (void)alive;
    }

    // -----------------------------------------------------------------
    Core::Expected<SamplerManager::SamplerLease> SamplerManager::GetOrCreate(const SamplerDesc& desc)
    {
        // F14: short-circuit on stub backends. Checked before the mutex so
        // the stub case takes the fastest possible path with no contention.
        // A non-operational backend can never have populated the dedup table,
        // so hash-lookup would miss anyway.
        if (!m_Impl->Device.IsOperational())
            return Core::Err<SamplerLease>(Core::ErrorCode::DeviceNotOperational);

        const std::uint64_t hash = HashSamplerDesc(desc);

        std::lock_guard lock{m_Impl->Mutex};

        // --- dedup lookup ---
        if (auto it = m_Impl->DedupTable.find(hash); it != m_Impl->DedupTable.end())
        {
            SamplerSlot* slot = m_Impl->Resolve(it->second);
            assert(slot && "SamplerManager dedup table referenced a stale sampler handle");
            if (!slot)
            {
                m_Impl->DedupTable.erase(it);
                return Core::Err<SamplerLease>(Core::ErrorCode::InvalidState);
            }

            assert(SamplerDescsEqual(slot->Desc, desc) &&
                   "SamplerDesc hash collision — two distinct descs mapped to the same hash");

            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
            m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
            // Lease::Adopt — we already incremented refcount above.
            return SamplerLease::Adopt(*this, it->second);
        }

        // --- not found: allocate new GPU sampler ---
        SamplerHandle deviceHandle = m_Impl->Device.CreateSampler(desc);
        if (!deviceHandle.IsValid())
            return Core::Err<SamplerLease>(Core::ErrorCode::OutOfDeviceMemory);

        const auto [slotIt, inserted] = m_Impl->Slots.try_emplace(deviceHandle);
        assert(inserted &&
               "SamplerManager::GetOrCreate — IDevice issued a duplicate live "
               "SamplerHandle. The device-side pool must increment the handle "
               "generation on reuse; see Core::ResourcePool::Add.");
        (void)inserted;
        SamplerSlot& slot  = slotIt->second;
        slot.Desc          = desc;
        slot.DeviceHandle  = deviceHandle;
        slot.RefCount.store(1, std::memory_order_relaxed);

        m_Impl->DedupTable[hash] = deviceHandle;

        m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        return SamplerLease::Adopt(*this, deviceHandle);
    }

    // -----------------------------------------------------------------
    void SamplerManager::Retain(SamplerHandle handle)
    {
        std::lock_guard lock{m_Impl->Mutex};
        const auto it = m_Impl->Slots.find(handle);
        assert(it != m_Impl->Slots.end() && "Retain called on invalid or released handle");
        if (it != m_Impl->Slots.end())
        {
            it->second.RefCount.fetch_add(1, std::memory_order_relaxed);
            m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------
    void SamplerManager::Release(SamplerHandle handle)
    {
        SamplerHandle deviceHandleToDestroy{};
        bool shouldDestroy = false;
        {
            std::lock_guard lock{m_Impl->Mutex};
            const auto it = m_Impl->Slots.find(handle);
            assert(it != m_Impl->Slots.end() &&
                   "Release called on invalid or already-freed handle");
            if (it == m_Impl->Slots.end()) return;

            SamplerSlot& slot = it->second;
            const std::uint32_t prev =
                slot.RefCount.fetch_sub(1, std::memory_order_acq_rel);

            assert(prev > 0 && "Refcount underflow");

            m_Impl->LiveLeaseCount.fetch_sub(1, std::memory_order_acq_rel);

            if (prev == 1)
            {
                deviceHandleToDestroy = slot.DeviceHandle;
                const std::uint64_t hash = HashSamplerDesc(slot.Desc);
                m_Impl->DedupTable.erase(hash);
                m_Impl->Slots.erase(it);
                shouldDestroy = true;
            }
        }

        if (shouldDestroy)
        {
            m_Impl->Device.DestroySampler(deviceHandleToDestroy);
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
