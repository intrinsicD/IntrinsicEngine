intrinsic_test_object_lib(ExtrinsicRuntimeTestObjs
        SOURCES
        Test.Runtime.StreamingExecutor.cpp
        Test.Runtime.EngineLayering.cpp
        DEPS ExtrinsicRuntime
)

intrinsic_test_executable(ExtrinsicRuntimeTests
        OBJECTS ExtrinsicRuntimeTestObjs
        LINK_LIBS ExtrinsicRuntime
)
