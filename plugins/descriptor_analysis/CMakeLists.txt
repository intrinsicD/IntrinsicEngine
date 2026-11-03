# ARCH-022: the FPFH descriptor analysis plugin (cmake/IntrinsicFeaturePlugin.cmake).
intrinsic_add_feature_plugin(DescriptorAnalysis
        DescriptorAnalysis.Run.cpp DescriptorAnalysis.Tools.cpp DescriptorAnalysis.Window.cpp
        DescriptorAnalysis.Plugin.cpp)
