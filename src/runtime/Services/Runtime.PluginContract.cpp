module;

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

module Extrinsic.Runtime.PluginContract;

import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.PluginHost;

namespace Extrinsic::Runtime
{
    bool RecordPluginCommand(const AgentOperationContext& context, std::string label, std::string command,
                             std::string payload)
    {
        if (context.History == nullptr) return false;
        return context.History->Execute(context.History->MakeRegisteredCommandRecord(
            std::move(label), std::move(command), std::move(payload))).Succeeded();
    }

    PluginJobHandle SubmitPluginJob(const AgentOperationContext&, PluginJob job, std::string* refusal)
    {
        auto* host = PluginHostModule::Current();
        return host != nullptr ? host->SubmitJob(std::move(job), refusal) : PluginJobHandle{};
    }

    bool CancelPluginJob(const PluginJobHandle job)
    {
        auto* host = PluginHostModule::Current();
        return host != nullptr && host->CancelJob(job);
    }

    void ReportPluginJobOutcome(const PluginJobHandle job, const PluginJobDomain resolved, std::string diagnostic)
    {
        if (auto* host = PluginHostModule::Current()) host->ReportJobOutcome(job, resolved, std::move(diagnostic));
    }

    bool StartPluginGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work)
    {
        auto* host = PluginHostModule::Current();
        return host != nullptr && host->StartGpuWork(context, std::move(work));
    }

    ServiceRegistry* PluginServices() noexcept
    {
        const auto* host = PluginHostModule::Current();
        return host != nullptr ? host->Services() : nullptr;
    }
}

namespace Extrinsic::Runtime::PluginProbes
{
    int& HostCounter() noexcept
    {
        static int value = 0;
        return value;
    }

    int& HostThreadLocal() noexcept
    {
        thread_local int value = 0;
        return value;
    }

    std::int64_t& HostUndoValue() noexcept
    {
        static std::int64_t value = 0;
        return value;
    }

    bool RecordClosureCommand(const AgentOperationContext& context, std::string label, std::function<bool()> redo,
                              std::function<bool()> undo)
    {
        const auto* host = PluginHostModule::Current();
        if (host == nullptr || !host->TestHooks() || context.History == nullptr) return false;
        using Status = EditorCommandHistoryStatus;
        return context.History->Execute({
            .Label = std::move(label),
            .Redo = [redo = std::move(redo)] { return redo() ? Status::Applied : Status::CommandFailed; },
            .Undo = [undo = std::move(undo)]
            {
                intrinsic_plugin_event("closure_undo"); // attributes the control's crash to this call
                return undo() ? Status::Undone : Status::UndoFailed;
            },
        }).Succeeded();
    }
}
