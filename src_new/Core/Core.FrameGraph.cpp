module;

#include <memory>
#include <string_view>

module Extrinsic.Core.FrameGraph;

namespace Extrinsic::Core
{
    FrameGraph::FrameGraph()
        : m_Graph(Dag::CreateTaskGraph(Dag::QueueDomain::Cpu))
    {}

    FrameGraph::~FrameGraph() = default;
    FrameGraph::FrameGraph(FrameGraph&&) noexcept = default;
    FrameGraph& FrameGraph::operator=(FrameGraph&&) noexcept = default;

    Core::Result FrameGraph::Compile()  { return m_Graph->Compile(); }
    Core::Result FrameGraph::Execute()  { return m_Graph->Execute(); }
    void         FrameGraph::Reset()    { m_Graph->Reset(); }

    uint32_t     FrameGraph::PassCount()           const noexcept { return m_Graph->PassCount(); }
    std::string_view FrameGraph::PassName(uint32_t i) const noexcept { return m_Graph->PassName(i); }
    uint64_t     FrameGraph::LastCompileTimeNs()   const noexcept { return m_Graph->LastCompileTimeNs(); }
    uint64_t     FrameGraph::LastExecuteTimeNs()   const noexcept { return m_Graph->LastExecuteTimeNs(); }
    uint64_t     FrameGraph::LastCriticalPathTimeNs() const noexcept { return m_Graph->LastCriticalPathTimeNs(); }
}

