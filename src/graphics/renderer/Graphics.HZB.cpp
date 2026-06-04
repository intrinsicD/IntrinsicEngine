module;

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Graphics.HZB;

namespace Extrinsic::Graphics
{
    std::uint32_t NextPow2(std::uint32_t v) noexcept
    {
        if (v <= 1u)
            return 1u;
        // Round up to the next power of two (bit-smearing). A value with the top
        // bit already set saturates to the top bit rather than wrapping to 0.
        --v;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        if (v == 0xFFFF'FFFFu)
            return 0x8000'0000u;
        return v + 1u;
    }

    HZBDesc ComputeHZBDesc(std::uint32_t renderWidth, std::uint32_t renderHeight) noexcept
    {
        if (renderWidth == 0u || renderHeight == 0u)
            return HZBDesc{};

        HZBDesc desc{};
        desc.Width  = NextPow2(renderWidth);
        desc.Height = NextPow2(renderHeight);
        desc.Fmt    = RHI::Format::R32_FLOAT;

        // MipLevels = floor(log2(max(W,H))) + 1 over the pow2 dims, so the chain
        // halves to 1x1.
        std::uint32_t largest = desc.Width > desc.Height ? desc.Width : desc.Height;
        std::uint32_t mips     = 1u;
        while (largest > 1u)
        {
            largest >>= 1;
            ++mips;
        }
        desc.MipLevels = mips;
        return desc;
    }

    struct HZBSystem::Impl
    {
        struct RetiredLease
        {
            RHI::TextureManager::TextureLease Lease{};
            std::uint64_t                     Deadline{0u};
        };

        bool                              Initialized{false};
        RHI::TextureManager*              TextureMgr{nullptr};

        bool                              Allocated{false};
        HZBDesc                           Desc{};
        RHI::TextureManager::TextureLease Textures[2]{};
        std::uint32_t                     CurrentIndex{0u};

        std::vector<RetiredLease>         Retire{};
        HZBDiagnostics                    Diagnostics{};
    };

    HZBSystem::HZBSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    HZBSystem::~HZBSystem() = default;

    void HZBSystem::Initialize(RHI::IDevice& /*device*/, RHI::TextureManager& textureMgr)
    {
        m_Impl->Initialized = true;
        m_Impl->TextureMgr  = &textureMgr;
    }

    void HZBSystem::Shutdown()
    {
        m_Impl->Textures[0] = {};
        m_Impl->Textures[1] = {};
        m_Impl->Retire.clear();
        m_Impl->Allocated   = false;
        m_Impl->Desc        = {};
        m_Impl->CurrentIndex = 0u;
        m_Impl->TextureMgr  = nullptr;
        m_Impl->Initialized = false;
    }

    bool HZBSystem::IsInitialized() const noexcept { return m_Impl->Initialized; }

    bool HZBSystem::EnsureAllocated(std::uint32_t renderWidth,
                                    std::uint32_t renderHeight,
                                    std::uint64_t currentFrame)
    {
        if (!m_Impl->Initialized || m_Impl->TextureMgr == nullptr)
            return false;

        const HZBDesc desc = ComputeHZBDesc(renderWidth, renderHeight);
        if (!desc.IsValid())
            return false;

        if (m_Impl->Allocated && m_Impl->Desc == desc)
            return true; // unchanged — keep the retained pair

        // Retire the superseded pair (if any) through the deadline window before
        // creating the replacement, so an in-flight frame keeps sampling the old
        // texture until `Tick(...)` frees it.
        if (m_Impl->Allocated)
        {
            for (RHI::TextureManager::TextureLease& lease : m_Impl->Textures)
            {
                if (lease.IsValid())
                    m_Impl->Retire.push_back(Impl::RetiredLease{std::move(lease), currentFrame});
            }
            m_Impl->Textures[0] = {};
            m_Impl->Textures[1] = {};
            m_Impl->Allocated = false;
            ++m_Impl->Diagnostics.ReallocationCount;
        }

        const RHI::TextureDesc textureDesc{
            .Width     = desc.Width,
            .Height    = desc.Height,
            .MipLevels = desc.MipLevels,
            .Fmt       = desc.Fmt,
            // Sampled (phase-1 cull reads it) + Storage (phase-2 build writes it).
            .Usage     = RHI::TextureUsage::Sampled | RHI::TextureUsage::Storage,
            .DebugName = "HZB",
        };

        auto leaseA = m_Impl->TextureMgr->Create(textureDesc);
        if (!leaseA.has_value())
            return false;
        auto leaseB = m_Impl->TextureMgr->Create(textureDesc);
        if (!leaseB.has_value())
            return false;

        m_Impl->Textures[0] = std::move(*leaseA);
        m_Impl->Textures[1] = std::move(*leaseB);
        m_Impl->CurrentIndex = 0u;
        m_Impl->Desc        = desc;
        m_Impl->Allocated   = true;
        ++m_Impl->Diagnostics.AllocationCount;
        return true;
    }

    void HZBSystem::Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight)
    {
        if (m_Impl->Retire.empty())
        {
            m_Impl->Diagnostics.PendingRetireCount = 0u;
            return;
        }

        std::vector<Impl::RetiredLease> survivors{};
        survivors.reserve(m_Impl->Retire.size());
        for (Impl::RetiredLease& retired : m_Impl->Retire)
        {
            // Free once `framesInFlight` frames have elapsed since the deadline
            // frame, so any frame that referenced the texture has been retired.
            const bool elapsed = currentFrame >= retired.Deadline + framesInFlight;
            if (elapsed)
            {
                retired.Lease = {}; // RAII free -> IDevice::DestroyTexture
                ++m_Impl->Diagnostics.RetiredTextureCount;
            }
            else
            {
                survivors.push_back(std::move(retired));
            }
        }
        m_Impl->Retire = std::move(survivors);
        m_Impl->Diagnostics.PendingRetireCount =
            static_cast<std::uint32_t>(m_Impl->Retire.size());
    }

    void HZBSystem::AdvanceFrame() noexcept
    {
        m_Impl->CurrentIndex ^= 1u;
    }

    RHI::TextureHandle HZBSystem::CurrentHZB() const noexcept
    {
        if (!m_Impl->Allocated)
            return {};
        return m_Impl->Textures[m_Impl->CurrentIndex].GetHandle();
    }

    RHI::TextureHandle HZBSystem::PreviousHZB() const noexcept
    {
        if (!m_Impl->Allocated)
            return {};
        return m_Impl->Textures[m_Impl->CurrentIndex ^ 1u].GetHandle();
    }

    HZBDesc HZBSystem::GetAllocatedDesc() const noexcept
    {
        return m_Impl->Allocated ? m_Impl->Desc : HZBDesc{};
    }

    bool HZBSystem::IsAllocated() const noexcept { return m_Impl->Allocated; }

    HZBDiagnostics HZBSystem::GetDiagnostics() const noexcept { return m_Impl->Diagnostics; }
}
