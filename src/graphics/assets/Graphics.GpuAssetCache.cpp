module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.Graphics.GpuAssetCache;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct Slot
        {
            // Visible to GetView() once state == Ready.  Empty before then
            // for first-time uploads; retains the previous resource for the
            // duration of an in-flight reload.
            RHI::BufferManager::BufferLease   CurrentBuffer{};
            RHI::TextureManager::TextureLease CurrentTexture{};
            RHI::BindlessIndex                CurrentBindless = RHI::kInvalidBindlessIndex;
            std::uint64_t                     CurrentGeneration = 0;

            // Pending upload (during GpuUploading).  Becomes Current when
            // the transfer token completes during Tick().
            RHI::BufferManager::BufferLease   PendingBuffer{};
            RHI::TextureManager::TextureLease PendingTexture{};
            RHI::BindlessIndex                PendingBindless = RHI::kInvalidBindlessIndex;
            std::uint64_t                     PendingGeneration = 0;
            RHI::TransferToken                InFlight{};

            GpuAssetState State = GpuAssetState::NotRequested;
            GpuAssetKind  Kind  = GpuAssetKind::Buffer;
        };

        // Old leases held alive across a hot reload.  Both Buffer and
        // Texture members are present so a single record can carry whichever
        // resource kind the slot was using; the unused member is empty.
        struct RetireRecord
        {
            RHI::BufferManager::BufferLease   Buffer{};
            RHI::TextureManager::TextureLease Texture{};
            std::uint64_t Deadline    = 0;
            bool          DeadlineSet = false;
        };
    }

    struct GpuAssetCache::Impl
    {
        RHI::BufferManager&  Buffers;
        RHI::TextureManager& Textures;
        RHI::ITransferQueue& Transfer;

        mutable std::mutex Mutex{};
        std::unordered_map<Assets::AssetId, Slot, Assets::AssetIdHash> Slots{};
        std::vector<RetireRecord> Retire{};
        std::uint64_t NextGeneration = 1;

        Impl(RHI::BufferManager& b, RHI::TextureManager& t, RHI::ITransferQueue& q)
            : Buffers(b), Textures(t), Transfer(q)
        {
        }

        // Move whatever is currently in `Current*` into the retire queue
        // (with the deadline left unset; Tick anchors it on the next pass).
        void RetireCurrent(Slot& slot)
        {
            if (!slot.CurrentBuffer.IsValid() && !slot.CurrentTexture.IsValid())
                return;

            RetireRecord rec{};
            rec.Buffer  = std::move(slot.CurrentBuffer);
            rec.Texture = std::move(slot.CurrentTexture);
            Retire.push_back(std::move(rec));

            slot.CurrentBindless   = RHI::kInvalidBindlessIndex;
            slot.CurrentGeneration = 0;
        }

        void RetirePending(Slot& slot)
        {
            if (!slot.PendingBuffer.IsValid() && !slot.PendingTexture.IsValid())
                return;

            RetireRecord rec{};
            rec.Buffer  = std::move(slot.PendingBuffer);
            rec.Texture = std::move(slot.PendingTexture);
            Retire.push_back(std::move(rec));

            slot.PendingBindless   = RHI::kInvalidBindlessIndex;
            slot.PendingGeneration = 0;
            slot.InFlight          = {};
        }
    };

    GpuAssetCache::GpuAssetCache(RHI::BufferManager&  buffers,
                                 RHI::TextureManager& textures,
                                 RHI::ITransferQueue& transfer)
        : m_Impl(std::make_unique<Impl>(buffers, textures, transfer))
    {
    }

    GpuAssetCache::~GpuAssetCache() = default;

    Core::Result GpuAssetCache::RequestUpload(const GpuBufferRequest& req)
    {
        if (!req.Id.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);

        Slot& slot = m_Impl->Slots[req.Id];

        if (slot.State == GpuAssetState::GpuUploading)
            return Core::Err(Core::ErrorCode::ResourceBusy);

        slot.Kind = GpuAssetKind::Buffer;

        auto leaseOr = m_Impl->Buffers.Create(req.Desc);
        if (!leaseOr.has_value())
        {
            // Allocation failure: keep any existing Current view valid;
            // tear down any old pending so we don't leak an in-flight token.
            m_Impl->RetirePending(slot);
            slot.State = GpuAssetState::Failed;
            return Core::Err(leaseOr.error());
        }

        const RHI::BufferHandle handle = leaseOr->GetHandle();
        const RHI::TransferToken token = m_Impl->Transfer.UploadBuffer(
            handle, req.Bytes, /*offset=*/0);

        // Drop any earlier pending (defensive: state machine forbids
        // overlapping pending uploads, but be explicit).
        m_Impl->RetirePending(slot);

        slot.PendingBuffer     = std::move(*leaseOr);
        slot.PendingTexture    = {};
        slot.PendingBindless   = RHI::kInvalidBindlessIndex;
        slot.PendingGeneration = m_Impl->NextGeneration++;
        slot.InFlight          = token;
        slot.State             = GpuAssetState::GpuUploading;
        return Core::Ok();
    }

    Core::Result GpuAssetCache::RequestUpload(const GpuTextureRequest& req)
    {
        if (!req.Id.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);

        Slot& slot = m_Impl->Slots[req.Id];

        if (slot.State == GpuAssetState::GpuUploading)
            return Core::Err(Core::ErrorCode::ResourceBusy);

        slot.Kind = GpuAssetKind::Texture;

        auto leaseOr = m_Impl->Textures.Create(req.Desc, req.Sampler);
        if (!leaseOr.has_value())
        {
            m_Impl->RetirePending(slot);
            slot.State = GpuAssetState::Failed;
            return Core::Err(leaseOr.error());
        }

        const RHI::TextureHandle handle = leaseOr->GetHandle();
        const RHI::TransferToken token = m_Impl->Transfer.UploadTexture(
            handle, req.Bytes.data(), static_cast<std::uint64_t>(req.Bytes.size()),
            /*mipLevel=*/0, /*arrayLayer=*/0);

        const RHI::BindlessIndex bindless = m_Impl->Textures.GetBindlessIndex(handle);

        m_Impl->RetirePending(slot);

        slot.PendingTexture    = std::move(*leaseOr);
        slot.PendingBuffer     = {};
        slot.PendingBindless   = bindless;
        slot.PendingGeneration = m_Impl->NextGeneration++;
        slot.InFlight          = token;
        slot.State             = GpuAssetState::GpuUploading;
        return Core::Ok();
    }

    void GpuAssetCache::Reserve(Assets::AssetId id)
    {
        if (!id.IsValid()) return;
        std::lock_guard guard(m_Impl->Mutex);
        Slot& slot = m_Impl->Slots[id];
        if (slot.State == GpuAssetState::NotRequested ||
            slot.State == GpuAssetState::Failed)
        {
            slot.State = GpuAssetState::CpuPending;
        }
    }

    void GpuAssetCache::NotifyFailed(Assets::AssetId id)
    {
        if (!id.IsValid()) return;
        std::lock_guard guard(m_Impl->Mutex);
        Slot& slot = m_Impl->Slots[id];
        m_Impl->RetirePending(slot);
        slot.State = GpuAssetState::Failed;
    }

    void GpuAssetCache::NotifyReloaded(Assets::AssetId id)
    {
        if (!id.IsValid()) return;
        std::lock_guard guard(m_Impl->Mutex);
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return;
        Slot& slot = it->second;
        if (slot.State == GpuAssetState::Ready)
            slot.State = GpuAssetState::CpuPending;
    }

    void GpuAssetCache::NotifyDestroyed(Assets::AssetId id)
    {
        if (!id.IsValid()) return;
        std::lock_guard guard(m_Impl->Mutex);
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return;
        Slot& slot = it->second;
        m_Impl->RetirePending(slot);
        m_Impl->RetireCurrent(slot);
        m_Impl->Slots.erase(it);
    }

    void GpuAssetCache::Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight)
    {
        std::lock_guard guard(m_Impl->Mutex);

        // 1. Promote completed pending uploads to current.
        for (auto& [id, slot] : m_Impl->Slots)
        {
            if (slot.State != GpuAssetState::GpuUploading)
                continue;
            if (!m_Impl->Transfer.IsComplete(slot.InFlight))
                continue;

            // Move old current (if any) to retire queue first.
            m_Impl->RetireCurrent(slot);

            slot.CurrentBuffer     = std::move(slot.PendingBuffer);
            slot.CurrentTexture    = std::move(slot.PendingTexture);
            slot.CurrentBindless   = slot.PendingBindless;
            slot.CurrentGeneration = slot.PendingGeneration;
            slot.PendingBindless   = RHI::kInvalidBindlessIndex;
            slot.PendingGeneration = 0;
            slot.InFlight          = {};
            slot.State             = GpuAssetState::Ready;
        }

        // 2. Anchor deadlines for any newly-retired records.
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_Impl->Retire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline    = deadline;
                rec.DeadlineSet = true;
            }
        }

        // 3. Drop retire records whose deadline has been reached.
        auto end = std::remove_if(m_Impl->Retire.begin(), m_Impl->Retire.end(),
            [currentFrame](const RetireRecord& rec) {
                return rec.DeadlineSet && rec.Deadline <= currentFrame;
            });
        m_Impl->Retire.erase(end, m_Impl->Retire.end());
    }

    GpuAssetState GpuAssetCache::GetState(Assets::AssetId id) const
    {
        if (!id.IsValid()) return GpuAssetState::NotRequested;
        std::lock_guard guard(m_Impl->Mutex);
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return GpuAssetState::NotRequested;
        return it->second.State;
    }

    Core::Expected<GpuAssetView> GpuAssetCache::GetView(Assets::AssetId id) const
    {
        if (!id.IsValid())
            return Core::Err<GpuAssetView>(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return Core::Err<GpuAssetView>(Core::ErrorCode::ResourceNotFound);

        const Slot& slot = it->second;
        const bool hasCurrent = slot.CurrentBuffer.IsValid() || slot.CurrentTexture.IsValid();
        if (!hasCurrent)
            return Core::Err<GpuAssetView>(Core::ErrorCode::InvalidState);

        GpuAssetView view{};
        view.Kind        = slot.Kind;
        view.Buffer      = slot.CurrentBuffer.GetHandle();
        view.Texture     = slot.CurrentTexture.GetHandle();
        view.BindlessIdx = slot.CurrentBindless;
        view.Generation  = slot.CurrentGeneration;
        return view;
    }

    std::size_t GpuAssetCache::TrackedCount() const
    {
        std::lock_guard guard(m_Impl->Mutex);
        return m_Impl->Slots.size();
    }

    std::size_t GpuAssetCache::PendingRetireCount() const
    {
        std::lock_guard guard(m_Impl->Mutex);
        return m_Impl->Retire.size();
    }
}
