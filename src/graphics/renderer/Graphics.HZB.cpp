module;

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
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

    namespace
    {
        [[nodiscard]] constexpr std::uint32_t CeilDiv(const std::uint32_t value,
                                                       const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] constexpr std::uint32_t MipExtent(const std::uint32_t base,
                                                        const std::uint32_t mip) noexcept
        {
            const std::uint32_t shifted = mip >= 31u ? 0u : (base >> mip);
            return shifted > 0u ? shifted : 1u;
        }
    }

    HZBBuildDispatchPlan ComputeHZBBuildDispatchPlan(const HZBDesc& desc,
                                                     HZBBuildCapabilities capabilities,
                                                     std::uint32_t tileSize)
    {
        HZBBuildDispatchPlan plan{};
        plan.Mode = capabilities.SupportsSinglePassMipChain
            ? HZBBuildMode::SinglePassMipChain
            : HZBBuildMode::PerMipDispatch;
        plan.Desc = desc;

        if (!desc.IsValid() || tileSize == 0u)
            return plan;

        if (plan.Mode == HZBBuildMode::SinglePassMipChain)
        {
            plan.Dispatches.push_back(HZBBuildDispatchDesc{
                .TargetMip = 0u,
                .SourceMip = 0u,
                .ReadsDepthSource = true,
                .TargetWidth = desc.Width,
                .TargetHeight = desc.Height,
                .GroupCountX = CeilDiv(desc.Width, tileSize),
                .GroupCountY = CeilDiv(desc.Height, tileSize),
                .GroupCountZ = 1u,
            });
            return plan;
        }

        plan.Dispatches.reserve(desc.MipLevels);
        for (std::uint32_t mip = 0u; mip < desc.MipLevels; ++mip)
        {
            const std::uint32_t width = MipExtent(desc.Width, mip);
            const std::uint32_t height = MipExtent(desc.Height, mip);
            plan.Dispatches.push_back(HZBBuildDispatchDesc{
                .TargetMip = mip,
                .SourceMip = mip == 0u ? 0u : mip - 1u,
                .ReadsDepthSource = mip == 0u,
                .TargetWidth = width,
                .TargetHeight = height,
                .GroupCountX = CeilDiv(width, tileSize),
                .GroupCountY = CeilDiv(height, tileSize),
                .GroupCountZ = 1u,
            });
        }
        return plan;
    }

    bool RecordHZBBuild(RHI::ICommandContext& cmd,
                        RHI::PipelineHandle pipeline,
                        RHI::TextureHandle hzbTexture,
                        const HZBBuildDispatchPlan& plan)
    {
        if (!pipeline.IsValid() || !hzbTexture.IsValid() || !plan.IsValid())
            return false;

        cmd.BindPipeline(pipeline);
        for (std::size_t dispatchIndex = 0u; dispatchIndex < plan.Dispatches.size(); ++dispatchIndex)
        {
            const HZBBuildDispatchDesc& dispatch = plan.Dispatches[dispatchIndex];
            const HZBBuildPushConstants pc{
                .RenderWidth = plan.Desc.Width,
                .RenderHeight = plan.Desc.Height,
                .TargetMip = dispatch.TargetMip,
                .SourceMip = dispatch.SourceMip,
                .MipCount = plan.Desc.MipLevels,
                .BuildMode = static_cast<std::uint32_t>(plan.Mode),
                .TargetWidth = dispatch.TargetWidth,
                .TargetHeight = dispatch.TargetHeight,
            };
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
            cmd.Dispatch(dispatch.GroupCountX, dispatch.GroupCountY, dispatch.GroupCountZ);

            if (plan.Mode == HZBBuildMode::PerMipDispatch && dispatchIndex + 1u < plan.Dispatches.size())
            {
                const RHI::TextureBarrierDesc barrier{
                    .Texture = hzbTexture,
                    .BeforeLayout = RHI::TextureLayout::General,
                    .AfterLayout = RHI::TextureLayout::General,
                    .BeforeAccess = RHI::MemoryAccess::ShaderWrite,
                    .AfterAccess = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                };
                cmd.SubmitBarriers(RHI::BarrierBatchDesc{
                    .TextureBarriers = std::span<const RHI::TextureBarrierDesc>{&barrier, 1u},
                });
            }
        }
        return true;
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
