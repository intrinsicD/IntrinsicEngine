# ARCH-022: the bilateral point filter plugin (cmake/IntrinsicFeaturePlugin.cmake).
intrinsic_add_feature_plugin(BilateralFilter
        BilateralFilter.Run.cpp BilateralFilter.Tools.cpp BilateralFilter.Window.cpp BilateralFilter.Plugin.cpp)
