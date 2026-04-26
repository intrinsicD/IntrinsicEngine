module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;
import :Pass;
import :Compiler;
import :Resources;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

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
                   state == TextureState::ShaderWrite || state == TextureState::TransferDst;
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
        return ImportTexture(std::move(name), handle, TextureState::Undefined, TextureState::Present);
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
            return RenderGraphCompiler::Compile(0u, 0u);
        }

        const auto hasValidationFailure = std::ranges::any_of(
            m_Impl->Passes, [](const RenderPassRecord& pass) { return pass.HasValidationError; });
        if (hasValidationFailure)
        {
            return Core::Unexpected(Core::ErrorCode::InvalidArgument);
        }

        return RenderGraphCompiler::Compile(static_cast<std::uint32_t>(m_Impl->Passes.size()),
                                            static_cast<std::uint32_t>(m_Impl->Textures.size() + m_Impl->Buffers.size()));
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
}
