module;

#include <string_view>

module Extrinsic.Runtime.AsyncWorkModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        void CancelJobServiceSurvivors(JobService& jobs)
        {
            for (const JobSnapshot& survivor : jobs.SnapshotAll())
                (void)jobs.Cancel(survivor.Token);
        }
    }

    std::string_view AsyncWorkModule::Name() const noexcept
    {
        return "Runtime.AsyncWorkModule";
    }

    Core::Result AsyncWorkModule::OnRegister(EngineSetup& setup)
    {
        if (m_Registered ||
            setup.Services().Phase() != ServiceRegistryPhase::Registration ||
            setup.Services().Find<JobService>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        Core::Result provided =
            setup.Services().Provide<JobService>(setup.Jobs(), Name());
        if (!provided.has_value())
        {
            return provided;
        }

        m_Registered = true;
        return Core::Ok();
    }

    void AsyncWorkModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (!m_Registered)
            return;

        // Stop publishing the borrowed kernel service before later modules in
        // reverse shutdown order run, then request cancellation for every
        // survivor. The engine owns worker quiescence after module shutdown.
        (void)context.Services.Withdraw<JobService>(context.Jobs);
        CancelJobServiceSurvivors(context.Jobs);
        m_Registered = false;
    }
}
