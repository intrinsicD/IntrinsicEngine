module;

#include <cstddef>
#include <cstdint>
#include <functional>
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
        GPU = 1,
    };

    enum class Initialization : uint8_t
    {
        Random = 0,
        Hierarchical = 1,
        KMeansPlusPlus = 2,
    };

    struct KMeansParams
    {
        uint32_t ClusterCount = 8;
        uint32_t MaxIterations = 32;
        float ConvergenceTolerance = 1.0e-4f;
        uint32_t Seed = 42;
        Initialization Init = Initialization::Hierarchical;
        Backend Compute = Backend::CPU;
    };

    struct KMeansResult
    {
        std::vector<uint32_t> Labels{};
        std::vector<float> SquaredDistances{};
        std::vector<glm::vec3> Centroids{};
        uint32_t Iterations = 0;
        bool Converged = false;
        float Inertia = 0.0f;
        uint32_t MaxDistanceIndex = 0;
        Backend RequestedBackend = Backend::CPU;
        Backend ActualBackend = Backend::CPU;
        bool FellBackToCPU = false;
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
    [[nodiscard]] std::optional<KMeansResult> Cluster(
        std::span<const glm::vec3> points,
        const KMeansParams& params = {});

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
    //
    // `onIteration(completed, maxIterations)` runs after each Lloyd iteration
    // on the calling thread; a run that converges early stops short of
    // `maxIterations`. Callers use it for progress only.
    using IterationObserver = std::function<void(uint32_t completed, uint32_t maxIterations)>;
    [[nodiscard]] std::optional<KMeansResult> Cluster(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
        CpuScratch* cpuScratch,
        const IterationObserver& onIteration = {});

    // Cluster's Lloyd loop one iteration at a time (ARCH-023: a step run's logical step is one assign and update):
    // BeginLloyd seeds the centroids as Cluster does, StepLloyd runs one iteration with Cluster's convergence rule,
    // and FinishLloyd assigns the labels of the current centroids when the last update moved one. Begin, then at
    // most MaxIterations steps until Converged, then Finish, equals Cluster for the same inputs. The state refers to
    // `points` and `cpuScratch`, which must outlive it.
    struct LloydState
    {
        std::span<const glm::vec3> Points{};
        CpuScratch* Scratch{nullptr};
        float ToleranceSquared{0.0f};
        bool CentroidsMoved{false}; // the last update changed a centroid (Finish reassigns)
        float MaxShiftSquared{0.0f}; // the last update's largest squared centroid shift
        KMeansResult Result{};
    };
    enum class LloydStatus : uint8_t
    {
        Continue,
        Converged,
        Failed, // a non-finite point: Cluster returns nullopt
    };
    // nullopt where Cluster returns nullopt for these inputs before its first iteration.
    [[nodiscard]] std::optional<LloydState> BeginLloyd(std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
        CpuScratch* cpuScratch);
    [[nodiscard]] LloydStatus StepLloyd(LloydState& state);
    // The result of the iterations so far (at least one): a copy of `state` to keep stepping, or a moved one.
    [[nodiscard]] std::optional<KMeansResult> FinishLloyd(LloydState state);

    // `count` distinct indices of [0, n), drawn uniformly in order (a partial Fisher-Yates over a sparse swap map):
    // O(count) time and memory, whatever n. Requires count <= n. Random initialization takes its seeds from it.
    [[nodiscard]] std::vector<uint32_t> SampleDistinctIndices(uint32_t n, uint32_t count, uint32_t seed);

    // Build an initial centroid set from either persistent ECS-provided seeds
    // or the algorithm's configured seeding policy. KMeansPlusPlus uses the
    // seeded D² distribution and is deterministic for identical inputs/seed.
    [[nodiscard]] std::vector<glm::vec3> BuildInitialCentroids(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
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
