module;
#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
module Extrinsic.Graphics.GpuPropertyResidency;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Graphics
{
    struct GpuPropertyResidency::Impl
    {
        struct Slot
        {
            GpuPropertyView View{};
            std::shared_ptr<int> Lease{std::make_shared<int>(0)};
            std::optional<std::uint64_t> LastFrame{};
            std::vector<RHI::TransferToken> Transfers{};
            std::vector<RHI::ReadbackToken> Readbacks{};
            double LastUse{};
        };
        struct Canonical
        {
            GpuPropertyKey Key{};
            Slot Slot{};
        };
        struct Ring
        {
            GpuPropertyKey Key{};
            GpuPropertyLayout Layout{};
            std::uint32_t Depth{};
            std::vector<Slot> Slots{};
            int Front{-1}, Back{-1};
            std::uint64_t FrontPublication{};
            std::uint64_t Generation{};
        };
        std::uint64_t NextPublication{1u};
        std::uint64_t NextRingGeneration{1u};
        RHI::IDevice& Device;
        GpuPropertyResidencyConfig Config;
        std::vector<Canonical> Canonicals{};
        std::vector<Ring> Rings{};
        std::vector<Slot> Retired{}; // released while leased or pending; freed once neither
        GpuPropertyResidencyStats Stats{};
        std::vector<std::byte> Staging{};
        Impl(RHI::IDevice& device, GpuPropertyResidencyConfig config) : Device(device), Config(std::move(config)) {}

        double Now() const
        {
            if (Config.Clock) return Config.Clock();
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        Canonical* FindCanonical(const GpuPropertyKey& key)
        {
            const auto it = std::ranges::find_if(Canonicals, [&](const Canonical& c) { return c.Key == key; });
            return it != Canonicals.end() ? &*it : nullptr;
        }
        Ring* FindRing(const GpuPropertyKey& key)
        {
            const auto it = std::ranges::find_if(Rings, [&](const Ring& r) { return r.Key == key; });
            return it != Rings.end() ? &*it : nullptr;
        }
        Slot* FindSlot(const RHI::BufferHandle buffer)
        {
            for (auto& c : Canonicals) if (c.Slot.View.Buffer == buffer) return &c.Slot;
            for (auto& r : Rings) for (auto& s : r.Slots) if (s.View.Buffer == buffer) return &s;
            for (auto& s : Retired) if (s.View.Buffer == buffer) return &s;
            return nullptr;
        }
        static bool Leased(const Slot& slot) { return slot.Lease.use_count() > 1; }
        // Drops finished tokens; true when nothing recorded on the slot is still in flight.
        bool Complete(Slot& slot)
        {
            auto& queue = Device.GetTransferQueue();
            std::erase_if(slot.Transfers, [&](const RHI::TransferToken t) { return queue.IsComplete(t); });
            std::erase_if(slot.Readbacks, [&](const RHI::ReadbackToken t) { return queue.IsComplete(t); });
            if (!slot.Transfers.empty() || !slot.Readbacks.empty()) return false;
            // The frame counter advances at EndFrame, but frame N's fence is waited only by
            // BeginFrame(N + FramesInFlight): at a distance of exactly FramesInFlight the use
            // may still be executing, so one more frame is required.
            const auto frame = Device.GetGlobalFrameNumber();
            return !slot.LastFrame || (frame >= *slot.LastFrame && frame - *slot.LastFrame > Device.GetFramesInFlight());
        }
        // A tightly packed stride and stride 0 name the same bytes; one identity for both.
        static GpuPropertyLayout Normalized(GpuPropertyLayout layout)
        {
            if (layout.Stride == GpuScalarBytes(layout.Scalar) * layout.Channels) layout.Stride = 0u;
            return layout;
        }
        bool Reusable(Slot& slot) { return !Leased(slot) && Complete(slot); }
        GpuPropertyView Use(Slot& slot)
        {
            slot.LastFrame = Device.GetGlobalFrameNumber();
            slot.LastUse = Now();
            auto view = slot.View;
            view.Lease = slot.Lease;
            return view;
        }
        std::optional<Slot> Allocate(const GpuPropertyLayout& layout, const char* name)
        {
            const auto bytes = layout.Bytes();
            const auto buffer = Device.CreateBuffer(RHI::BufferDesc{
                .SizeBytes = bytes,
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName = name});
            if (!buffer.IsValid()) return std::nullopt;
            Stats.ResidentBytes += bytes;
            return Slot{.View = {.Buffer = buffer, .Address = Device.GetBufferDeviceAddress(buffer), .Bytes = bytes,
                                 .Layout = layout},
                        .LastUse = Now()};
        }
        void Release(Slot& slot)
        {
            if (slot.View.Buffer.IsValid()) Device.DestroyBuffer(slot.View.Buffer); // frame-deferred
            Stats.ResidentBytes -= slot.View.Bytes;
            ++Stats.Releases;
        }
        void Retire(Slot&& slot)
        {
            if (Reusable(slot)) Release(slot);
            else Retired.push_back(std::move(slot));
        }
        void Sweep()
        {
            std::erase_if(Retired, [&](Slot& slot) {
                if (!Reusable(slot)) return false;
                Release(slot);
                return true;
            });
        }
        void DiscardRing(Ring& ring)
        {
            for (auto& slot : ring.Slots) Retire(std::move(slot));
            ring.Slots.clear();
        }
    };

    GpuPropertyResidency::GpuPropertyResidency(RHI::IDevice& device, GpuPropertyResidencyConfig config)
        : m_Impl(std::make_unique<Impl>(device, std::move(config)))
    {
    }
    GpuPropertyResidency::~GpuPropertyResidency() { Clear(); }

    std::optional<GpuPropertyView> GpuPropertyResidency::AcquireInput(
        const GpuPropertyKey& key, const std::uint64_t revision, const GpuPropertyLayout& requested,
        const std::function<void(std::span<std::byte>)>& fill)
    {
        auto& s = *m_Impl;
        s.Sweep();
        const auto layout = Impl::Normalized(requested);
        if (!s.Device.IsOperational() || !layout.Valid() || !fill) return std::nullopt;
        auto* found = s.FindCanonical(key);
        if (found && found->Slot.View.Revision == revision && found->Slot.View.Layout == layout)
        {
            ++s.Stats.Hits;
            return s.Use(found->Slot);
        }
        ++s.Stats.Misses;
        auto slot = s.Allocate(layout, "GpuPropertyResidency.Canonical");
        if (!slot) return std::nullopt;
        const auto bytes = layout.Bytes();
        s.Staging.assign(std::size_t(bytes), std::byte{0});
        fill(s.Staging);
        // Only a transfer-queue upload reports its outcome (the synchronous WriteBuffer path
        // cannot); a refused upload caches nothing and the caller defers (ADR 0030, 7).
        const auto upload = s.Device.GetTransferQueue().UploadBuffer(slot->View.Buffer, s.Staging.data(), bytes);
        if (!upload.IsValid())
        {
            ++s.Stats.UploadRefusals;
            s.Release(*slot);
            return std::nullopt;
        }
        // Another revision or layout of the key: released once nobody holds or reads it.
        if (found)
        {
            s.Retire(std::move(found->Slot));
            s.Canonicals.erase(s.Canonicals.begin() + (found - s.Canonicals.data()));
        }
        slot->View.Revision = revision;
        slot->View.Upload = upload;
        slot->Transfers.push_back(upload);
        ++s.Stats.Uploads;
        s.Stats.UploadBytes += bytes;
        s.Canonicals.push_back({.Key = key, .Slot = std::move(*slot)});
        return s.Use(s.Canonicals.back().Slot);
    }

    std::optional<GpuPropertyView> GpuPropertyResidency::AcquireBack(
        const GpuPropertyKey& key, const GpuPropertyLayout& requested, const std::uint32_t depth)
    {
        auto& s = *m_Impl;
        s.Sweep();
        const auto layout = Impl::Normalized(requested);
        if (!s.Device.IsOperational() || !layout.Valid() || depth < 1u || depth > 3u) return std::nullopt;
        auto* ring = s.FindRing(key);
        const bool created = ring == nullptr;
        if (!ring)
        {
            s.Rings.push_back({.Key = key, .Layout = layout, .Depth = depth, .Generation = s.NextRingGeneration++});
            ring = &s.Rings.back();
        }
        else if (ring->Layout != layout)
            return std::nullopt;
        int pick = -1;
        bool waited = false;
        for (int i = 0; i < int(ring->Slots.size()); ++i)
        {
            auto& slot = ring->Slots[std::size_t(i)];
            if (i == ring->Front || Impl::Leased(slot)) continue;
            if (!s.Complete(slot))
            {
                waited = true;
                continue;
            }
            pick = i;
            break;
        }
        if (pick < 0 && ring->Slots.size() < ring->Depth)
        {
            auto slot = s.Allocate(layout, "GpuPropertyResidency.Ring");
            if (!slot)
            {
                // A ring created by this call has no slot: it must not stay behind (a
                // slot-less ring would refuse every later run on the key).
                if (created) s.Rings.pop_back();
                return std::nullopt;
            }
            ring->Slots.push_back(std::move(*slot));
            pick = int(ring->Slots.size()) - 1;
        }
        if (waited) ++s.Stats.RingWaits;
        if (pick < 0)
        {
            ++s.Stats.DroppedPreviews;
            return std::nullopt;
        }
        ring->Back = pick;
        return s.Use(ring->Slots[std::size_t(pick)]);
    }

    bool GpuPropertyResidency::Publish(const GpuPropertyKey& key, std::optional<std::array<float, 2>> scalarRange)
    {
        auto* ring = m_Impl->FindRing(key);
        if (!ring || ring->Back < 0) return false;
        ring->Front = std::exchange(ring->Back, -1);
        ring->Slots[std::size_t(ring->Front)].View.ScalarRange = scalarRange;
        ring->FrontPublication = m_Impl->NextPublication++;
        ++m_Impl->Stats.Publishes;
        return true;
    }

    void GpuPropertyResidency::Discard(const GpuPropertyKey& key)
    {
        auto& s = *m_Impl;
        std::erase_if(s.Rings, [&](Impl::Ring& ring) {
            if (!(ring.Key == key)) return false;
            s.DiscardRing(ring);
            return true;
        });
    }

    std::uint64_t GpuPropertyResidency::RingGeneration(const GpuPropertyKey& key) const
    {
        const auto* ring = m_Impl->FindRing(key);
        return ring ? ring->Generation : 0u;
    }

    bool GpuPropertyResidency::Discard(const GpuPropertyKey& key, const std::uint64_t generation)
    {
        if (generation == 0u || RingGeneration(key) != generation) return false;
        Discard(key);
        return true;
    }

    std::optional<GpuPropertyView> GpuPropertyResidency::Front(const GpuPropertyKey& key) const
    {
        auto& s = *m_Impl;
        if (auto* ring = s.FindRing(key); ring && ring->Front >= 0)
        {
            auto view = ring->Slots[std::size_t(ring->Front)].View;
            view.Lease = ring->Slots[std::size_t(ring->Front)].Lease;
            view.Publication = ring->FrontPublication;
            return view;
        }
        if (auto* canonical = s.FindCanonical(key))
        {
            auto view = canonical->Slot.View;
            view.Lease = canonical->Slot.Lease;
            return view;
        }
        return std::nullopt;
    }

    bool GpuPropertyResidency::BindRevision(const GpuPropertyKey& key, const std::uint64_t revision,
                                            const std::uint64_t publication)
    {
        auto& s = *m_Impl;
        auto* ring = s.FindRing(key);
        if (!ring || ring->Front < 0) return false;
        if (publication != 0u && ring->FrontPublication != publication) return false;
        Impl::Slot front = std::move(ring->Slots[std::size_t(ring->Front)]);
        front.View.Publication = ring->FrontPublication;
        ring->Slots.erase(ring->Slots.begin() + ring->Front);
        ring->Front = -1;
        s.DiscardRing(*ring);
        s.Rings.erase(s.Rings.begin() + (ring - s.Rings.data()));
        front.View.Revision = revision;
        front.View.Upload = {};
        front.LastUse = s.Now();
        if (auto* canonical = s.FindCanonical(key))
        {
            s.Retire(std::move(canonical->Slot));
            canonical->Slot = std::move(front);
        }
        else
            s.Canonicals.push_back({.Key = key, .Slot = std::move(front)});
        return true;
    }

    bool GpuPropertyResidency::HasRing(const GpuPropertyKey& key) const { return m_Impl->FindRing(key) != nullptr; }

    void GpuPropertyResidency::NoteUse(const RHI::BufferHandle slot, const std::uint64_t frame)
    {
        if (auto* found = m_Impl->FindSlot(slot))
        {
            found->LastFrame = std::max(found->LastFrame.value_or(0u), frame);
            found->LastUse = m_Impl->Now();
        }
    }

    void GpuPropertyResidency::AddCompletion(const RHI::BufferHandle slot, const RHI::TransferToken token)
    {
        if (!token.IsValid()) return;
        if (auto* found = m_Impl->FindSlot(slot)) found->Transfers.push_back(token);
    }

    void GpuPropertyResidency::AddCompletion(const RHI::BufferHandle slot, const RHI::ReadbackToken token,
                                             const std::uint64_t bytes)
    {
        auto* found = m_Impl->FindSlot(slot);
        if (!found) return;
        ++m_Impl->Stats.Readbacks;
        m_Impl->Stats.ReadbackBytes += bytes;
        if (token.IsValid()) found->Readbacks.push_back(token);
    }

    void GpuPropertyResidency::MarkObserved(const GpuPropertyKey& key)
    {
        auto& s = *m_Impl;
        if (auto* ring = s.FindRing(key); ring && ring->Front >= 0)
            (void)s.Use(ring->Slots[std::size_t(ring->Front)]);
        else if (auto* canonical = s.FindCanonical(key))
            (void)s.Use(canonical->Slot);
    }

    void GpuPropertyResidency::Tick()
    {
        auto& s = *m_Impl;
        s.Sweep();
        const double now = s.Now();
        const auto evictable = [&](Impl::Canonical& c) { return !s.FindRing(c.Key) && s.Reusable(c.Slot); };
        const auto evict = [&](const std::size_t index) {
            s.Release(s.Canonicals[index].Slot);
            s.Canonicals.erase(s.Canonicals.begin() + std::ptrdiff_t(index));
            ++s.Stats.Evictions;
        };
        if (s.Config.IdleEvictSeconds > 0.0)
            for (std::size_t i = 0; i < s.Canonicals.size();)
            {
                if (now - s.Canonicals[i].Slot.LastUse > s.Config.IdleEvictSeconds && evictable(s.Canonicals[i]))
                    evict(i);
                else
                    ++i;
            }
        if (s.Config.BudgetBytes == 0u) return;
        while (s.Stats.ResidentBytes > s.Config.BudgetBytes)
        {
            // Least recently used first, weighted by size: the largest stale slot goes first.
            std::optional<std::size_t> victim;
            double best = -1.0;
            for (std::size_t i = 0; i < s.Canonicals.size(); ++i)
            {
                auto& c = s.Canonicals[i];
                if (!evictable(c)) continue;
                const double score = (now - c.Slot.LastUse) * double(c.Slot.View.Bytes);
                if (!victim || score > best ||
                    (score == best && c.Slot.LastUse < s.Canonicals[*victim].Slot.LastUse))
                {
                    victim = i;
                    best = score;
                }
            }
            if (!victim) break;
            evict(*victim);
        }
    }

    void GpuPropertyResidency::Prune(const std::function<bool(const GpuPropertyKey&)>& keep)
    {
        auto& s = *m_Impl;
        s.Sweep();
        std::erase_if(s.Rings, [&](Impl::Ring& ring) {
            if (keep && keep(ring.Key)) return false;
            s.DiscardRing(ring);
            return true;
        });
        std::erase_if(s.Canonicals, [&](Impl::Canonical& c) {
            if (keep && keep(c.Key)) return false;
            s.Retire(std::move(c.Slot));
            return true;
        });
    }

    void GpuPropertyResidency::Clear()
    {
        auto& s = *m_Impl;
        for (auto& c : s.Canonicals) s.Release(c.Slot);
        for (auto& ring : s.Rings) for (auto& slot : ring.Slots) s.Release(slot);
        for (auto& slot : s.Retired) s.Release(slot);
        s.Canonicals.clear();
        s.Rings.clear();
        s.Retired.clear();
    }

    GpuPropertyResidencyStats GpuPropertyResidency::Stats() const noexcept
    {
        auto stats = m_Impl->Stats;
        stats.Slots = std::uint32_t(m_Impl->Canonicals.size());
        stats.Rings = std::uint32_t(m_Impl->Rings.size());
        return stats;
    }
}
