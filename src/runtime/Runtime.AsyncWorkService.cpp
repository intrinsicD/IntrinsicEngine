module;

#include <cstdint>
#include <memory>
#include <utility>

module Extrinsic.Runtime.AsyncWorkService;

namespace Extrinsic::Runtime
{
    AsyncWorkService::AsyncWorkService() = default;
    AsyncWorkService::~AsyncWorkService() = default;

    void AsyncWorkService::Initialize()
    {
        m_StreamingExecutor = std::make_unique<StreamingExecutor>();
        m_DerivedJobRegistry =
            std::make_unique<DerivedJobRegistry>(*m_StreamingExecutor);
    }

    void AsyncWorkService::ShutdownAndDrain()
    {
        // BUG-076: quiesce the derived registry alongside the executor. The
        // registry runs its background work on the streaming executor, so shut
        // the executor down first to finish/join all threaded work, then drain
        // the registry's completion and readback queues so no derived result is
        // left in-flight. Draining only the executor left this asymmetric with
        // the DrainCompletions()/PumpBackground() paths, which prefer the
        // registry.
        if (m_StreamingExecutor)
        {
            m_StreamingExecutor->ShutdownAndDrain();
        }
        if (m_DerivedJobRegistry)
        {
            m_DerivedJobRegistry->DrainCompletions();
            m_DerivedJobRegistry->DrainReadbacks();
        }
    }

    void AsyncWorkService::Reset() noexcept
    {
        m_DerivedJobRegistry.reset();
        m_StreamingExecutor.reset();
    }

    StreamingExecutor* AsyncWorkService::Streaming() noexcept
    {
        return m_StreamingExecutor.get();
    }

    const StreamingExecutor* AsyncWorkService::Streaming() const noexcept
    {
        return m_StreamingExecutor.get();
    }

    DerivedJobRegistry* AsyncWorkService::DerivedJobs() noexcept
    {
        return m_DerivedJobRegistry.get();
    }

    const DerivedJobRegistry* AsyncWorkService::DerivedJobs() const noexcept
    {
        return m_DerivedJobRegistry.get();
    }

    void AsyncWorkService::DrainCompletions()
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

    std::uint32_t AsyncWorkService::ApplyMainThreadResults(
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

    void AsyncWorkService::PumpBackground(const std::uint32_t maxLaunches)
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

    DerivedJobHandle AsyncWorkService::SubmitDerivedJob(DerivedJobDesc desc)
    {
        if (!m_DerivedJobRegistry)
        {
            return {};
        }
        return m_DerivedJobRegistry->Submit(std::move(desc));
    }

    void AsyncWorkService::CancelDerivedJob(const DerivedJobHandle handle)
    {
        if (m_DerivedJobRegistry)
        {
            m_DerivedJobRegistry->Cancel(handle);
        }
    }

    DerivedJobQueueSnapshot AsyncWorkService::SnapshotDerivedJobs() const
    {
        if (!m_DerivedJobRegistry)
        {
            return {};
        }
        return m_DerivedJobRegistry->SnapshotAll();
    }
}
