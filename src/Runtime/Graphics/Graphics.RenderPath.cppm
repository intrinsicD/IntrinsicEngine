module;
#include <string>
#include <vector>
#include <functional>
#include <concepts>
#include <string_view>
#include <memory>

export module Graphics:RenderPath;

import :RenderPipeline;
import Core;

export namespace Graphics
{
    // ---------------------------------------------------------------------
    // RenderStage
    // ---------------------------------------------------------------------
    // A lightweight wrapper for a single rendering step.
    // It captures a name (for profiling/debugging) and an execution function.
    class RenderStage
    {
    public:
        using ExecuteFn = std::function<void(RenderPassContext&)>;

        RenderStage(std::string name, ExecuteFn fn)
            : m_Name(std::move(name)), m_Execute(std::move(fn)) {}

        void Execute(RenderPassContext& ctx) const
        {
            // Future: Insert automatic GPU scope here using m_Name.
            // RHI::Profiler::Scope scope(ctx.Cmd, m_Name.c_str());
            if (m_Execute)
            {
                m_Execute(ctx);
            }
        }

        [[nodiscard]] const std::string& GetName() const { return m_Name; }

    private:
        std::string m_Name;
        ExecuteFn m_Execute;
    };

    // ---------------------------------------------------------------------
    // RenderPath (Frame Graph Builder)
    // ---------------------------------------------------------------------
    // A linear sequence of stages that defines a frame.
    // This replaces hardcoded function calls in RenderPipeline::SetupFrame().
    class RenderPath
    {
    public:
        // Add a generic invocable stage (lambda, function pointer, functor).
        // Example: path.AddStage("ShadowPass", [&](auto& ctx) { ... });
        template<typename F>
        requires std::invocable<F, RenderPassContext&>
        void AddStage(std::string_view name, F&& func)
        {
            m_Stages.emplace_back(std::string(name), std::forward<F>(func));
        }

        // Helper to add a legacy IRenderFeature directly.
        // Example: path.AddFeature("Forward", m_ForwardPass.get());
        void AddFeature(std::string_view name, IRenderFeature* feature)
        {
            if (feature)
            {
                AddStage(name, [feature](RenderPassContext& ctx) {
                    feature->AddPasses(ctx);
                });
            }
        }

        // Execute the entire path for the frame.
        void Execute(RenderPassContext& ctx) const
        {
            for (const auto& stage : m_Stages)
            {
                stage.Execute(ctx);
            }
        }

        void Clear() { m_Stages.clear(); }
        [[nodiscard]] bool IsEmpty() const { return m_Stages.empty(); }

    private:
        std::vector<RenderStage> m_Stages;
    };
}
