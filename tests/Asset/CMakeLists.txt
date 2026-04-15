intrinsic_test_object_lib(AssetTestObjs
        SOURCES
        Test_CoreAssets.cpp
        Test_CoreAssetSafety.cpp
        DEPS ExtrinsicCore ExtrinsicAssets EnTT::EnTT
)

intrinsic_test_executable(ExtrinsicAssetTests
        OBJECTS  AssetTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicAssets EnTT::EnTT
)