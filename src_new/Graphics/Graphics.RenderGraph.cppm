module;

#include <cstdint>
#include <memory>
#include <string>

export module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;

export import :Resources;
export import :Pass;
export import :Compiler;
export import :Barriers;
export import :TransientAllocator;
export import :Executor;

namespace Extrinsic::Graphics
{
    export class RenderGraph final
    {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(RenderGraph&&) noexcept;
        RenderGraph& operator=(RenderGraph&&) noexcept;

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        [[nodiscard]] PassRef AddPass(std::string name, bool sideEffect = false);

        [[nodiscard]] Core::Expected<CompiledRenderGraph> Compile() const;
        void Reset();

        [[nodiscard]] std::uint32_t GetPassCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
