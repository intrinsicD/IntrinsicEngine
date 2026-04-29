intrinsic_test_object_lib(ExtrinsicECSTestObjs
        SOURCES
        Test.ECS.Scene.Registry.cpp
        DEPS ExtrinsicCore ExtrinsicECS EnTT::EnTT
)

intrinsic_test_executable(ExtrinsicECSTests
        OBJECTS  ExtrinsicECSTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicECS
        LABELS unit ecs
)
