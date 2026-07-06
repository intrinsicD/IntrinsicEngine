module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <tuple>

module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;
import :Pass;
import :Compiler;
import :Resources;
import :TransientAllocator;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.TextureUpload;

namespace Extrinsic::Graphics
{
    struct RenderGraphCompilerScratch;
    using RenderGraphCompilerScratchDeleter = void (*)(RenderGraphCompilerScratch*) noexcept;

    [[nodiscard]] RenderGraphCompilerScratch* CreateRenderGraphCompilerScratch();
    void DestroyRenderGraphCompilerScratch(RenderGraphCompilerScratch* scratch) noexcept;
    [[nodiscard]] Core::Expected<CompiledRenderGraph> CompileRenderGraphWithScratch(
        std::span<const RenderPassRecord> passes,
        std::span<const TextureResourceDesc> textures,
        std::span<const BufferResourceDesc> buffers,
        RenderGraphCompilerScratch& scratch,
        RenderGraphValidationResult* validationOut = nullptr);

    namespace
    {
        [[nodiscard]] constexpr bool TextureUsageIsWrite(const TextureUsage usage)
        {
            return usage == TextureUsage::ColorAttachmentWrite || usage == TextureUsage::DepthWrite ||
                   usage == TextureUsage::ShaderWrite || usage == TextureUsage::TransferDst;
        }

        [[nodiscard]] constexpr bool TextureStateAllowsWrite(const TextureState state)
        {
            return state == TextureState::ColorAttachmentWrite || state == TextureState::DepthWrite ||
                   state == TextureState::ShaderWrite || state == TextureState::TransferDst ||
                   state == TextureState::Present;
        }

        [[nodiscard]] constexpr bool BufferUsageIsWrite(const BufferUsage usage)
        {
            return usage == BufferUsage::ShaderWrite || usage == BufferUsage::TransferDst;
        }

        [[nodiscard]] constexpr bool BufferStateAllowsWrite(const BufferState state)
        {
            return state == BufferState::ShaderWrite || state == BufferState::TransferDst;
        }

        inline constexpr std::uint32_t kInvalidTransientPlacementResource =
            std::numeric_limits<std::uint32_t>::max();
        inline constexpr std::uint64_t kDefaultTransientPlacementAlignmentBytes = 256u;

        [[nodiscard]] constexpr std::uint64_t AlignUp(const std::uint64_t value,
                                                       const std::uint64_t alignment) noexcept
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const std::uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }

        [[nodiscard]] constexpr std::uint64_t AlignedTransientSize(const std::uint64_t sizeBytes,
                                                                   const std::uint64_t alignmentBytes) noexcept
        {
            return sizeBytes == 0u ? 0u : AlignUp(sizeBytes, alignmentBytes);
        }

        void SortBarrierPacketsByPass(std::vector<BarrierPacket>& packets)
        {
            std::ranges::stable_sort(packets, [](const BarrierPacket& lhs, const BarrierPacket& rhs) {
                return std::tuple{lhs.PassIndex, BarrierPacketStageSortKey(lhs.Stage)} <
                       std::tuple{rhs.PassIndex, BarrierPacketStageSortKey(rhs.Stage)};
            });
        }

        [[nodiscard]] BarrierPacket& FindOrCreateBarrierPacket(std::vector<BarrierPacket>& packets,
                                                               const std::uint32_t passIndex,
                                                               const BarrierPacketStage stage)
        {
            const auto it = std::ranges::find_if(packets, [passIndex, stage](const BarrierPacket& packet) {
                return packet.PassIndex == passIndex && packet.Stage == stage;
            });
            if (it != packets.end())
            {
                return *it;
            }

            packets.push_back(BarrierPacket{
                .Kind = BarrierKind::AliasReuse,
                .PassIndex = passIndex,
                .Stage = stage,
            });
            return packets.back();
        }

        struct TransientPlacementItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
            std::uint64_t SizeBytes = 0u;
            std::uint64_t AlignmentBytes = 1u;
        };

        struct TransientPlacementPlan
        {
            std::vector<TransientResourcePlacement> Placements{};
            std::uint64_t PeakBytes = 0u;
        };

        template <typename EmitAliasReuseHazard>
        [[nodiscard]] TransientPlacementPlan BuildTransientPlacementPlan(
            const std::span<const TransientPlacementItem> items,
            const bool aliasingEnabled,
            EmitAliasReuseHazard&& emitAliasReuseHazard)
        {
            struct ActiveRange
            {
                std::uint32_t ResourceIndex = 0u;
                std::uint32_t LastUsePass = 0u;
                std::uint32_t BlockIndex = 0u;
                std::uint64_t OffsetBytes = 0u;
                std::uint64_t SizeBytes = 0u;
            };

            struct FreeRange
            {
                std::uint32_t BlockIndex = 0u;
                std::uint64_t OffsetBytes = 0u;
                std::uint64_t SizeBytes = 0u;
                std::uint32_t PreviousResourceIndex = kInvalidTransientPlacementResource;
            };

            std::vector<ActiveRange> activeRanges{};
            std::vector<FreeRange> freeRanges{};
            std::uint64_t blockSize = 0u;
            TransientPlacementPlan plan{};
            plan.Placements.reserve(items.size());

            auto sortFreeRanges = [&]() {
                std::ranges::sort(freeRanges, [](const FreeRange& lhs, const FreeRange& rhs) {
                    return std::tuple{lhs.BlockIndex, lhs.OffsetBytes, lhs.SizeBytes, lhs.PreviousResourceIndex} <
                           std::tuple{rhs.BlockIndex, rhs.OffsetBytes, rhs.SizeBytes, rhs.PreviousResourceIndex};
                });
            };

            for (const TransientPlacementItem& item : items)
            {
                for (std::size_t activeIndex = 0u; activeIndex < activeRanges.size();)
                {
                    const ActiveRange& active = activeRanges[activeIndex];
                    if (active.LastUsePass < item.FirstUsePass)
                    {
                        if (aliasingEnabled && active.SizeBytes != 0u)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = active.BlockIndex,
                                .OffsetBytes = active.OffsetBytes,
                                .SizeBytes = active.SizeBytes,
                                .PreviousResourceIndex = active.ResourceIndex,
                            });
                        }
                        activeRanges.erase(activeRanges.begin() + static_cast<std::ptrdiff_t>(activeIndex));
                        continue;
                    }
                    ++activeIndex;
                }

                sortFreeRanges();

                bool placedInFreeRange = false;
                std::uint32_t blockIndex = 0u;
                std::uint64_t offsetBytes = 0u;

                if (aliasingEnabled && item.SizeBytes != 0u)
                {
                    for (std::size_t rangeIndex = 0u; rangeIndex < freeRanges.size(); ++rangeIndex)
                    {
                        const FreeRange range = freeRanges[rangeIndex];
                        const std::uint64_t alignedOffset = AlignUp(range.OffsetBytes, item.AlignmentBytes);
                        const std::uint64_t rangeEnd = range.OffsetBytes + range.SizeBytes;
                        if (alignedOffset > rangeEnd || item.SizeBytes > rangeEnd - alignedOffset)
                        {
                            continue;
                        }

                        blockIndex = range.BlockIndex;
                        offsetBytes = alignedOffset;
                        placedInFreeRange = true;
                        freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(rangeIndex));

                        if (range.OffsetBytes < alignedOffset)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = range.BlockIndex,
                                .OffsetBytes = range.OffsetBytes,
                                .SizeBytes = alignedOffset - range.OffsetBytes,
                                .PreviousResourceIndex = range.PreviousResourceIndex,
                            });
                        }

                        const std::uint64_t allocationEnd = alignedOffset + item.SizeBytes;
                        if (allocationEnd < rangeEnd)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = range.BlockIndex,
                                .OffsetBytes = allocationEnd,
                                .SizeBytes = rangeEnd - allocationEnd,
                                .PreviousResourceIndex = range.PreviousResourceIndex,
                            });
                        }

                        if (range.PreviousResourceIndex != kInvalidTransientPlacementResource)
                        {
                            emitAliasReuseHazard(range.PreviousResourceIndex,
                                                 item.ResourceIndex,
                                                 item.FirstUsePass,
                                                 blockIndex,
                                                 offsetBytes,
                                                 item.SizeBytes);
                        }
                        break;
                    }
                }

                if (!placedInFreeRange)
                {
                    offsetBytes = AlignUp(blockSize, item.AlignmentBytes);
                    blockSize = offsetBytes + item.SizeBytes;
                }

                plan.Placements.push_back(TransientResourcePlacement{
                    .ResourceIndex = item.ResourceIndex,
                    .BlockIndex = blockIndex,
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = item.SizeBytes,
                    .AlignmentBytes = item.AlignmentBytes,
                    .FirstUsePass = item.FirstUsePass,
                    .LastUsePass = item.LastUsePass,
                });

                activeRanges.push_back(ActiveRange{
                    .ResourceIndex = item.ResourceIndex,
                    .LastUsePass = item.LastUsePass,
                    .BlockIndex = blockIndex,
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = item.SizeBytes,
                });
            }

            plan.PeakBytes = blockSize;
            std::ranges::sort(plan.Placements, [](const TransientResourcePlacement& lhs,
                                                  const TransientResourcePlacement& rhs) {
                return lhs.ResourceIndex < rhs.ResourceIndex;
            });
            return plan;
        }
    }

    struct RenderGraph::Impl
    {
        std::vector<RenderPassRecord> Passes{};
        std::vector<RenderPassRecord> RecycledPasses{};
        std::vector<TextureResourceDesc> Textures{};
        std::vector<BufferResourceDesc> Buffers{};
        TransientAllocator Transients{};
        bool TransientAliasingEnabled = true;
        std::uint32_t Generation = 1;
        RenderGraphValidationResult LastCompileValidationResult{};
        std::unique_ptr<RenderGraphCompilerScratch, RenderGraphCompilerScratchDeleter> CompileScratch{
            nullptr,
            DestroyRenderGraphCompilerScratch};
    };

    void ResetPassRecordForReuse(RenderPassRecord& record)
    {
        record.Name.clear();
        record.Id = {};
        record.SideEffect = false;
        record.Queue = RenderQueue::Graphics;
        record.TextureAccesses.clear();
        record.BufferAccesses.clear();
        record.ExplicitDependencies.clear();
        record.HasRenderPassDesc = false;
        record.RenderPass = {};
        record.HasValidationError = false;
    }

    RenderGraphBuilder::RenderGraphBuilder(
        RenderPassRecord& record,
        std::move_only_function<bool(TextureRef, TextureUsage, bool)>&& textureValidator,
        std::move_only_function<bool(BufferRef, BufferUsage, bool)>&& bufferValidator)
        : m_Record(&record), m_TextureValidator(std::move(textureValidator)), m_BufferValidator(std::move(bufferValidator))
    {
    }

    TextureRef RenderGraphBuilder::Read(const TextureRef ref, const TextureUsage usage)
    {
        const bool ok = m_TextureValidator && m_TextureValidator(ref, usage, false);
        if (!ok)
        {
            m_Record->HasValidationError = true;
            return {};
        }
        m_Record->TextureAccesses.push_back(TextureAccess{.Ref = ref, .Usage = usage, .Write = false});
        return ref;
    }

    TextureRef RenderGraphBuilder::Write(const TextureRef ref, const TextureUsage usage)
    {
        const bool ok = m_TextureValidator && m_TextureValidator(ref, usage, true);
        if (!ok)
        {
            m_Record->HasValidationError = true;
            return {};
        }
        m_Record->TextureAccesses.push_back(TextureAccess{.Ref = ref, .Usage = usage, .Write = true});
        return ref;
    }

    BufferRef RenderGraphBuilder::Read(const BufferRef ref, const BufferUsage usage)
    {
        const bool ok = m_BufferValidator && m_BufferValidator(ref, usage, false);
        if (!ok)
        {
            m_Record->HasValidationError = true;
            return {};
        }
        m_Record->BufferAccesses.push_back(BufferAccess{.Ref = ref, .Usage = usage, .Write = false});
        return ref;
    }

    BufferRef RenderGraphBuilder::Write(const BufferRef ref, const BufferUsage usage)
    {
        const bool ok = m_BufferValidator && m_BufferValidator(ref, usage, true);
        if (!ok)
        {
            m_Record->HasValidationError = true;
            return {};
        }
        m_Record->BufferAccesses.push_back(BufferAccess{.Ref = ref, .Usage = usage, .Write = true});
        return ref;
    }

    void RenderGraphBuilder::DependsOn(const PassRef dependency)
    {
        if (!dependency.IsValid())
        {
            m_Record->HasValidationError = true;
            return;
        }
        m_Record->ExplicitDependencies.push_back(dependency);
    }

    void RenderGraphBuilder::SetQueue(const RenderQueue queue)
    {
        m_Record->Queue = queue;
    }

    void RenderGraphBuilder::SetRenderPass(const RHI::RenderPassDesc& desc)
    {
        m_Record->RenderPass = desc;
        m_Record->HasRenderPassDesc = true;
    }

    void RenderGraphBuilder::SideEffect()
    {
        m_Record->SideEffect = true;
    }

    RenderGraph::RenderGraph() = default;
    RenderGraph::~RenderGraph() = default;
    RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

    PassRef RenderGraph::AddPass(std::string name)
    {
        return AddPass(std::move(name), false);
    }

    PassRef RenderGraph::AddPass(std::string name, const bool sideEffect)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }

        const auto index = static_cast<std::uint32_t>(m_Impl->Passes.size());
        if (!m_Impl->RecycledPasses.empty())
        {
            RenderPassRecord record = std::move(m_Impl->RecycledPasses.back());
            m_Impl->RecycledPasses.pop_back();
            ResetPassRecordForReuse(record);
            record.Name = std::move(name);
            record.SideEffect = sideEffect;
            m_Impl->Passes.push_back(std::move(record));
        }
        else
        {
            m_Impl->Passes.push_back(RenderPassRecord{.Name = std::move(name), .SideEffect = sideEffect});
        }
        return PassRef{.Index = index, .Generation = m_Impl->Generation};
    }

    PassRef RenderGraph::AddPass(std::string name,
                                 std::move_only_function<void(RenderGraphBuilder&)> setup)
    {
        return AddPass(std::move(name), std::move(setup), false);
    }

    PassRef RenderGraph::AddPass(std::string name,
                                 std::move_only_function<void(RenderGraphBuilder&)> setup,
                                 const bool sideEffect)
    {
        const PassRef pass = AddPass(std::move(name), sideEffect);
        RenderPassRecord& record = m_Impl->Passes[pass.Index];
        RenderGraphBuilder builder(
            record,
            [this, sideEffect](const TextureRef ref, const TextureUsage usage, const bool write) {
                if (!ValidateTextureRef(ref).has_value())
                {
                    return false;
                }

                if (usage == TextureUsage::Present && write)
                {
                    return false;
                }

                if (write && !TextureUsageIsWrite(usage))
                {
                    return false;
                }

                const TextureResourceDesc* desc = GetTextureDesc(ref);
                if (!desc)
                {
                    return false;
                }

                if (desc->IsBackbuffer && write && !sideEffect)
                {
                    return false;
                }

                if (desc->Imported && write)
                {
                    const bool writableContract = TextureStateAllowsWrite(desc->InitialState) ||
                                                  TextureStateAllowsWrite(desc->FinalState);
                    if (!writableContract)
                    {
                        return false;
                    }
                }

                return true;
            },
            [this](const BufferRef ref, const BufferUsage usage, const bool write) {
                if (!ValidateBufferRef(ref).has_value())
                {
                    return false;
                }

                if (write && !BufferUsageIsWrite(usage))
                {
                    return false;
                }

                const BufferResourceDesc* desc = GetBufferDesc(ref);
                if (!desc)
                {
                    return false;
                }

                if (desc->Imported && write)
                {
                    const bool writableContract = BufferStateAllowsWrite(desc->InitialState) ||
                                                  BufferStateAllowsWrite(desc->FinalState);
                    if (!writableContract)
                    {
                        return false;
                    }
                }

                return true;
            });

        setup(builder);
        return pass;
    }

    TextureRef RenderGraph::ImportBackbuffer(std::string name, const RHI::TextureHandle handle)
    {
        const TextureRef ref = ImportTexture(std::move(name), handle, TextureState::Undefined, TextureState::Present);
        if (m_Impl && ref.Index < m_Impl->Textures.size())
        {
            m_Impl->Textures[ref.Index].IsBackbuffer = true;
        }
        return ref;
    }

    TextureRef RenderGraph::ImportTexture(std::string name,
                                          const RHI::TextureHandle handle,
                                          const TextureState initial,
                                          const TextureState finalState)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }

        const auto index = static_cast<std::uint32_t>(m_Impl->Textures.size());
        TextureResourceDesc& texture = m_Impl->Textures.emplace_back();
        texture.Name = std::move(name);
        texture.Imported = true;
        texture.IsBackbuffer = false;
        texture.AliasEligible = false;
        texture.InitialState = initial;
        texture.FinalState = finalState;
        texture.ImportedHandle = handle;
        texture.Generation = m_Impl->Generation;
        return TextureRef{.Index = index, .Generation = m_Impl->Generation};
    }

    BufferRef RenderGraph::ImportBuffer(std::string name,
                                        const RHI::BufferHandle handle,
                                        const BufferState initial,
                                        const BufferState finalState)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }

        const auto index = static_cast<std::uint32_t>(m_Impl->Buffers.size());
        BufferResourceDesc& buffer = m_Impl->Buffers.emplace_back();
        buffer.Name = std::move(name);
        buffer.Imported = true;
        buffer.AliasEligible = false;
        buffer.InitialState = initial;
        buffer.FinalState = finalState;
        buffer.ImportedHandle = handle;
        buffer.Generation = m_Impl->Generation;
        return BufferRef{.Index = index, .Generation = m_Impl->Generation};
    }

    TextureRef RenderGraph::CreateTexture(std::string name, const RHI::TextureDesc& desc)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }

        const auto index = static_cast<std::uint32_t>(m_Impl->Textures.size());
        TextureResourceDesc& texture = m_Impl->Textures.emplace_back();
        texture.Name = std::move(name);
        texture.Imported = false;
        texture.IsBackbuffer = false;
        texture.AliasEligible = true;
        texture.InitialState = TextureState::Undefined;
        texture.FinalState = TextureState::Undefined;
        texture.Desc = desc;
        texture.Generation = m_Impl->Generation;
        return TextureRef{.Index = index, .Generation = m_Impl->Generation};
    }

    BufferRef RenderGraph::CreateBuffer(std::string name, const RHI::BufferDesc& desc)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }

        const auto index = static_cast<std::uint32_t>(m_Impl->Buffers.size());
        BufferResourceDesc& buffer = m_Impl->Buffers.emplace_back();
        buffer.Name = std::move(name);
        buffer.Imported = false;
        buffer.AliasEligible = true;
        buffer.InitialState = BufferState::Undefined;
        buffer.FinalState = BufferState::Undefined;
        buffer.Desc = desc;
        buffer.Generation = m_Impl->Generation;
        return BufferRef{.Index = index, .Generation = m_Impl->Generation};
    }

    Core::Expected<CompiledRenderGraph> RenderGraph::Compile()
    {
        if (!m_Impl)
        {
            return RenderGraphCompiler::Compile({}, {}, {});
        }

        m_Impl->LastCompileValidationResult.Findings.clear();

        const auto hasValidationFailure = std::ranges::any_of(
            m_Impl->Passes, [](const RenderPassRecord& pass) { return pass.HasValidationError; });
        if (hasValidationFailure)
        {
            const auto failedIt = std::ranges::find_if(
                m_Impl->Passes, [](const RenderPassRecord& pass) { return pass.HasValidationError; });
            const std::string passName = (failedIt != m_Impl->Passes.end()) ? failedIt->Name : "<unknown>";
            std::string message = "RenderGraph validation failed while recording pass resource usage: pass=\"" +
                                  passName + "\".";
            m_Impl->LastCompileValidationResult.Findings.push_back(RenderGraphValidationFinding{
                .Severity = RenderGraphValidationSeverity::Error,
                .Code = RenderGraphValidationCode::InvalidTextureAccess,
                .Message = std::move(message),
                .PassIndex = failedIt != m_Impl->Passes.end()
                                 ? static_cast<std::uint32_t>(std::distance(m_Impl->Passes.begin(), failedIt))
                                 : std::numeric_limits<std::uint32_t>::max(),
                .PassName = passName,
            });
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        const auto invalidPresentPassIt = std::ranges::find_if(m_Impl->Passes, [this](const RenderPassRecord& pass) {
            return std::ranges::any_of(pass.TextureAccesses, [this](const TextureAccess& access) {
                if (access.Usage != TextureUsage::Present)
                {
                    return false;
                }
                const TextureResourceDesc* desc = GetTextureDesc(access.Ref);
                return (desc == nullptr) || !desc->Imported || !desc->IsBackbuffer;
            });
        });
        if (invalidPresentPassIt != m_Impl->Passes.end())
        {
            std::string message = "RenderGraph present pass must target an imported backbuffer texture: pass=\"" +
                                  invalidPresentPassIt->Name + "\".";
            m_Impl->LastCompileValidationResult.Findings.push_back(RenderGraphValidationFinding{
                .Severity = RenderGraphValidationSeverity::Error,
                .Code = RenderGraphValidationCode::InvalidTextureAccess,
                .Message = std::move(message),
                .PassIndex = static_cast<std::uint32_t>(std::distance(m_Impl->Passes.begin(), invalidPresentPassIt)),
                .PassName = invalidPresentPassIt->Name,
            });
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        if (!m_Impl->CompileScratch)
        {
            m_Impl->CompileScratch.reset(CreateRenderGraphCompilerScratch());
        }

        auto compiled = CompileRenderGraphWithScratch(
            m_Impl->Passes,
            m_Impl->Textures,
            m_Impl->Buffers,
            *m_Impl->CompileScratch,
            &m_Impl->LastCompileValidationResult);
        if (!compiled.has_value())
        {
            return compiled;
        }

        m_Impl->LastCompileValidationResult.Findings = compiled->ValidationFindings;

        m_Impl->Transients.ResetFrame();

        struct TextureAllocItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
            std::uint64_t SizeBytes = 0u;
            std::uint64_t AlignmentBytes = kDefaultTransientPlacementAlignmentBytes;
        };
        struct BufferAllocItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
            std::uint64_t SizeBytes = 0u;
            std::uint64_t AlignmentBytes = kDefaultTransientPlacementAlignmentBytes;
        };
        std::vector<TextureAllocItem> texturesToAllocate{};
        std::vector<BufferAllocItem> buffersToAllocate{};
        texturesToAllocate.reserve(m_Impl->Textures.size());
        buffersToAllocate.reserve(m_Impl->Buffers.size());

        for (std::uint32_t i = 0; i < m_Impl->Textures.size(); ++i)
        {
            if (m_Impl->Textures[i].Imported || !compiled->TextureLifetimes[i].HasUse)
            {
                continue;
            }
            const std::uint64_t sizeBytes = AlignedTransientSize(
                RHI::EstimateTextureStorageBytes(m_Impl->Textures[i].Desc),
                kDefaultTransientPlacementAlignmentBytes);
            texturesToAllocate.push_back(TextureAllocItem{
                .ResourceIndex = i,
                .FirstUsePass = compiled->TextureLifetimes[i].FirstUsePass,
                .LastUsePass = compiled->TextureLifetimes[i].LastUsePass,
                .SizeBytes = sizeBytes,
                .AlignmentBytes = kDefaultTransientPlacementAlignmentBytes,
            });
            compiled->TransientNaiveMemoryEstimateBytes += sizeBytes;
        }
        for (std::uint32_t i = 0; i < m_Impl->Buffers.size(); ++i)
        {
            if (m_Impl->Buffers[i].Imported || !compiled->BufferLifetimes[i].HasUse)
            {
                continue;
            }
            const std::uint64_t sizeBytes = AlignedTransientSize(
                m_Impl->Buffers[i].Desc.SizeBytes,
                kDefaultTransientPlacementAlignmentBytes);
            buffersToAllocate.push_back(BufferAllocItem{
                .ResourceIndex = i,
                .FirstUsePass = compiled->BufferLifetimes[i].FirstUsePass,
                .LastUsePass = compiled->BufferLifetimes[i].LastUsePass,
                .SizeBytes = sizeBytes,
                .AlignmentBytes = kDefaultTransientPlacementAlignmentBytes,
            });
            compiled->TransientNaiveMemoryEstimateBytes += sizeBytes;
        }

        auto byFirstThenIndex = [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.FirstUsePass, lhs.ResourceIndex) < std::tie(rhs.FirstUsePass, rhs.ResourceIndex);
        };
        std::ranges::sort(texturesToAllocate, byFirstThenIndex);
        std::ranges::sort(buffersToAllocate, byFirstThenIndex);

        if (m_Impl->TransientAliasingEnabled)
        {
            struct ActiveTextureAllocation
            {
                std::uint32_t LastUsePass = 0u;
                RHI::TextureHandle Handle{};
            };
            std::vector<ActiveTextureAllocation> activeTextures{};
            for (const TextureAllocItem& item : texturesToAllocate)
            {
                for (std::size_t activeIndex = 0; activeIndex < activeTextures.size();)
                {
                    if (activeTextures[activeIndex].LastUsePass < item.FirstUsePass)
                    {
                        m_Impl->Transients.ReleaseTexture(activeTextures[activeIndex].Handle);
                        activeTextures.erase(activeTextures.begin() + static_cast<std::ptrdiff_t>(activeIndex));
                        continue;
                    }
                    ++activeIndex;
                }

                const auto handle = m_Impl->Transients.AcquireTexture(m_Impl->Textures[item.ResourceIndex].Desc);
                compiled->TextureHandles[item.ResourceIndex] = handle;
                ++compiled->TransientTextureCount;
                activeTextures.push_back(ActiveTextureAllocation{
                    .LastUsePass = item.LastUsePass,
                    .Handle = handle,
                });
            }

            struct ActiveBufferAllocation
            {
                std::uint32_t LastUsePass = 0u;
                RHI::BufferHandle Handle{};
            };
            std::vector<ActiveBufferAllocation> activeBuffers{};
            for (const BufferAllocItem& item : buffersToAllocate)
            {
                for (std::size_t activeIndex = 0; activeIndex < activeBuffers.size();)
                {
                    if (activeBuffers[activeIndex].LastUsePass < item.FirstUsePass)
                    {
                        m_Impl->Transients.ReleaseBuffer(activeBuffers[activeIndex].Handle);
                        activeBuffers.erase(activeBuffers.begin() + static_cast<std::ptrdiff_t>(activeIndex));
                        continue;
                    }
                    ++activeIndex;
                }

                const auto handle = m_Impl->Transients.AcquireBuffer(m_Impl->Buffers[item.ResourceIndex].Desc);
                compiled->BufferHandles[item.ResourceIndex] = handle;
                ++compiled->TransientBufferCount;
                activeBuffers.push_back(ActiveBufferAllocation{
                    .LastUsePass = item.LastUsePass,
                    .Handle = handle,
                });
            }
        }
        else
        {
            for (const TextureAllocItem& item : texturesToAllocate)
            {
                compiled->TextureHandles[item.ResourceIndex] = m_Impl->Transients.AcquireTexture(m_Impl->Textures[item.ResourceIndex].Desc);
                ++compiled->TransientTextureCount;
            }
            for (const BufferAllocItem& item : buffersToAllocate)
            {
                compiled->BufferHandles[item.ResourceIndex] = m_Impl->Transients.AcquireBuffer(m_Impl->Buffers[item.ResourceIndex].Desc);
                ++compiled->TransientBufferCount;
            }
        }

        std::vector<TransientPlacementItem> texturePlacementItems{};
        texturePlacementItems.reserve(texturesToAllocate.size());
        for (const TextureAllocItem& item : texturesToAllocate)
        {
            texturePlacementItems.push_back(TransientPlacementItem{
                .ResourceIndex = item.ResourceIndex,
                .FirstUsePass = item.FirstUsePass,
                .LastUsePass = item.LastUsePass,
                .SizeBytes = item.SizeBytes,
                .AlignmentBytes = item.AlignmentBytes,
            });
        }

        std::vector<TransientPlacementItem> bufferPlacementItems{};
        bufferPlacementItems.reserve(buffersToAllocate.size());
        for (const BufferAllocItem& item : buffersToAllocate)
        {
            bufferPlacementItems.push_back(TransientPlacementItem{
                .ResourceIndex = item.ResourceIndex,
                .FirstUsePass = item.FirstUsePass,
                .LastUsePass = item.LastUsePass,
                .SizeBytes = item.SizeBytes,
                .AlignmentBytes = item.AlignmentBytes,
            });
        }

        auto emitTextureAliasReuseHazard =
            [&compiled](const std::uint32_t previousResourceIndex,
                        const std::uint32_t resourceIndex,
                        const std::uint32_t executionRank,
                        const std::uint32_t blockIndex,
                        const std::uint64_t offsetBytes,
                        const std::uint64_t sizeBytes)
        {
            const std::uint32_t passIndex =
                executionRank < compiled->TopologicalOrder.size()
                    ? compiled->TopologicalOrder[executionRank]
                    : executionRank;
            BarrierPacket& packet = FindOrCreateBarrierPacket(
                compiled->BarrierPackets, passIndex, BarrierPacketStage::BeforePass);
            packet.TextureAliasReuseBarriers.push_back(TextureAliasReuseBarrierPacket{
                .PreviousTextureIndex = previousResourceIndex,
                .TextureIndex = resourceIndex,
                .BlockIndex = blockIndex,
                .OffsetBytes = offsetBytes,
                .SizeBytes = sizeBytes,
            });
        };

        auto emitBufferAliasReuseHazard =
            [&compiled](const std::uint32_t previousResourceIndex,
                        const std::uint32_t resourceIndex,
                        const std::uint32_t executionRank,
                        const std::uint32_t blockIndex,
                        const std::uint64_t offsetBytes,
                        const std::uint64_t sizeBytes)
        {
            const std::uint32_t passIndex =
                executionRank < compiled->TopologicalOrder.size()
                    ? compiled->TopologicalOrder[executionRank]
                    : executionRank;
            BarrierPacket& packet = FindOrCreateBarrierPacket(
                compiled->BarrierPackets, passIndex, BarrierPacketStage::BeforePass);
            packet.BufferAliasReuseBarriers.push_back(BufferAliasReuseBarrierPacket{
                .PreviousBufferIndex = previousResourceIndex,
                .BufferIndex = resourceIndex,
                .BlockIndex = blockIndex,
                .OffsetBytes = offsetBytes,
                .SizeBytes = sizeBytes,
            });
        };

        TransientPlacementPlan texturePlan =
            BuildTransientPlacementPlan(texturePlacementItems, m_Impl->TransientAliasingEnabled, emitTextureAliasReuseHazard);
        TransientPlacementPlan bufferPlan =
            BuildTransientPlacementPlan(bufferPlacementItems, m_Impl->TransientAliasingEnabled, emitBufferAliasReuseHazard);
        compiled->TextureTransientPlacements = std::move(texturePlan.Placements);
        compiled->BufferTransientPlacements = std::move(bufferPlan.Placements);
        compiled->TransientPlacedPeakMemoryEstimateBytes = texturePlan.PeakBytes + bufferPlan.PeakBytes;
        compiled->TransientMemoryEstimateBytes = compiled->TransientPlacedPeakMemoryEstimateBytes;
        SortBarrierPacketsByPass(compiled->BarrierPackets);

        return compiled;
    }

    const RenderGraphValidationResult& RenderGraph::GetLastCompileValidationResult() const
    {
        static const RenderGraphValidationResult empty{};
        if (!m_Impl)
        {
            return empty;
        }
        return m_Impl->LastCompileValidationResult;
    }

    Core::Result RenderGraph::ValidateTextureRef(const TextureRef ref) const
    {
        if (!m_Impl || !ref.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        if (ref.Index >= m_Impl->Textures.size())
        {
            return Core::Err(Core::ErrorCode::OutOfRange);
        }
        if (m_Impl->Textures[ref.Index].Generation != ref.Generation)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    Core::Result RenderGraph::ValidateBufferRef(const BufferRef ref) const
    {
        if (!m_Impl || !ref.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        if (ref.Index >= m_Impl->Buffers.size())
        {
            return Core::Err(Core::ErrorCode::OutOfRange);
        }
        if (m_Impl->Buffers[ref.Index].Generation != ref.Generation)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    const TextureResourceDesc* RenderGraph::GetTextureDesc(const TextureRef ref) const
    {
        if (!ValidateTextureRef(ref).has_value())
        {
            return nullptr;
        }
        return &m_Impl->Textures[ref.Index];
    }

    const BufferResourceDesc* RenderGraph::GetBufferDesc(const BufferRef ref) const
    {
        if (!ValidateBufferRef(ref).has_value())
        {
            return nullptr;
        }
        return &m_Impl->Buffers[ref.Index];
    }

    const TextureResourceDesc* RenderGraph::GetTextureDescByIndex(const std::uint32_t index) const
    {
        if (!m_Impl || index >= m_Impl->Textures.size())
        {
            return nullptr;
        }
        return &m_Impl->Textures[index];
    }

    const BufferResourceDesc* RenderGraph::GetBufferDescByIndex(const std::uint32_t index) const
    {
        if (!m_Impl || index >= m_Impl->Buffers.size())
        {
            return nullptr;
        }
        return &m_Impl->Buffers[index];
    }

    Core::Result RenderGraph::SetPassId(const PassRef ref, const FramePassId id)
    {
        if (!m_Impl || !ref.IsValid() || ref.Index >= m_Impl->Passes.size())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        if (ref.Generation != m_Impl->Generation)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        m_Impl->Passes[ref.Index].Id = id;
        return Core::Ok();
    }

    Core::Result RenderGraph::SetTextureResourceId(const TextureRef ref, const FrameResourceId id)
    {
        if (!ValidateTextureRef(ref).has_value())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        m_Impl->Textures[ref.Index].Id = id;
        return Core::Ok();
    }

    Core::Result RenderGraph::SetBufferResourceId(const BufferRef ref, const FrameResourceId id)
    {
        if (!ValidateBufferRef(ref).has_value())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        m_Impl->Buffers[ref.Index].Id = id;
        return Core::Ok();
    }

    void RenderGraph::Reset()
    {
        if (!m_Impl)
        {
            return;
        }

        m_Impl->RecycledPasses.reserve(m_Impl->RecycledPasses.size() + m_Impl->Passes.size());
        for (auto passIt = m_Impl->Passes.rbegin(); passIt != m_Impl->Passes.rend(); ++passIt)
        {
            ResetPassRecordForReuse(*passIt);
            m_Impl->RecycledPasses.push_back(std::move(*passIt));
        }
        m_Impl->Passes.clear();
        m_Impl->Textures.clear();
        m_Impl->Buffers.clear();
        ++m_Impl->Generation;
        if (m_Impl->Generation == 0)
        {
            m_Impl->Generation = 1;
        }
    }

    std::uint32_t RenderGraph::GetPassCount() const
    {
        if (!m_Impl)
        {
            return 0u;
        }

        return static_cast<std::uint32_t>(m_Impl->Passes.size());
    }

    void RenderGraph::SetTransientAliasingEnabled(const bool enabled)
    {
        if (!m_Impl)
        {
            m_Impl = std::make_unique<Impl>();
        }
        m_Impl->TransientAliasingEnabled = enabled;
    }

    bool RenderGraph::IsTransientAliasingEnabled() const
    {
        return !m_Impl || m_Impl->TransientAliasingEnabled;
    }
}
