module;

#include <vector>
#include <memory>

export module Extrinsic.Core.Dag.Scheduler:DomainGraph;

import Extrinsic.Core.Error;
import :Types;
import :Compiler;

export namespace Extrinsic::Core::Dag
{
    class DomainTaskGraph
    {
    public:
        virtual ~DomainTaskGraph() = default;
        virtual Result Submit(const PendingTaskDesc& task) = 0;
        [[nodiscard]] virtual Expected<std::vector<PlanTask>> BuildPlan(const BuildConfig& config) = 0;
        [[nodiscard]] virtual QueueDomain Domain() const noexcept = 0;
        virtual void Reset() = 0;
    };

    [[nodiscard]] std::unique_ptr<DomainTaskGraph> CreateDomainTaskGraph(QueueDomain domain);
}
