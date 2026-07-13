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
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool IsUploadDeferral(const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::DeviceNotOperational;
        }

        [[nodiscard]] bool HasTextureUsage(const RHI::TextureUsage flags,
                                           const RHI::TextureUsage bit) noexcept
        {
            return (static_cast<std::uint32_t>(flags) &
                    static_cast<std::uint32_t>(bit)) != 0u;
        }

        enum class PendingCompletionKind : std::uint8_t
        {
            None,
            TransferToken,
            FrameNumber,
        };

        struct Slot
        {
            // Visible to GetView() once state == Ready.  Empty before then
            // for first-time uploads; retains the previous resource for the
            // duration of an in-flight reload.
            RHI::BufferManager::BufferLease   CurrentBuffer{};
            RHI::TextureManager::TextureLease CurrentTexture{};
            RHI::SamplerManager::SamplerLease CurrentSampler{};
            RHI::SamplerHandle                CurrentSamplerHandle{};
            RHI::BindlessIndex                CurrentBindless = RHI::kInvalidBindlessIndex;
            std::uint64_t                     CurrentGeneration = 0;

            // Pending upload (during GpuUploading).  Becomes Current when
            // the transfer token completes during Tick().
            RHI::BufferManager::BufferLease   PendingBuffer{};
            RHI::TextureManager::TextureLease PendingTexture{};
            RHI::SamplerManager::SamplerLease PendingSampler{};
            RHI::SamplerHandle                PendingSamplerHandle{};
            RHI::BindlessIndex                PendingBindless = RHI::kInvalidBindlessIndex;
            std::uint64_t                     PendingGeneration = 0;
            RHI::TransferToken                InFlight{};
            std::uint64_t                     PendingReadyFrame = 0;
            PendingCompletionKind             CompletionKind = PendingCompletionKind::None;

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
            RHI::SamplerManager::SamplerLease Sampler{};
            std::uint64_t Deadline    = 0;
            bool          DeadlineSet = false;
        };
    }

    struct GpuAssetCache::Impl
    {
        RHI::BufferManager&  Buffers;
        RHI::TextureManager& Textures;
        RHI::SamplerManager* Samplers = nullptr;
        RHI::ITransferQueue& Transfer;

        mutable std::mutex Mutex{};
        std::unordered_map<Assets::AssetId, Slot, Assets::AssetIdHash> Slots{};
        std::vector<RetireRecord> Retire{};
        std::uint64_t NextGeneration = 1;
        RHI::TextureManager::TextureLease FallbackTexture{};
        RHI::SamplerManager::SamplerLease FallbackSampler{};
        RHI::SamplerHandle FallbackSamplerHandle{};
        RHI::BindlessIndex FallbackBindless = RHI::kInvalidBindlessIndex;
        std::uint64_t FallbackGeneration = 0;
        GpuAssetCacheDiagnostics Diagnostics{};

        Impl(RHI::BufferManager& b, RHI::TextureManager& t, RHI::SamplerManager* s, RHI::ITransferQueue& q)
            : Buffers(b), Textures(t), Samplers(s), Transfer(q)
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
            rec.Sampler = std::move(slot.CurrentSampler);
            Retire.push_back(std::move(rec));

            slot.CurrentBindless   = RHI::kInvalidBindlessIndex;
            slot.CurrentSamplerHandle = {};
            slot.CurrentGeneration = 0;
        }

        void RetirePending(Slot& slot)
        {
            if (!slot.PendingBuffer.IsValid() && !slot.PendingTexture.IsValid())
                return;

            RetireRecord rec{};
            rec.Buffer  = std::move(slot.PendingBuffer);
            rec.Texture = std::move(slot.PendingTexture);
            rec.Sampler = std::move(slot.PendingSampler);
            Retire.push_back(std::move(rec));

            slot.PendingBindless   = RHI::kInvalidBindlessIndex;
            slot.PendingSamplerHandle = {};
            slot.PendingGeneration = 0;
            slot.InFlight          = {};
            slot.PendingReadyFrame = 0;
            slot.CompletionKind    = PendingCompletionKind::None;
        }

        [[nodiscard]] bool IsPendingComplete(const Slot& slot,
                                             const RHI::ITransferQueue& transfer,
                                             const std::uint64_t currentFrame) noexcept
        {
            switch (slot.CompletionKind)
            {
            case PendingCompletionKind::TransferToken:
                return transfer.IsComplete(slot.InFlight);
            case PendingCompletionKind::FrameNumber:
                return currentFrame >= slot.PendingReadyFrame;
            case PendingCompletionKind::None:
                return false;
            }
            return false;
        }

        [[nodiscard]] Core::Expected<RHI::SamplerManager::SamplerLease> CreateSamplerLease(
            const RHI::SamplerDesc& desc)
        {
            if (Samplers == nullptr)
                return Core::Err<RHI::SamplerManager::SamplerLease>(Core::ErrorCode::InvalidState);

            auto samplerOr = Samplers->GetOrCreate(desc);
            if (!samplerOr.has_value())
            {
                ++Diagnostics.SamplerCreateFailures;
                return Core::Err<RHI::SamplerManager::SamplerLease>(samplerOr.error());
            }
            return samplerOr;
        }

        [[nodiscard]] bool HasFallback() const noexcept
        {
            return FallbackTexture.IsValid();
        }

        [[nodiscard]] GpuAssetView FallbackView() const noexcept
        {
            return GpuAssetView{
                .Kind = GpuAssetKind::Texture,
                .Texture = FallbackTexture.GetHandle(),
                .BindlessIdx = FallbackBindless,
                .Sampler = FallbackSampler.IsValid()
                    ? FallbackSampler.GetHandle()
                    : FallbackSamplerHandle,
                .Generation = FallbackGeneration,
            };
        }
    };

    GpuAssetCache::GpuAssetCache(RHI::BufferManager&  buffers,
                                 RHI::TextureManager& textures,
                                 RHI::ITransferQueue& transfer)
        : m_Impl(std::make_unique<Impl>(buffers, textures, nullptr, transfer))
    {
    }

    GpuAssetCache::GpuAssetCache(RHI::BufferManager&  buffers,
                                 RHI::TextureManager& textures,
                                 RHI::SamplerManager& samplers,
                                 RHI::ITransferQueue& transfer)
        : m_Impl(std::make_unique<Impl>(buffers, textures, &samplers, transfer))
    {
    }

    GpuAssetCache::~GpuAssetCache() = default;

    Core::Result GpuAssetCache::RequestUpload(const GpuBufferRequest& req)
    {
        if (!req.Id.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);
        ++m_Impl->Diagnostics.UploadRequests;

        Slot& slot = m_Impl->Slots[req.Id];

        if (slot.State == GpuAssetState::GpuUploading)
            return Core::Err(Core::ErrorCode::ResourceBusy);

        slot.Kind = GpuAssetKind::Buffer;

        auto leaseOr = m_Impl->Buffers.Create(req.Desc);
        if (!leaseOr.has_value())
        {
            ++m_Impl->Diagnostics.UploadFailures;
            if (IsUploadDeferral(leaseOr.error()))
            {
                ++m_Impl->Diagnostics.UploadDeferrals;
                return Core::Err(leaseOr.error());
            }

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
        slot.PendingReadyFrame = 0;
        slot.CompletionKind    = PendingCompletionKind::TransferToken;
        slot.State             = GpuAssetState::GpuUploading;
        return Core::Ok();
    }

    Core::Result GpuAssetCache::RequestUpload(const GpuTextureRequest& req)
    {
        if (!req.Id.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);
        ++m_Impl->Diagnostics.UploadRequests;
        ++m_Impl->Diagnostics.TextureUploadRequests;

        Slot& slot = m_Impl->Slots[req.Id];

        if (slot.State == GpuAssetState::GpuUploading)
            return Core::Err(Core::ErrorCode::ResourceBusy);

        slot.Kind = GpuAssetKind::Texture;

        RHI::SamplerManager::SamplerLease samplerLease{};
        RHI::SamplerHandle sampler = req.Sampler;
        if (!sampler.IsValid() && m_Impl->Samplers != nullptr)
        {
            auto samplerOr = m_Impl->CreateSamplerLease(req.SamplerDesc);
            if (!samplerOr.has_value())
            {
                ++m_Impl->Diagnostics.UploadFailures;
                if (IsUploadDeferral(samplerOr.error()))
                {
                    ++m_Impl->Diagnostics.UploadDeferrals;
                    return Core::Err(samplerOr.error());
                }

                m_Impl->RetirePending(slot);
                slot.State = GpuAssetState::Failed;
                return Core::Err(samplerOr.error());
            }
            samplerLease = std::move(*samplerOr);
            sampler = samplerLease.GetHandle();
        }

        auto leaseOr = m_Impl->Textures.Create(req.Desc, sampler);
        if (!leaseOr.has_value())
        {
            ++m_Impl->Diagnostics.UploadFailures;
            if (IsUploadDeferral(leaseOr.error()))
            {
                ++m_Impl->Diagnostics.UploadDeferrals;
                return Core::Err(leaseOr.error());
            }

            m_Impl->RetirePending(slot);
            slot.State = GpuAssetState::Failed;
            ++m_Impl->Diagnostics.TextureCreateFailures;
            return Core::Err(leaseOr.error());
        }

        const RHI::TextureHandle handle = leaseOr->GetHandle();
        const RHI::TransferToken token = m_Impl->Transfer.UploadTexture(
            handle, req.Bytes.data(), static_cast<std::uint64_t>(req.Bytes.size()),
            /*mipLevel=*/0, /*arrayLayer=*/0);

        const RHI::BindlessIndex bindless = m_Impl->Textures.GetBindlessIndex(handle);

        m_Impl->RetirePending(slot);

        slot.PendingTexture       = std::move(*leaseOr);
        slot.PendingSampler       = std::move(samplerLease);
        slot.PendingSamplerHandle = sampler;
        slot.PendingBuffer        = {};
        slot.PendingBindless      = bindless;
        slot.PendingGeneration    = m_Impl->NextGeneration++;
        slot.InFlight             = token;
        slot.PendingReadyFrame    = 0;
        slot.CompletionKind       = PendingCompletionKind::TransferToken;
        slot.State                = GpuAssetState::GpuUploading;
        return Core::Ok();
    }

    Core::Expected<GpuProducedTexturePendingView>
    GpuAssetCache::BeginGpuProducedTexture(const GpuProducedTextureRequest& req)
    {
        if (!req.Id.IsValid())
            return Core::Err<GpuProducedTexturePendingView>(Core::ErrorCode::InvalidArgument);
        if (!HasTextureUsage(req.Desc.Usage, RHI::TextureUsage::Sampled) ||
            !HasTextureUsage(req.Desc.Usage, RHI::TextureUsage::ColorTarget))
        {
            return Core::Err<GpuProducedTexturePendingView>(Core::ErrorCode::InvalidArgument);
        }

        std::lock_guard guard(m_Impl->Mutex);
        ++m_Impl->Diagnostics.UploadRequests;
        ++m_Impl->Diagnostics.TextureUploadRequests;

        Slot& slot = m_Impl->Slots[req.Id];

        if (slot.State == GpuAssetState::GpuUploading)
            return Core::Err<GpuProducedTexturePendingView>(Core::ErrorCode::ResourceBusy);

        slot.Kind = GpuAssetKind::Texture;

        RHI::SamplerManager::SamplerLease samplerLease{};
        RHI::SamplerHandle sampler = req.Sampler;
        if (!sampler.IsValid() && m_Impl->Samplers != nullptr)
        {
            auto samplerOr = m_Impl->CreateSamplerLease(req.SamplerDesc);
            if (!samplerOr.has_value())
            {
                ++m_Impl->Diagnostics.UploadFailures;
                if (IsUploadDeferral(samplerOr.error()))
                {
                    ++m_Impl->Diagnostics.UploadDeferrals;
                    return Core::Err<GpuProducedTexturePendingView>(samplerOr.error());
                }

                m_Impl->RetirePending(slot);
                slot.State = GpuAssetState::Failed;
                return Core::Err<GpuProducedTexturePendingView>(samplerOr.error());
            }
            samplerLease = std::move(*samplerOr);
            sampler = samplerLease.GetHandle();
        }

        auto leaseOr = m_Impl->Textures.Create(req.Desc, sampler);
        if (!leaseOr.has_value())
        {
            ++m_Impl->Diagnostics.UploadFailures;
            if (IsUploadDeferral(leaseOr.error()))
            {
                ++m_Impl->Diagnostics.UploadDeferrals;
                return Core::Err<GpuProducedTexturePendingView>(leaseOr.error());
            }

            m_Impl->RetirePending(slot);
            slot.State = GpuAssetState::Failed;
            ++m_Impl->Diagnostics.TextureCreateFailures;
            return Core::Err<GpuProducedTexturePendingView>(leaseOr.error());
        }

        const RHI::TextureHandle handle = leaseOr->GetHandle();
        const RHI::BindlessIndex bindless = m_Impl->Textures.GetBindlessIndex(handle);
        const std::uint64_t generation = m_Impl->NextGeneration++;

        m_Impl->RetirePending(slot);

        slot.PendingTexture       = std::move(*leaseOr);
        slot.PendingSampler       = std::move(samplerLease);
        slot.PendingSamplerHandle = sampler;
        slot.PendingBuffer        = {};
        slot.PendingBindless      = bindless;
        slot.PendingGeneration    = generation;
        slot.InFlight             = {};
        slot.PendingReadyFrame    = req.ReadyFrame;
        slot.CompletionKind       = req.HasReadyFrame
            ? PendingCompletionKind::FrameNumber
            : PendingCompletionKind::None;
        slot.State                = GpuAssetState::GpuUploading;

        return GpuProducedTexturePendingView{
            .Texture = handle,
            .BindlessIdx = bindless,
            .Sampler = sampler,
            .Generation = generation,
        };
    }

    Core::Result GpuAssetCache::SetGpuProducedTextureReadyFrame(
        const Assets::AssetId id,
        const std::uint64_t readyFrame)
    {
        if (!id.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return Core::Err(Core::ErrorCode::ResourceNotFound);

        Slot& slot = it->second;
        if (slot.State != GpuAssetState::GpuUploading ||
            slot.Kind != GpuAssetKind::Texture ||
            !slot.PendingTexture.IsValid() ||
            slot.CompletionKind == PendingCompletionKind::TransferToken)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        slot.PendingReadyFrame = readyFrame;
        slot.InFlight = {};
        slot.CompletionKind = PendingCompletionKind::FrameNumber;
        return Core::Ok();
    }

    Core::Result GpuAssetCache::FailGpuProducedTexture(
        const Assets::AssetId id,
        const std::uint64_t generation)
    {
        if (!id.IsValid() || generation == 0u)
            return Core::Err(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);
        const auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
            return Core::Err(Core::ErrorCode::ResourceNotFound);

        Slot& slot = it->second;
        if (slot.State != GpuAssetState::GpuUploading ||
            slot.Kind != GpuAssetKind::Texture ||
            !slot.PendingTexture.IsValid() ||
            slot.PendingGeneration != generation ||
            slot.CompletionKind == PendingCompletionKind::TransferToken)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->RetirePending(slot);
        slot.State = GpuAssetState::Failed;
        return Core::Ok();
    }

    Core::Result GpuAssetCache::InitializeFallbackTexture(const GpuTextureFallbackDesc& desc)
    {
        std::lock_guard guard(m_Impl->Mutex);

        RHI::SamplerManager::SamplerLease samplerLease{};
        RHI::SamplerHandle sampler = desc.Sampler;
        if (!sampler.IsValid())
        {
            auto samplerOr = m_Impl->CreateSamplerLease(desc.SamplerDesc);
            if (!samplerOr.has_value())
                return Core::Err(samplerOr.error());
            samplerLease = std::move(*samplerOr);
            sampler = samplerLease.GetHandle();
        }

        auto textureOr = m_Impl->Textures.Create(desc.Desc, sampler);
        if (!textureOr.has_value())
        {
            ++m_Impl->Diagnostics.TextureCreateFailures;
            return Core::Err(textureOr.error());
        }

        const RHI::TextureHandle handle = textureOr->GetHandle();
        if (!desc.Bytes.empty())
        {
            (void)m_Impl->Transfer.UploadTexture(
                handle,
                desc.Bytes.data(),
                static_cast<std::uint64_t>(desc.Bytes.size()),
                /*mipLevel=*/0,
                /*arrayLayer=*/0);
        }

        m_Impl->FallbackTexture = std::move(*textureOr);
        m_Impl->FallbackSampler = std::move(samplerLease);
        m_Impl->FallbackSamplerHandle = sampler;
        m_Impl->FallbackBindless = m_Impl->Textures.GetBindlessIndex(handle);
        m_Impl->FallbackGeneration = m_Impl->NextGeneration++;
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
        for (auto& entry : m_Impl->Slots)
        {
            Slot& slot = entry.second;
            if (slot.State != GpuAssetState::GpuUploading)
                continue;
            if (!m_Impl->IsPendingComplete(slot, m_Impl->Transfer, currentFrame))
                continue;

            // Move old current (if any) to retire queue first.
            m_Impl->RetireCurrent(slot);

            slot.CurrentBuffer        = std::move(slot.PendingBuffer);
            slot.CurrentTexture       = std::move(slot.PendingTexture);
            slot.CurrentSampler       = std::move(slot.PendingSampler);
            slot.CurrentSamplerHandle = slot.PendingSamplerHandle;
            slot.CurrentBindless      = slot.PendingBindless;
            slot.CurrentGeneration    = slot.PendingGeneration;
            slot.PendingBindless      = RHI::kInvalidBindlessIndex;
            slot.PendingSamplerHandle = {};
            slot.PendingGeneration    = 0;
            slot.InFlight             = {};
            slot.PendingReadyFrame     = 0;
            slot.CompletionKind        = PendingCompletionKind::None;
            slot.State                = GpuAssetState::Ready;
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
        view.Sampler     = slot.CurrentSampler.IsValid()
            ? slot.CurrentSampler.GetHandle()
            : slot.CurrentSamplerHandle;
        view.Generation  = slot.CurrentGeneration;
        return view;
    }

    Core::Expected<GpuAssetResolvedView> GpuAssetCache::GetViewOrFallback(Assets::AssetId id)
    {
        if (!id.IsValid())
            return Core::Err<GpuAssetResolvedView>(Core::ErrorCode::InvalidArgument);

        std::lock_guard guard(m_Impl->Mutex);

        GpuAssetResolvedView resolved{};
        auto it = m_Impl->Slots.find(id);
        if (it == m_Impl->Slots.end())
        {
            resolved.RequestedState = GpuAssetState::NotRequested;
            resolved.FallbackReason = GpuAssetFallbackReason::Missing;
        }
        else
        {
            const Slot& slot = it->second;
            resolved.RequestedState = slot.State;
            const bool hasCurrent = slot.CurrentBuffer.IsValid() || slot.CurrentTexture.IsValid();
            if (hasCurrent && slot.State == GpuAssetState::Ready)
            {
                resolved.View = GpuAssetView{
                    .Kind = slot.Kind,
                    .Buffer = slot.CurrentBuffer.GetHandle(),
                    .Texture = slot.CurrentTexture.GetHandle(),
                    .BindlessIdx = slot.CurrentBindless,
                    .Sampler = slot.CurrentSampler.IsValid()
                        ? slot.CurrentSampler.GetHandle()
                        : slot.CurrentSamplerHandle,
                    .Generation = slot.CurrentGeneration,
                };
                return resolved;
            }
            resolved.FallbackReason = slot.State == GpuAssetState::Failed
                ? GpuAssetFallbackReason::Failed
                : GpuAssetFallbackReason::Pending;
        }

        if (!m_Impl->HasFallback())
        {
            ++m_Impl->Diagnostics.FallbackMisses;
            return Core::Err<GpuAssetResolvedView>(Core::ErrorCode::ResourceNotFound);
        }

        ++m_Impl->Diagnostics.FallbackHits;
        resolved.View = m_Impl->FallbackView();
        resolved.UsedFallback = true;
        return resolved;
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

    GpuAssetCacheDiagnostics GpuAssetCache::GetDiagnostics() const
    {
        std::lock_guard guard(m_Impl->Mutex);
        GpuAssetCacheDiagnostics diagnostics = m_Impl->Diagnostics;
        diagnostics.TrackedAssets = m_Impl->Slots.size();
        diagnostics.PendingRetireRecords = m_Impl->Retire.size();
        diagnostics.FallbackTextureReady = m_Impl->HasFallback();
        diagnostics.NonEvictingCache = true;
        return diagnostics;
    }
}
