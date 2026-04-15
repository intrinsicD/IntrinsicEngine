intrinsic_test_object_lib(AssetTestObjs
        SOURCES
        Test_Assets.cpp
        Test_AssetSafety.cpp
        DEPS ExtrinsicCore ExtrinsicAssets EnTT::EnTT
)

intrinsic_test_executable(ExtrinsicAssetTests
        OBJECTS  AssetTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicAssets EnTT::EnTT
)