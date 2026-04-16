set(target_name ExtrinsicCore)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Core.Config.Window.cppm
        Core.Config.Render.cppm
        Core.Config.Engine.cppm
        Core.Error.cppm
        Core.Filesystem.cppm
        Core.Hash.cppm
        Core.LockFreeQueue.cppm
        Core.Logging.cppm
        Core.StrongHandle.cppm
        Core.Tasks.cppm
        Core.Tasks.Internal.cppm
        Core.Tasks.LocalTask.cppm
        PRIVATE
        Core.Filesystem.cpp
        Core.Logging.cpp
        Core.Tasks.Internal.cpp
        Core.Tasks.Job.cpp
        Core.Tasks.LocalTask.cpp
        Core.Tasks.Scheduler.cpp
)

target_include_directories(${target_name} PUBLIC .)