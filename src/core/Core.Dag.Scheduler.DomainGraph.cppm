module;

#include <vector>
#include <memory>

export module Extrinsic.Core.Dag.Scheduler:DomainGraph;

import Extrinsic.Core.Error;
import :Types;
import :Compiler;

export namespace Extrinsic::Core::Dag
{
    class TaskPlanGraph
    {
    public:
        virtual ~TaskPlanGraph() = default;
        virtual Result Submit(const PendingTaskDesc& task) = 0;
        [[nodiscard]] virtual Expected<std::vector<PlanTask>> BuildPlan() = 0;
        [[nodiscard]] virtual ScheduleStats GetLastStats() const = 0;
        virtual void Reset() = 0;
    };

    [[nodiscard]] std::unique_ptr<TaskPlanGraph> CreateTaskPlanGraph();
}
