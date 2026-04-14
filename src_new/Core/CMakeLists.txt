set(target_name ExtrinsicCore)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Core.Config.Window.cppm
        Core.Config.Render.cppm
        Core.Config.Engine.cppm
        Core.StrongHandle.cppm
        Core.Error.cppm
        Core.Hash.cppm
        PRIVATE

)

target_include_directories(${target_name} PUBLIC .)