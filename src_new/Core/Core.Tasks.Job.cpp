module;

#include <coroutine>
#include <memory>
#include <atomic>

module Extrinsic.Core.Tasks;

import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Tasks
{
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
        return Job(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    void Job::promise_type::unhandled_exception() noexcept
    {
        Log::Error("Core::Tasks::Job encountered an unhandled exception (exceptions disabled). Terminating coroutine.");
    }
}