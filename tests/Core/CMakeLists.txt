intrinsic_test_object_lib(ExtrinsicCoreTestObjs
        SOURCES
        #Test_CoreFilesystem.cpp
        Test.Core.Hash.cpp
        Test.Core.Filesystem.cpp
        Test.Core.CallbackRegistry.cpp
        Test.Core.DagScheduler.cpp
        Test.Core.GraphInterfaces.cpp
        Test.Core.Config.cpp
        Test.Core.Error.cpp
        Test.Core.StrongHandle.cpp
        Test.Core.LockFreeQueue.cpp
        Test.Core.Logging.cpp
        Test.Core.Memory.cpp
        Test.Core.Tasks.cpp
        DEPS ExtrinsicCore
)

intrinsic_test_executable(ExtrinsicCoreTests
        OBJECTS  ExtrinsicCoreTestObjs
        LINK_LIBS ExtrinsicCore
)
