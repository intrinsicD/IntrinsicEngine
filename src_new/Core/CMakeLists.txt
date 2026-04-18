set(target_name ExtrinsicCore)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Core.Config.Window.cppm
        Core.Config.Render.cppm
        Core.Config.Engine.cppm
        Core.Dag.Scheduler.cppm
        Core.CallbackRegistry.cppm
        Core.Error.cppm
        Core.Filesystem.PathResolver.cppm
        Core.Filesystem.cppm
        Core.Hash.cppm
        Core.LockFreeQueue.cppm
        Core.Logging.cppm
        Core.StrongHandle.cppm
        Core.Tasks.cppm
        Core.Tasks.CounterEvent.cppm
        Core.Tasks.Internal.cppm
        Core.Tasks.LocalTask.cppm
        FILE_SET tasks_impl TYPE CXX_MODULES FILES
        Core.Tasks.Internal.cpp
        Core.Tasks.LocalTask.cpp
        PRIVATE
        Core.Filesystem.cpp
        Core.Filesystem.PathResolver.cpp
        Core.Logging.cpp
        Core.Tasks.CounterEvent.cpp
        Core.Tasks.Dispatch.cpp
        Core.Tasks.Job.cpp
        Core.Tasks.Lifecycle.cpp
        Core.Tasks.Stats.cpp
        Core.Tasks.WaitToken.cpp
        Core.Tasks.Worker.cpp
)

target_include_directories(${target_name} PUBLIC .)