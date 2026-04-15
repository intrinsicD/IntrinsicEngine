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
        PRIVATE
        Core.Filesystem.cpp
        Core.Logging.cpp
        Core.Tasks.Common.cpp
        Core.Tasks.Coroutine.cpp
        Core.Tasks.Scheduler.cpp
        Core.Tasks.Wait.cpp
)

target_include_directories(${target_name} PUBLIC .)