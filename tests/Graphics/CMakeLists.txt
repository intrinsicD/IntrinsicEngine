intrinsic_test_object_lib(ExtrinsicGraphicsTestObjs
        SOURCES
        Test.RHI.BufferManager.cpp
        DEPS ExtrinsicCore ExtrinsicGraphics
)

intrinsic_test_executable(ExtrinsicGraphicsTests
        OBJECTS  ExtrinsicGraphicsTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicGraphics
)
