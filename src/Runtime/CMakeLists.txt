set(target_name IntrinsicRuntime)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES
        FILES
        RHI/RHI.Bindless.cppm
        RHI/RHI.Context.cppm
        RHI/RHI.Device.cppm
        RHI/RHI.Swapchain.cppm
        RHI/RHI.Renderer.cppm
        RHI/RHI.Buffer.cppm
        RHI/RHI.CommandUtils.cppm
        RHI/RHI.Types.cppm
        RHI/RHI.Shader.cppm
        RHI/RHI.Pipeline.cppm
        RHI/RHI.Descriptors.cppm
        RHI/RHI.Image.cppm
        RHI/RHI.Texture.cppm
        ECS/ECS.Components.cppm
        ECS/ECS.Scene.cppm
        Graphics/Graphics.Material.cppm
        Graphics/Graphics.Model.cppm
        Graphics/Graphics.Geometry.cppm
        Graphics/Graphics.RenderSystem.cppm
        Graphics/Graphics.ModelLoader.cppm
        Graphics/Graphics.Camera.cppm
        RenderGraph/RenderGraph.cppm
        Interface/Gui.cppm
        Geometry/Geometry.AABB.cppm
        Geometry/Geometry.OBB.cppm
        Geometry/Geometry.ContactManifold.cppm
        Geometry/Geometry.Containment.cppm
        Geometry/Geometry.GJK.cppm
        Geometry/Geometry.Overlap.cppm
        Geometry/Geometry.Primitives.cppm
        Geometry/Geometry.Support.cppm
        Geometry/Geometry.SDF.cppm
        Geometry/Geometry.SDFContact.cppm
        Geometry/Geometry.Validation.cppm
        Geometry/Geometry.Properties.cppm
        Geometry/Geometry.MeshUtils.cppm
        Geometry/Geometry.Octree.cppm
        Utils/Utils.BoundedHeap.cppm
        Runtime.Engine.cppm
        PRIVATE
        RHI/RHI.Bindless.cpp
        RHI/RHI.Context.cpp
        RHI/RHI.Device.cpp
        RHI/RHI.Swapchain.cpp
        RHI/RHI.Renderer.cpp
        RHI/RHI.Buffer.cpp
        RHI/RHI.Shader.cpp
        RHI/RHI.Pipeline.cpp
        RHI/RHI.Vma.cpp
        RHI/RHI.Descriptors.cpp
        RHI/RHI.Image.cpp
        RHI/RHI.Texture.cpp
        Graphics/Graphics.Material.cpp
        Graphics/Graphics.RenderSystem.cpp
        Graphics/Graphics.Geometry.cpp
        Graphics/Graphics.ModelLoader.cpp
        Graphics/Graphics.Camera.cpp
        Graphics/Graphics.TinyGltf.cpp
        Geometry/Geometry.MeshUtils.cpp
        RenderGraph/RenderGraph.cpp
        Interface/Gui.cpp
        Runtime.Engine.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(${target_name} SYSTEM PUBLIC
        ${vma_SOURCE_DIR}/include
        ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        stb
        volk::volk
        EnTT::EnTT
        nlohmann_json::nlohmann_json
        imgui_lib
)