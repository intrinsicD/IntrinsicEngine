# ARCH-022: the kernel density plugin (cmake/IntrinsicFeaturePlugin.cmake).
intrinsic_add_feature_plugin(KernelDensity
        KernelDensity.Run.cpp KernelDensity.Tools.cpp KernelDensity.Window.cpp KernelDensity.Plugin.cpp)
