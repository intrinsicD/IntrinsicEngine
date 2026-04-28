#pragma once

#ifdef INTRINSIC_HAS_CUDA

#include <cstdint>
#include <cuda.h>

namespace Runtime::PointCloudKMeans::CudaKernels
{
    [[nodiscard]] bool LaunchLloyd(
        CUstream stream,
        CUdeviceptr positions,
        uint32_t pointCount,
        CUdeviceptr centroids,
        uint32_t clusterCount,
        uint32_t iterations,
        CUdeviceptr labels,
        CUdeviceptr distances,
        CUdeviceptr sums,
        CUdeviceptr clusterSizes);
}

#endif // INTRINSIC_HAS_CUDA

