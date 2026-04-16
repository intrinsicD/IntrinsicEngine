intrinsic_test_object_lib(AssetTestObjs
        SOURCES
        Test.Asset.EventBus.cpp
        Test.Asset.LoadPipeline.cpp
        Test.Asset.PathIndex.cpp
        Test.Asset.PayloadStore.cpp
        Test.Asset.Registry.cpp
        Test.Asset.Service.cpp
        DEPS ExtrinsicCore ExtrinsicAssets
)

intrinsic_test_executable(ExtrinsicAssetTests
        OBJECTS  AssetTestObjs
        LINK_LIBS ExtrinsicCore ExtrinsicAssets
)