// Private parameter conversion shared by config validation and curvature execution.
// Include after the config and geometry parameter declarations are visible.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime
    {
        [[nodiscard]] Geometry::Segmentation::SegmentationParams
        MakeCurvatureSegmentationParams(const CurvatureSegmentationConfig& config);
    }
}
