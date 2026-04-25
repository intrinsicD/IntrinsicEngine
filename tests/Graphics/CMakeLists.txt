intrinsic_test_object_lib(ExtrinsicGraphicsTestObjs
        SOURCES
        Test.RHI.BufferManager.cpp
        Test.RHI.TextureManager.cpp
        Test.RHI.SamplerManager.cpp
        Test.RHI.PipelineManager.cpp
        Test.Graphics.GpuWorldAndCulling.cpp
        Test.Graphics.MinimalTriangleAcceptance.cpp
        DEPS ExtrinsicCore ExtrinsicGraphics
)

intrinsic_test_executable(ExtrinsicGraphicsTests
        OBJECTS  ExtrinsicGraphicsTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicGraphics
)
