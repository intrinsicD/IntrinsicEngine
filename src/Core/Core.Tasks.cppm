module;

#include <functional>

export module Core.Tasks;
import Core.Logging;

namespace Core::Tasks {

    // A Task is just a unit of work. 
    // For now, we use std::function. Later we can optimize to raw function pointers 
    // to avoid heap allocation, or use our LinearArena.
    using TaskFunction = std::function<void()>;

    export class Scheduler {
    public:
        // Initialize with 'threadCount' workers (0 = Auto-detect hardware threads)
        static void Initialize(unsigned threadCount = 0);
        static void Shutdown();

        // Add a fire-and-forget task
        static void Dispatch(TaskFunction&& task);

        // Wait until all tasks currently in the queue are finished
        // (Simple fence mechanism for research purposes)
        static void WaitForAll();

    private:
        static void WorkerEntry(unsigned threadIndex);
        
        // We hide implementation details in the cpp mostly, 
        // but for this module example, we keep state here.
        // In a real engine, this would be PIMPL or a Singleton instance.
    };
}