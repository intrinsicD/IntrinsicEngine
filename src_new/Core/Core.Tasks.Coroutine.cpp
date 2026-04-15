module;

module Extrinsic.Core.Tasks;

import Extrinsic.Core.Logging;

#include "Core.Tasks.Internal.hpp"

namespace Extrinsic::Core::Tasks
{
    LocalTask::~LocalTask()
    {
        if (m_VTable)
            std::destroy_at(m_VTable);
    }

    LocalTask::LocalTask(LocalTask&& other) noexcept
    {
        if (other.m_VTable)
        {
            other.m_VTable->MoveTo(m_Storage);
            m_VTable = reinterpret_cast<Concept*>(m_Storage);
            std::destroy_at(other.m_VTable);
            other.m_VTable = nullptr;
        }
    }

    LocalTask& LocalTask::operator=(LocalTask&& other) noexcept
    {
        if (this != &other)
        {
            if (m_VTable)
                std::destroy_at(m_VTable);
            m_VTable = nullptr;

            if (other.m_VTable)
            {
                other.m_VTable->MoveTo(m_Storage);
                m_VTable = reinterpret_cast<Concept*>(m_Storage);
                std::destroy_at(other.m_VTable);
                other.m_VTable = nullptr;
            }
        }
        return *this;
    }

    void LocalTask::operator()() const
    {
        if (m_VTable)
            m_VTable->Execute();
    }

    Job::Job(std::coroutine_handle<promise_type> handle) noexcept
        : m_Handle(handle), m_Alive(std::make_shared<std::atomic<bool>>(true))
    {
    }

    Job::Job(Job&& other) noexcept
        : m_Handle(other.m_Handle)
          , m_Alive(std::move(other.m_Alive))
    {
        other.m_Handle = {};
    }

    Job& Job::operator=(Job&& other) noexcept
    {
        if (this != &other)
        {
            DestroyIfOwned();
            m_Handle = other.m_Handle;
            m_Alive = std::move(other.m_Alive);
            other.m_Handle = {};
        }
        return *this;
    }

    Job::~Job()
    {
        DestroyIfOwned();
    }

    void Job::DestroyIfOwned()
    {
        if (!m_Handle)
            return;

        if (m_Alive)
            m_Alive->store(false, std::memory_order_release);
        if (!m_Handle.done())
            m_Handle.destroy();
        m_Handle = {};
    }

    Job Job::promise_type::get_return_object() noexcept
    {
        return Job(std::coroutine_handle<Job::promise_type>::from_promise(*this));
    }

    void Job::promise_type::unhandled_exception() noexcept
    {
        Log::Error("Core::Tasks::Job encountered an unhandled exception (exceptions disabled). Terminating coroutine.");
    }

    void YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        Scheduler::Reschedule(handle);
    }
}
