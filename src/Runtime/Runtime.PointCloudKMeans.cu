#include <cstdint>
#include "Runtime.PointCloudKMeans.CudaKernels.hpp"
#include <cuda.h>
#include <cuda_runtime.h>

namespace Runtime::PointCloudKMeans::CudaKernels
{
    namespace
    {
        struct Float3
        {
            float x;
            float y;
            float z;
        };
        static_assert(sizeof(Float3) == 12);

        __global__ void AssignClusters(
            const Float3* positions,
            uint32_t pointCount,
            Float3* centroids,
            uint32_t clusterCount,
            uint32_t* labels,
            float* distances,
            Float3* sums,
            uint32_t* clusterSizes)
        {
            const uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
            if (idx >= pointCount)
                return;

            const Float3 p = positions[idx];
            uint32_t bestCluster = 0;
            float dx = p.x - centroids[0].x;
            float dy = p.y - centroids[0].y;
            float dz = p.z - centroids[0].z;
            float bestDistance = dx * dx + dy * dy + dz * dz;

            for (uint32_t c = 1; c < clusterCount; ++c)
            {
                dx = p.x - centroids[c].x;
                dy = p.y - centroids[c].y;
                dz = p.z - centroids[c].z;
                const float d = dx * dx + dy * dy + dz * dz;
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestCluster = c;
                }
            }

            labels[idx] = bestCluster;
            distances[idx] = bestDistance;
            atomicAdd(&sums[bestCluster].x, p.x);
            atomicAdd(&sums[bestCluster].y, p.y);
            atomicAdd(&sums[bestCluster].z, p.z);
            atomicAdd(&clusterSizes[bestCluster], 1u);
        }

        __global__ void UpdateCentroids(
            Float3* centroids,
            const Float3* sums,
            const uint32_t* clusterSizes,
            uint32_t clusterCount)
        {
            const uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
            if (idx >= clusterCount)
                return;

            const uint32_t count = clusterSizes[idx];
            if (count == 0)
                return;

            const float invCount = 1.0f / static_cast<float>(count);
            centroids[idx].x = sums[idx].x * invCount;
            centroids[idx].y = sums[idx].y * invCount;
            centroids[idx].z = sums[idx].z * invCount;
        }
    }

    bool LaunchLloyd(
        CUstream stream,
        CUdeviceptr positions,
        uint32_t pointCount,
        CUdeviceptr centroids,
        uint32_t clusterCount,
        uint32_t iterations,
        CUdeviceptr labels,
        CUdeviceptr distances,
        CUdeviceptr sums,
        CUdeviceptr clusterSizes)
    {
        if (pointCount == 0 || clusterCount == 0 || iterations == 0)
            return false;

        auto* dPositions = reinterpret_cast<const Float3*>(positions);
        auto* dCentroids = reinterpret_cast<Float3*>(centroids);
        auto* dLabels = reinterpret_cast<uint32_t*>(labels);
        auto* dDistances = reinterpret_cast<float*>(distances);
        auto* dSums = reinterpret_cast<Float3*>(sums);
        auto* dClusterSizes = reinterpret_cast<uint32_t*>(clusterSizes);
        cudaStream_t runtimeStream = reinterpret_cast<cudaStream_t>(stream);

        constexpr uint32_t threadsPerBlock = 256;
        const uint32_t pointBlocks = (pointCount + threadsPerBlock - 1u) / threadsPerBlock;
        const uint32_t clusterBlocks = (clusterCount + threadsPerBlock - 1u) / threadsPerBlock;

        for (uint32_t iter = 0; iter < iterations; ++iter)
        {
            if (cudaMemsetAsync(dSums, 0, sizeof(Float3) * clusterCount, runtimeStream) != cudaSuccess)
                return false;
            if (cudaMemsetAsync(dClusterSizes, 0, sizeof(uint32_t) * clusterCount, runtimeStream) != cudaSuccess)
                return false;

            AssignClusters<<<pointBlocks, threadsPerBlock, 0, runtimeStream>>>(
                dPositions,
                pointCount,
                dCentroids,
                clusterCount,
                dLabels,
                dDistances,
                dSums,
                dClusterSizes);
            if (cudaPeekAtLastError() != cudaSuccess)
                return false;

            UpdateCentroids<<<clusterBlocks, threadsPerBlock, 0, runtimeStream>>>(
                dCentroids,
                dSums,
                dClusterSizes,
                clusterCount);
            if (cudaPeekAtLastError() != cudaSuccess)
                return false;
        }

        return true;
    }
}

