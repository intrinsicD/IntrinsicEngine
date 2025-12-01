set(target_name IntrinsicTests)

enable_testing()

add_executable(${target_name}
        Test_CoreMemory.cpp
        Test_CoreTasks.cpp
        Test_CoreAssets.cpp
        Test_RuntimeGeometry_Contact.cpp
        Test_RuntimeGeometry_Containment.cpp
        Test_RuntimeGeometry_Overlap.cpp
        Test_RuntimeGeometryProperties.cpp
)

#Apply Strict Flags ONLY to our tests
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

# Link against the Core Engine and GTest
target_link_libraries(${target_name}
        PRIVATE
        IntrinsicCore
        IntrinsicRuntime
        GTest::gtest_main
        EnTT::EnTT
)

# Helper to run tests via CTest
include(GoogleTest)
gtest_discover_tests(${target_name})