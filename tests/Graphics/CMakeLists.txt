intrinsic_test_object_lib(ExtrinsicGraphicsTestObjs
        SOURCES
        Test.RHI.BufferManager.cpp
        Test.RHI.TextureManager.cpp
        Test.RHI.SamplerManager.cpp
        Test.RHI.PipelineManager.cpp
        Test.RHI.CommandContextBarriers.cpp
        Test.Graphics.GpuWorldAndCulling.cpp
        Test.Graphics.MinimalTriangleAcceptance.cpp
        Test.Graphics.RenderGraph.cpp
        Test.Graphics.Renderer.cpp
        DEPS ExtrinsicCore ExtrinsicGraphics EnTT::EnTT glm::glm
)

intrinsic_test_executable(ExtrinsicGraphicsTests
        OBJECTS  ExtrinsicGraphicsTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicGraphics
)
