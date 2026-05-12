set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

option(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
        "Allow runtime to instantiate the promoted fail-closed Vulkan IDevice when RenderConfig opts in"
        OFF)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.Engine.cppm
        Runtime.FrameLoop.cppm
        Runtime.ProceduralGeometry.cppm
        Runtime.ProceduralGeometryPacker.cppm
        Runtime.RenderExtraction.cppm
        Runtime.StreamingExecutor.cppm
        PRIVATE
        Runtime.Engine.cpp
        Runtime.ProceduralGeometry.cpp
        Runtime.ProceduralGeometryPacker.cpp
        Runtime.StreamingExecutor.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicAssets
        ExtrinsicCore
        ExtrinsicECS
        ExtrinsicGraphics
        ExtrinsicGraphicsAssets
        ExtrinsicPlatform
        EnTT::EnTT
        glm::glm
)

if(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN)
        if(TARGET ExtrinsicBackendsVulkan)
                target_link_libraries(${target_name} PRIVATE ExtrinsicBackendsVulkan)
                target_compile_definitions(${target_name} PRIVATE EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN=1)
        else()
                message(WARNING
                        "INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON but ExtrinsicBackendsVulkan is not built; "
                        "runtime will keep using the Null fallback.")
        endif()
endif()

