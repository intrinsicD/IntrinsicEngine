module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.KMeans;

import Geometry.KDTree;
import Geometry.Properties;

export namespace Geometry::KMeans
{
    enum class Backend : uint8_t
    {
        CPU = 0,
        CUDA = 1,
    };

    enum class Initialization : uint8_t
    {
        Random = 0,
        Hierarchical = 1,
    };

    struct Params
    {
        uint32_t ClusterCount = 8;
        uint32_t MaxIterations = 32;
        float ConvergenceTolerance = 1.0e-4f;
        uint32_t Seed = 42;
        Initialization Init = Initialization::Hierarchical;
        Backend Compute = Backend::CPU;
    };

    struct Result
    {
        std::vector<uint32_t> Labels{};
        std::vector<float> SquaredDistances{};
        std::vector<glm::vec3> Centroids{};
        uint32_t Iterations = 0;
        bool Converged = false;
        float Inertia = 0.0f;
        uint32_t MaxDistanceIndex = 0;
        Backend ActualBackend = Backend::CPU;
    };

    struct CpuScratch
    {
        Geometry::KDTree CentroidTree{};
        Geometry::KDTreeBuildParams CentroidTreeBuildParams{};
        std::vector<Geometry::KDTree::ElementIndex> QueryBuffer{};
    };

    // Lloyd-style k-means on unstructured 3D point sets.
    //
    // Objective:
    //   $$ E(\ell, c) = \sum_{i=1}^{n} \|x_i - c_{\ell_i}\|_2^2 $$
    // where $\ell_i \in \{0, \dots, k-1\}$ is the cluster label and $c_j$
    // is centroid $j$.
    //
    // Experimental Engine24 parity:
    //   - iterative assign/update Lloyd loop,
    //   - random or farthest-point-style hierarchical seeding,
    //   - returns labels, squared distances, centroids, inertia, and farthest sample.
    //
    // Robustness:
    //   - returns nullopt for empty input or zero requested clusters,
    //   - clamps k <= n,
    //   - re-seeds empty clusters from the farthest currently assigned point.
    //
    // Complexity:
    //   - Time: O(n * k * iters)
    //   - Space: O(n + k)
    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params = {});

    // Lloyd-style k-means with externally managed centroid seeds.
    //
    // The initial centroids may come from persistent ECS-owned state such as a
    // retained centroid point-cloud entity. When the provided set contains at
    // least k finite positions, those seeds are used directly; otherwise the
    // operator falls back to the requested initialization policy.
    //
    // CPU scratch owns only temporary acceleration structures. In particular,
    // the implementation rebuilds the centroid KD-tree each iteration because
    // centroid positions are mutable within the solve.
    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const Params& params,
        CpuScratch* cpuScratch);

    // Build an initial centroid set from either persistent ECS-provided seeds
    // or the algorithm's configured seeding policy.
    [[nodiscard]] std::vector<glm::vec3> BuildInitialCentroids(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const Params& params,
        uint32_t clusterCount);

    // Recompute cluster centroids directly from an existing label assignment.
    //
    // This is useful for downstream visualization paths that only persist labels
    // but still need a centroidal Voronoi evaluation of arbitrary samples.
    //
    // Robustness:
    //   - returns zero vectors for empty / invalid clusters,
    //   - ignores labels outside [0, clusterCount),
    //   - ignores non-finite sample positions.
    //
    // Complexity:
    //   - Time: O(n)
    //   - Space: O(k)
    [[nodiscard]] std::vector<glm::vec3> RecomputeCentroids(
        std::span<const glm::vec3> points,
        std::span<const uint32_t> labels,
        uint32_t clusterCount);

    // Classify an arbitrary sample against a centroid set.
    //
    // Returns the nearest centroid index under the squared Euclidean metric:
    //   $$ \ell(x) = \arg\min_j \|x - c_j\|_2^2 $$
    //
    // Robustness:
    //   - returns nullopt for empty centroid sets or non-finite query points,
    //   - skips non-finite centroids.
    //
    // Complexity:
    //   - Time: O(k)
    //   - Space: O(1)
    [[nodiscard]] std::optional<uint32_t> ClassifyPointToCentroid(
        const glm::vec3& point,
        std::span<const glm::vec3> centroids) noexcept;
}
