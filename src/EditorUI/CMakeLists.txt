intrinsic_add_module_library(IntrinsicEditorUI)
set(target_name IntrinsicEditorUI)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.EditorUI.cppm
        PRIVATE
        Runtime.EditorUI.cpp
        Runtime.EditorUI.Widgets.cpp
        Runtime.EditorUI.InspectorController.cpp
        Runtime.EditorUI.EntityCommands.cpp
        Runtime.EditorUI.GeometryWorkflowController.cpp
        Runtime.EditorUI.SpatialDebugController.cpp
)

target_include_directories(${target_name} PUBLIC .)

#target_include_directories(${target_name} SYSTEM PUBLIC
#        ${tinygltf_SOURCE_DIR}
#)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGraphics
        IntrinsicGeometry
        IntrinsicInterface
        IntrinsicRuntime
        PRIVATE
        #glfw
        #nlohmann_json::nlohmann_json
)