target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.Component.GpuSceneSlot.cppm
        Graphics.Component.Material.cppm
        Graphics.Component.RenderGeometry.cppm
        Graphics.Component.VisualizationConfig.cppm
)

target_include_directories(${target_name} PUBLIC .)

