module;
#include <vector>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <type_traits>
#include <utility>
#include <cstddef>
#include <new>

export module Core.Tasks;
import Core.Logging;
import Core.Memory;

namespace Core::Tasks
{
    export class TaskFunction
    {
    public:
        using InvokeFn = void(*)(void*);
        using DestroyFn = void(*)(void*);

        TaskFunction() = default;

        template <typename F>
        TaskFunction(F&& fn) { Emplace(std::forward<F>(fn)); }

        TaskFunction(TaskFunction&& other) noexcept
            : m_Invoke(other.m_Invoke), m_Destroy(other.m_Destroy), m_Context(other.m_Context)
        {
            other.m_Invoke = nullptr;
            other.m_Destroy = nullptr;
            other.m_Context = nullptr;
        }

        TaskFunction& operator=(TaskFunction&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Invoke = other.m_Invoke;
                m_Destroy = other.m_Destroy;
                m_Context = other.m_Context;
                other.m_Invoke = nullptr;
                other.m_Destroy = nullptr;
                other.m_Context = nullptr;
            }
            return *this;
        }

        TaskFunction(const TaskFunction&) = delete;
        TaskFunction& operator=(const TaskFunction&) = delete;

        ~TaskFunction() { Reset(); }

        void operator()()
        {
            if (m_Invoke) m_Invoke(m_Context);
        }

        explicit operator bool() const { return m_Invoke != nullptr; }

    private:
        template <typename F>
        void Emplace(F&& fn)
        {
            using FnT = std::decay_t<F>;
            constexpr bool kTrivial = std::is_trivially_destructible_v<FnT>;

            void* storage = nullptr;
            if constexpr (kTrivial)
            {
                storage = Scheduler::AcquireTaskStorage(sizeof(FnT), alignof(FnT));
            }

            bool usesArena = storage != nullptr;
            if (!storage)
            {
                storage = ::operator new(sizeof(FnT));
            }

            FnT* functor = new (storage) FnT(std::forward<F>(fn));
            m_Context = functor;
            m_Invoke = [](void* ctx)
            {
                (*static_cast<FnT*>(ctx))();
            };

            if (!usesArena)
            {
                m_Destroy = [](void* ctx)
                {
                    FnT* target = static_cast<FnT*>(ctx);
                    target->~FnT();
                    ::operator delete(target);
                };
            }
            else
            {
                m_Destroy = nullptr; // Reclaimed when arena resets
            }
        }

        void Reset()
        {
            if (m_Destroy && m_Context) m_Destroy(m_Context);
            m_Invoke = nullptr;
            m_Destroy = nullptr;
            m_Context = nullptr;
        }

        InvokeFn m_Invoke = nullptr;
        DestroyFn m_Destroy = nullptr;
        void* m_Context = nullptr;

        friend class Scheduler;
    };

    export class Scheduler
    {
    public:
        // Initialize with 'threadCount' workers (0 = Auto-detect hardware threads)
        static void Initialize(unsigned threadCount = 0);
        static void Shutdown();

        // Add a fire-and-forget task
        static void Dispatch(TaskFunction&& task);

        template <typename F>
        static void Dispatch(F&& fn)
        {
            Dispatch(TaskFunction(std::forward<F>(fn)));
        }

        // Wait until all tasks currently in the queue are finished
        static void WaitForAll();

    private:
        static void WorkerEntry(unsigned threadIndex);
        static void* AcquireTaskStorage(size_t size, size_t alignment);
        static void ResetTaskArena();
    };
}
