module;

#include <memory>
#include <string_view>
#include <vector>
#include <cstdint>

module Extrinsic.Core.FrameGraph;

namespace Extrinsic::Core
{
    FrameGraph::FrameGraph()
        : m_Graph(Dag::CreateTaskGraph())
    {}

    FrameGraph::~FrameGraph() = default;
    FrameGraph::FrameGraph(FrameGraph&&) noexcept = default;
    FrameGraph& FrameGraph::operator=(FrameGraph&&) noexcept = default;

    Core::Result FrameGraph::Compile()  { return m_Graph->Compile(); }
    Core::Result FrameGraph::Execute()  { return m_Graph->Execute(); }
    Core::Expected<std::vector<Dag::PlanTask>> FrameGraph::BuildPlan()
    {
        return m_Graph->BuildPlan();
    }

    void         FrameGraph::Reset()    { (void)m_Graph->Reset(); }
    Core::Result FrameGraph::ResetForReplay() { return m_Graph->ResetForReplay(); }

    uint32_t     FrameGraph::PassCount()           const noexcept { return m_Graph->PassCount(); }
    std::string_view FrameGraph::PassName(uint32_t i) const noexcept { return m_Graph->PassName(i); }
    const std::vector<std::vector<uint32_t>>& FrameGraph::GetExecutionLayers() const noexcept
    {
        return m_Graph->GetExecutionLayers();
    }
    Dag::ScheduleStats FrameGraph::GetScheduleStats() const noexcept { return m_Graph->GetScheduleStats(); }
    uint64_t     FrameGraph::LastCompileTimeNs()   const noexcept { return m_Graph->LastCompileTimeNs(); }
    uint64_t     FrameGraph::LastExecuteTimeNs()   const noexcept { return m_Graph->LastExecuteTimeNs(); }
    uint64_t     FrameGraph::LastCriticalPathTimeNs() const noexcept { return m_Graph->LastCriticalPathTimeNs(); }
    Dag::TaskGraphPlanReuseStats FrameGraph::GetPlanReuseStats() const noexcept
    {
        return m_Graph->GetPlanReuseStats();
    }
}
