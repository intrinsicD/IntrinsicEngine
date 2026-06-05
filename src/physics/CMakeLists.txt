set(target_name ExtrinsicPhysics)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Physics.World.cppm
        PRIVATE
        Physics.World.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore
        IntrinsicGeometry
        glm::glm
)
