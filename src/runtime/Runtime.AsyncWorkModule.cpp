module;

#include <cstdint>
#include <memory>
#include <string_view>

module Extrinsic.Runtime.AsyncWorkModule;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kApplyBudgetPerFrame = 8u;
    }

    AsyncWorkModule::AsyncWorkModule() = default;
    AsyncWorkModule::~AsyncWorkModule() = default;

    std::string_view AsyncWorkModule::Name() const noexcept
    {
        return "Runtime.AsyncWorkModule";
    }

    Core::Result AsyncWorkModule::OnRegister(EngineSetup& setup)
    {
        if (m_StreamingExecutor || m_DerivedJobRegistry ||
            m_WorldRetirementSubscription.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        // Registration is intentionally all-or-nothing. Reject a conflicting
        // or invalid registration phase before publishing the first borrowed
        // pointer so an expected conflict adds no partial provider or boot
        // diagnostics. Engine boot is single-threaded, making this preflight
        // stable through the three Provide calls below; exact Withdraw remains
        // the fail-closed rollback for an unexpected Provide failure.
        if (setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<StreamingExecutor>() != nullptr ||
            setup.Services().Find<DerivedJobRegistry>() != nullptr ||
            setup.Services().Find<Core::IStreamingFrameHooks>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_StreamingExecutor = std::make_unique<StreamingExecutor>();
        m_DerivedJobRegistry =
            std::make_unique<DerivedJobRegistry>(*m_StreamingExecutor);

        if (Core::Result provided =
                setup.Services().Provide<StreamingExecutor>(
                    *m_StreamingExecutor, Name());
            !provided.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return provided;
        }
        if (Core::Result provided =
                setup.Services().Provide<DerivedJobRegistry>(
                    *m_DerivedJobRegistry, Name());
            !provided.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return provided;
        }
        if (Core::Result provided =
                setup.Services().Provide<Core::IStreamingFrameHooks>(
                    *this, Name());
            !provided.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return provided;
        }

        m_WorldRetirementSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [this](const WorldWillBeDestroyed& event)
                {
                    RetireWorld(event.World);
                });

        return Core::Ok();
    }

    Core::Result AsyncWorkModule::OnResolve(EngineSetup& setup)
    {
        (void)setup;
        return Core::Ok();
    }

    void AsyncWorkModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (m_WorldRetirementSubscription.IsValid())
            context.Events.Unsubscribe(m_WorldRetirementSubscription);
        m_WorldRetirementSubscription = {};
        ShutdownAndReset(&context.Services);
    }

    void AsyncWorkModule::ShutdownAndReset(ServiceRegistry* const services)
    {
        // BUG-076: quiesce the derived registry alongside the executor. The
        // registry runs its background work on the streaming executor, so shut
        // the executor down first to finish/join all threaded work, then drain
        // the registry's completion and readback queues so no derived result is
        // left in-flight. Draining only the executor left this asymmetric with
        // the DrainCompletions()/PumpBackground() paths, which prefer the
        // registry.
        //
        // DrainReadbacks can promote a WaitingForReadback job to ReadyForApply
        // after the executor already ran its final ApplyMainThreadResults(), so
        // apply once more after draining — otherwise a newly-ready main-thread
        // result is silently dropped when Reset() destroys the registry. After
        // that drain→apply pass, cancel every survivor (notably a readback that
        // is still unavailable). Otherwise later polling could resume its
        // callback after this shutdown boundary. The executor is already shut
        // down, so the snapshot is stable and cancellation makes every record
        // terminal before this method returns.
        if (m_StreamingExecutor)
        {
            m_StreamingExecutor->ShutdownAndDrain();
        }
        if (m_DerivedJobRegistry)
        {
            m_DerivedJobRegistry->DrainCompletions();
            m_DerivedJobRegistry->DrainReadbacks();
            m_DerivedJobRegistry->ApplyMainThreadResults();

            const DerivedJobQueueSnapshot survivors =
                m_DerivedJobRegistry->SnapshotAll();
            for (const DerivedJobSnapshot& survivor : survivors.Entries)
            {
                m_DerivedJobRegistry->Cancel(survivor.Handle);
            }
        }

        // ServiceRegistry stores borrowed pointers. Withdraw each exact
        // instance after the final drain/apply pass and before destroying its
        // owner so modules that shut down later cannot observe stale entries.
        // Withdraw is also valid during registration rollback, where some
        // entries may not have been published yet.
        if (services != nullptr)
        {
            (void)services->Withdraw<Core::IStreamingFrameHooks>(*this);
            if (m_DerivedJobRegistry)
            {
                (void)services->Withdraw<DerivedJobRegistry>(
                    *m_DerivedJobRegistry);
            }
            if (m_StreamingExecutor)
            {
                (void)services->Withdraw<StreamingExecutor>(
                    *m_StreamingExecutor);
            }
        }

        m_DerivedJobRegistry.reset();
        m_StreamingExecutor.reset();
    }

    void AsyncWorkModule::DrainCompletions()
    {
        if (m_DerivedJobRegistry)
        {
            m_DerivedJobRegistry->DrainCompletions();
            m_DerivedJobRegistry->DrainReadbacks();
            return;
        }
        if (m_StreamingExecutor)
        {
            m_StreamingExecutor->DrainCompletions();
        }
    }

    void AsyncWorkModule::ApplyMainThreadResults()
    {
        (void)ApplyMainThreadResults(kApplyBudgetPerFrame);
    }

    std::uint32_t AsyncWorkModule::ApplyMainThreadResults(
        const std::uint32_t maxApplyCount)
    {
        if (m_DerivedJobRegistry)
        {
            return m_DerivedJobRegistry->ApplyMainThreadResults(maxApplyCount);
        }
        if (m_StreamingExecutor)
        {
            return m_StreamingExecutor->ApplyMainThreadResults(maxApplyCount);
        }
        return 0u;
    }

    void AsyncWorkModule::SubmitFrameWork()
    {
        // The existing streaming contract has no independent per-frame
        // submission source. Asset/document callers submit explicitly.
    }

    void AsyncWorkModule::PumpBackground(const std::uint32_t maxLaunches)
    {
        if (m_DerivedJobRegistry)
        {
            m_DerivedJobRegistry->Pump(maxLaunches);
            return;
        }
        if (m_StreamingExecutor)
        {
            m_StreamingExecutor->PumpBackground(maxLaunches);
        }
    }

    void AsyncWorkModule::RetireWorld(const WorldHandle world)
    {
        if (m_DerivedJobRegistry)
            (void)m_DerivedJobRegistry->CancelAllForWorld(world);
        if (m_StreamingExecutor)
            (void)m_StreamingExecutor->RetireWorld(world);
    }
}
