intrinsic_test_object_lib(ExtrinsicCoreTestObjs
        SOURCES
        #Test_CoreFilesystem.cpp
        Test_CoreCallbackRegistry.cpp
        Test_CoreHash.cpp
        #Test_CoreLockFreeQueue.cpp
        #Test_CoreLogging.cpp
        #Test_CoreTasks.cpp
        DEPS ExtrinsicCore
)

intrinsic_test_executable(ExtrinsicCoreTests
        OBJECTS  ExtrinsicCoreTestObjs
        LINK_LIBS ExtrinsicCore
)