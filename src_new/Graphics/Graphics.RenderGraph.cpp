module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <ranges>
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

namespace Extrinsic::Graphics
{
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
    }

    struct RenderGraph::Impl
    {
        std::vector<RenderPassRecord> Passes{};
        std::vector<TextureResourceDesc> Textures{};
        std::vector<BufferResourceDesc> Buffers{};
        TransientAllocator Transients{};
        bool TransientAliasingEnabled = true;
        std::uint32_t Generation = 1;
    };

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
        m_Impl->Passes.push_back(RenderPassRecord{.Name = std::move(name), .SideEffect = sideEffect});
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
            [this](const TextureRef ref, const TextureUsage usage, const bool write) {
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

    Core::Expected<CompiledRenderGraph> RenderGraph::Compile() const
    {
        if (!m_Impl)
        {
            return RenderGraphCompiler::Compile({}, {}, {});
        }

        const auto hasValidationFailure = std::ranges::any_of(
            m_Impl->Passes, [](const RenderPassRecord& pass) { return pass.HasValidationError; });
        if (hasValidationFailure)
        {
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        const bool invalidPresentTarget = std::ranges::any_of(m_Impl->Passes, [this](const RenderPassRecord& pass) {
            return std::ranges::any_of(pass.TextureAccesses, [this](const TextureAccess& access) {
                if (access.Usage != TextureUsage::Present)
                {
                    return false;
                }
                const TextureResourceDesc* desc = GetTextureDesc(access.Ref);
                return (desc == nullptr) || !desc->Imported || !desc->IsBackbuffer;
            });
        });
        if (invalidPresentTarget)
        {
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        auto compiled = RenderGraphCompiler::Compile(m_Impl->Passes, m_Impl->Textures, m_Impl->Buffers);
        if (!compiled.has_value())
        {
            return compiled;
        }

        auto BytesPerPixel = [](const RHI::Format fmt) -> std::uint64_t {
            switch (fmt)
            {
            case RHI::Format::R8_UNORM: return 1u;
            case RHI::Format::RG8_UNORM: return 2u;
            case RHI::Format::RGBA8_UNORM:
            case RHI::Format::RGBA8_SRGB:
            case RHI::Format::BGRA8_UNORM:
            case RHI::Format::BGRA8_SRGB:
            case RHI::Format::R32_FLOAT:
            case RHI::Format::R32_UINT:
            case RHI::Format::R32_SINT:
            case RHI::Format::D32_FLOAT: return 4u;
            case RHI::Format::RG16_FLOAT:
            case RHI::Format::R16_FLOAT:
            case RHI::Format::R16_UINT:
            case RHI::Format::R16_UNORM:
            case RHI::Format::D16_UNORM: return 2u;
            case RHI::Format::RGBA16_FLOAT:
            case RHI::Format::RG32_FLOAT:
            case RHI::Format::D24_UNORM_S8_UINT: return 8u;
            case RHI::Format::RGB32_FLOAT:
            case RHI::Format::D32_FLOAT_S8_UINT: return 12u;
            case RHI::Format::RGBA32_FLOAT: return 16u;
            default: return 4u;
            }
        };

        m_Impl->Transients.ResetFrame();

        struct TextureAllocItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
        };
        struct BufferAllocItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
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
            texturesToAllocate.push_back(TextureAllocItem{
                .ResourceIndex = i,
                .FirstUsePass = compiled->TextureLifetimes[i].FirstUsePass,
                .LastUsePass = compiled->TextureLifetimes[i].LastUsePass,
            });
        }
        for (std::uint32_t i = 0; i < m_Impl->Buffers.size(); ++i)
        {
            if (m_Impl->Buffers[i].Imported || !compiled->BufferLifetimes[i].HasUse)
            {
                continue;
            }
            buffersToAllocate.push_back(BufferAllocItem{
                .ResourceIndex = i,
                .FirstUsePass = compiled->BufferLifetimes[i].FirstUsePass,
                .LastUsePass = compiled->BufferLifetimes[i].LastUsePass,
            });
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

        for (std::uint32_t i = 0; i < m_Impl->Textures.size(); ++i)
        {
            if (m_Impl->Textures[i].Imported || !compiled->TextureLifetimes[i].HasUse)
            {
                continue;
            }
            const auto& desc = m_Impl->Textures[i].Desc;
            compiled->TransientMemoryEstimateBytes += static_cast<std::uint64_t>(desc.Width) *
                                                      static_cast<std::uint64_t>(desc.Height) *
                                                      static_cast<std::uint64_t>(desc.DepthOrArrayLayers) *
                                                      static_cast<std::uint64_t>(desc.MipLevels) *
                                                      static_cast<std::uint64_t>(std::max(desc.SampleCount, 1u)) *
                                                      BytesPerPixel(desc.Fmt);
        }

        for (std::uint32_t i = 0; i < m_Impl->Buffers.size(); ++i)
        {
            if (m_Impl->Buffers[i].Imported || !compiled->BufferLifetimes[i].HasUse)
            {
                continue;
            }
            compiled->TransientMemoryEstimateBytes += m_Impl->Buffers[i].Desc.SizeBytes;
        }

        return compiled;
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

    void RenderGraph::Reset()
    {
        if (!m_Impl)
        {
            return;
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
