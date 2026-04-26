module;

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;
import :Pass;
import :Compiler;

namespace Extrinsic::Graphics
{
    struct RenderGraph::Impl
    {
        std::vector<RenderPassRecord> Passes{};
        std::uint32_t Generation = 1;
    };

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

    Core::Expected<CompiledRenderGraph> RenderGraph::Compile() const
    {
        if (!m_Impl)
        {
            return RenderGraphCompiler::Compile(0u, 0u);
        }

        return RenderGraphCompiler::Compile(static_cast<std::uint32_t>(m_Impl->Passes.size()), 0u);
    }

    void RenderGraph::Reset()
    {
        if (!m_Impl)
        {
            return;
        }

        m_Impl->Passes.clear();
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
