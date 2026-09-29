module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
module Extrinsic.Graphics.GpuPropertyResidency;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    struct GpuPropertyResidency::Impl
    {
        struct Slot
        {
            GpuPropertyKey Key{};
            GpuPropertyView View{};
            std::shared_ptr<int> Lease{std::make_shared<int>(0)};
        };
        RHI::IDevice& Device;
        std::vector<Slot> Slots{};
        std::vector<Slot> Retired{}; // replaced while still leased; released once unleased
        GpuPropertyResidencyStats Stats{};
        std::vector<std::byte> Staging{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}

        void Release(Slot& slot)
        {
            if (slot.View.Buffer.IsValid()) Device.DestroyBuffer(slot.View.Buffer); // frame-deferred
            Stats.ResidentBytes -= slot.View.Bytes;
            ++Stats.Releases;
        }
        void SweepRetired()
        {
            std::erase_if(Retired, [&](Slot& slot) {
                if (slot.Lease.use_count() > 1) return false;
                Release(slot);
                return true;
            });
        }
    };

    GpuPropertyResidency::GpuPropertyResidency(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    GpuPropertyResidency::~GpuPropertyResidency() { Clear(); }

    std::optional<GpuPropertyView> GpuPropertyResidency::AcquireInput(
        const GpuPropertyKey& key, const std::uint64_t revision, const std::uint32_t elementBytes,
        const std::uint32_t count, const std::function<void(std::span<std::byte>)>& fill)
    {
        auto& s = *m_Impl;
        s.SweepRetired();
        if (!s.Device.IsOperational() || elementBytes == 0u || count == 0u || !fill) return std::nullopt;
        const auto found = std::ranges::find_if(s.Slots, [&](const Impl::Slot& slot) { return slot.Key == key; });
        if (found != s.Slots.end() && found->View.Revision == revision && found->View.ElementBytes == elementBytes &&
            found->View.Count == count)
        {
            ++s.Stats.Hits;
            auto view = found->View;
            view.Lease = found->Lease;
            return view;
        }
        const std::uint64_t bytes = std::uint64_t(elementBytes) * count;
        const auto buffer = s.Device.CreateBuffer(RHI::BufferDesc{
            .SizeBytes = bytes,
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = "GpuPropertyResidency.Canonical"});
        if (!buffer.IsValid()) return std::nullopt;
        s.Staging.assign(std::size_t(bytes), std::byte{0});
        fill(s.Staging);
        s.Device.WriteBuffer(buffer, s.Staging.data(), bytes, 0u);
        // A new revision replaces the slot. Holders of the old view keep its buffer alive until
        // they drop their lease; the slot is released here only if nobody holds it.
        if (found != s.Slots.end())
        {
            if (found->Lease.use_count() == 1) s.Release(*found);
            else s.Retired.push_back(std::move(*found));
            s.Slots.erase(found);
        }
        Impl::Slot slot{.Key = key,
                        .View = {.Buffer = buffer, .Address = s.Device.GetBufferDeviceAddress(buffer), .Bytes = bytes,
                                 .Revision = revision, .ElementBytes = elementBytes, .Count = count}};
        s.Stats.ResidentBytes += bytes;
        ++s.Stats.Uploads;
        s.Stats.UploadBytes += bytes;
        auto view = slot.View;
        view.Lease = slot.Lease;
        s.Slots.push_back(std::move(slot));
        return view;
    }

    void GpuPropertyResidency::Prune(const std::function<bool(const GpuPropertyKey&)>& keep)
    {
        auto& s = *m_Impl;
        s.SweepRetired();
        std::erase_if(s.Slots, [&](Impl::Slot& slot) {
            if (slot.Lease.use_count() > 1 || (keep && keep(slot.Key))) return false;
            s.Release(slot);
            return true;
        });
    }

    void GpuPropertyResidency::Clear()
    {
        auto& s = *m_Impl;
        for (auto& slot : s.Slots) s.Release(slot);
        for (auto& slot : s.Retired) s.Release(slot);
        s.Slots.clear();
        s.Retired.clear();
    }

    GpuPropertyResidencyStats GpuPropertyResidency::Stats() const noexcept
    {
        auto stats = m_Impl->Stats;
        stats.Slots = std::uint32_t(m_Impl->Slots.size());
        return stats;
    }
}
