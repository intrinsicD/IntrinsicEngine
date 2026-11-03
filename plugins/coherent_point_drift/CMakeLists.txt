# ARCH-022: coherent point drift as a step run (cmake/IntrinsicFeaturePlugin.cmake).
intrinsic_add_feature_plugin(CoherentPointDrift
        CoherentPointDrift.Run.cpp CoherentPointDrift.Tools.cpp CoherentPointDrift.Window.cpp CoherentPointDrift.Plugin.cpp)
