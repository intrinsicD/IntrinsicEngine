intrinsic_test_object_lib(ExtrinsicCoreTestObjs
        SOURCES
        #Test_CoreFilesystem.cpp
        Test.Core.Hash.cpp
        Test.Core.Filesystem.cpp
        Test.Core.CallbackRegistry.cpp
        Test.Core.DagScheduler.cpp
        Test.Core.GraphInterfaces.cpp
        #Test_CoreLockFreeQueue.cpp
        #Test_CoreLogging.cpp
        #Test_CoreTasks.cpp
        DEPS ExtrinsicCore
)

intrinsic_test_executable(ExtrinsicCoreTests
        OBJECTS  ExtrinsicCoreTestObjs
        LINK_LIBS ExtrinsicCore
)
