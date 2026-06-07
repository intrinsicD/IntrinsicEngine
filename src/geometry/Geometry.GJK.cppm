module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <vector>
#include <array>
#include <optional>

export module Geometry.GJK;

import Geometry.Primitives;
import Geometry.Support;
import Core.Memory;

export namespace Geometry::Internal
{
    // --- GJK HELPER STRUCTS ---

    struct MinkowskiDifference
    {
        // Helper to compute A - B support in Minkowski difference
        template <ConvexShape A, ConvexShape B>
        static glm::vec3 Support(const A& a, const B& b, const glm::vec3& dir)
        {
            return Geometry::Support(a, dir) - Geometry::Support(b, -dir);
        }
    };

    struct Simplex
    {
        std::array<glm::vec3, 4> Points{}; // Value-initialize to zeros
        int Size = 0;
        float InvScale = 1.0f; // Normalization factor applied to all points (1/scale)

        void Push(glm::vec3 p) { Points[Size++] = p; }
        glm::vec3& operator[](int i) { return Points[i]; }
        const glm::vec3& operator[](int i) const { return Points[i]; }
    };

    // --- GJK Configuration ---
    // All arithmetic runs in a normalized workspace (~unit scale) so fixed
    // tolerances are well-conditioned regardless of original object size
    // (see GJK_Boolean / GJK_Intersection invScale comment block below).
    //
    // GEOM-015 Slice 2 callsite audit:
    //   Every GJK_EPSILON consumer below operates in this normalized
    //   workspace, so the constant is intentionally a normalized-space
    //   tolerance rather than an original-space magnitude. Each callsite is
    //   tagged with one of:
    //     (a) normalized convergence tolerance — termination / progress /
    //         duplicate-membership tests in ~unit space.
    //     (c) barycentric clamp — dimensionless [0, 1] tolerance on a
    //         barycentric coordinate, not a magnitude.
    //   No (b) original-space magnitude guards exist here; original-space
    //   guards live in Geometry.Support and Geometry.SDFContact and were
    //   migrated to Geometry.RobustPredicates::ApproxZeroSq in that slice.
    //
    // GEOM-015 Slice 3 GJK tolerance contract:
    //   The driver (GJK_Boolean / GJK_Intersection) computes
    //     invScale = 1 / |initial support|
    //   from the first Minkowski-difference support point and multiplies
    //   every subsequent support result by invScale before it enters any
    //   GJK_EPSILON-bearing predicate. The resulting workspace is
    //   dimensionless and bounded by O(shape-ratio) — typically O(1) for
    //   non-degenerate inputs. GJK_EPSILON is therefore a dimensionless
    //   convergence tolerance on this normalized workspace, NOT a
    //   length / magnitude / area in original shape space.
    //
    //   Decision: keep GJK_EPSILON as a normalized-space constant. We do
    //   not thread a per-call scale into the driver because the driver
    //   already factors the scale out via invScale; doing so would double-
    //   normalize and re-introduce the scale dependence we just removed.
    //   See docs/architecture/geometry.md "GJK tolerance contract" for the
    //   full rationale.
    namespace Config
    {
        constexpr float GJK_EPSILON  = 1e-6f;  // Numerical tolerance for GJK convergence (normalized workspace; dimensionless)
        constexpr int   GJK_MAX_ITERATIONS = 64;
        constexpr float EPA_EPSILON  = 1e-4f;  // Tolerance for EPA penetration depth
        constexpr int   EPA_MAX_ITERATIONS = 32;

        // Sanity-pin the GJK_EPSILON contract: it is dimensionless in the
        // ~unit normalized workspace, so it must be strictly positive and
        // strictly smaller than 1. If a future edit pushes GJK_EPSILON
        // outside this band, revisit the docs/architecture/geometry.md
        // "GJK tolerance contract" and the GEOM-015 callsite audit before
        // changing this value.
        static_assert(GJK_EPSILON > 0.0f && GJK_EPSILON < 1.0f,
                      "GJK_EPSILON must be a dimensionless tolerance in (0, 1) "
                      "on the normalized GJK workspace; see GEOM-015 Slice 3.");
    }

    // --- GJK Termination Diagnostics ---
    //
    // GEOM-015 Slice 4 surfaces *why* a GJK driver exited so callers can
    // distinguish geometric outcomes from numerical-degeneracy fallbacks
    // without re-running the algorithm. The boolean entry points stay as
    // thin wrappers; callers that want diagnostics opt in via the
    // out-param overload.
    enum class TerminationReason : unsigned char
    {
        // NextSimplex confirmed origin is contained, or the search
        // direction / current support collapsed to ~zero in the
        // normalized workspace ⇒ origin is on the simplex.
        Converged,
        // dot(support, direction) < GJK_EPSILON: the latest support
        // point did not extend strictly past the origin along the
        // search direction ⇒ a separating axis exists.
        EarlyOutNegativeSupport,
        // The latest support point duplicates an existing simplex
        // vertex (within GJK_EPSILON in normalized space) ⇒ no further
        // progress is possible. Typically reached on edge-aligned
        // degenerate inputs.
        NoSimplexProgress,
        // The iteration budget Config::GJK_MAX_ITERATIONS was exhausted
        // before any of the above terminating conditions fired. Expected
        // to be very rare on non-pathological inputs; persistent hits
        // indicate either a misbehaving support function or a
        // numerical-stability regression worth investigating.
        MaxIterationsHit,
    };

    struct GJKDiagnostics
    {
        // Number of main-loop iterations executed (each iteration runs
        // one Minkowski-difference support query). The initial seed
        // support is *not* counted; the first new support is iteration 1.
        int iterations = 0;
        TerminationReason reason = TerminationReason::Converged;
    };

    namespace Detail
    {
        [[nodiscard]] inline glm::vec3 TripleProduct(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            return glm::cross(glm::cross(a, b), c);
        }

        // (a) normalized convergence tolerance: early-out / zero-direction guard.
        // Operates on the normalized simplex workspace; |v|² ≤ EPS² ⇒ direction
        // has collapsed to ~zero and origin is effectively on the current simplex.
        [[nodiscard]] inline bool NearlyZero(const glm::vec3& v) noexcept
        {
            return glm::length2(v) <= Config::GJK_EPSILON * Config::GJK_EPSILON;
        }
    }

    // --- GJK IMPLEMENTATION ---

    // Handles the logic of processing the simplex to see if it contains origin.
    // Returns true if intersection found, updates direction for next search.
    // Operates in normalized space — fixed epsilons are always well-conditioned.
    bool NextSimplex(Simplex& points, glm::vec3& direction);


    // =========================================================================
    // Normalized-workspace GJK
    //
    // The first Minkowski-difference support point determines the workspace
    // scale. All subsequent support results are multiplied by 1/scale so that
    // the simplex lives in ~unit space. This makes fixed tolerances
    // (GJK_EPSILON) reliable at any object scale — cross products, dot
    // products, and triple products all operate on O(1) magnitudes.
    //
    // For GJK_Boolean the result is just a bool; no unscaling needed.
    // For GJK_Intersection the returned simplex is in normalized space;
    // EPA_Solver receives it as-is and performs the same normalization on its
    // own support queries, then unscales the final penetration depth.
    // =========================================================================

    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b, Core::Memory::LinearArena& /*scratch*/, GJKDiagnostics& diag)
    {
        // Currently allocation-free; scratch is plumbed for consistency with EPA.
        diag = {};

        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        // Compute normalization scale from the initial support point.
        const float scale = glm::length(support);
        const float invScale = (scale > 1e-30f) ? (1.0f / scale) : 1.0f;

        // Normalize the initial support into ~unit space.
        support *= invScale;

        if (Detail::NearlyZero(support))
        {
            diag.reason = TerminationReason::Converged;
            return true;
        }

        Simplex points;
        points.Push(support);

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (true)
        {
            ++diag.iterations;

            if (Detail::NearlyZero(direction))
            {
                diag.reason = TerminationReason::Converged;
                return true;
            }

            // Support query in original space, then normalize.
            support = MinkowskiDifference::Support(a, b, direction) * invScale;

            if (Detail::NearlyZero(support))
            {
                diag.reason = TerminationReason::Converged;
                return true;
            }

            // (a) normalized convergence tolerance: support-progress test.
            // In normalized workspace, a new support point that does not
            // strictly extend past the origin along `direction` means GJK has
            // converged on a separating axis.
            if (glm::dot(support, direction) < Config::GJK_EPSILON)
            {
                diag.reason = TerminationReason::EarlyOutNegativeSupport;
                return false;
            }

            bool duplicate = false;
            for (int i = 0; i < points.Size; ++i)
            {
                // (a) normalized convergence tolerance: simplex-membership /
                // duplicate test. Two normalized-space supports within EPS of
                // each other ⇒ no further progress is possible.
                if (glm::length2(points[i] - support) <= Config::GJK_EPSILON * Config::GJK_EPSILON)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                diag.reason = TerminationReason::NoSimplexProgress;
                return false;
            }

            points.Push(support);

            if (NextSimplex(points, direction))
            {
                diag.reason = TerminationReason::Converged;
                return true;
            }

            if (points.Size > 0 && --maxIterations == 0)
            {
                diag.reason = TerminationReason::MaxIterationsHit;
                return false;
            }
        }
    }

    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b, Core::Memory::LinearArena& scratch)
    {
        GJKDiagnostics diag;
        return GJK_Boolean(a, b, scratch, diag);
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b, Core::Memory::LinearArena& /*scratch*/, GJKDiagnostics& diag)
    {
        diag = {};

        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        const float scale = glm::length(support);
        const float invScale = (scale > 1e-30f) ? (1.0f / scale) : 1.0f;

        support *= invScale;

        Simplex points;
        points.InvScale = invScale;
        points.Push(support);

        if (Detail::NearlyZero(support))
        {
            diag.reason = TerminationReason::Converged;
            return points;
        }

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (maxIterations-- > 0)
        {
            ++diag.iterations;

            if (Detail::NearlyZero(direction))
            {
                diag.reason = TerminationReason::Converged;
                return points;
            }

            support = MinkowskiDifference::Support(a, b, direction) * invScale;

            if (Detail::NearlyZero(support))
            {
                points.Push(support);
                diag.reason = TerminationReason::Converged;
                return points;
            }

            // (a) normalized convergence tolerance: support-progress test
            // (Intersection variant; mirrors GJK_Boolean).
            if (glm::dot(support, direction) < Config::GJK_EPSILON)
            {
                diag.reason = TerminationReason::EarlyOutNegativeSupport;
                return std::nullopt;
            }

            for (int i = 0; i < points.Size; ++i)
            {
                // (a) normalized convergence tolerance: simplex-membership /
                // duplicate test (Intersection variant; mirrors GJK_Boolean).
                if (glm::length2(points[i] - support) <= Config::GJK_EPSILON * Config::GJK_EPSILON)
                {
                    diag.reason = TerminationReason::NoSimplexProgress;
                    return std::nullopt;
                }
            }

            points.Push(support);
            if (NextSimplex(points, direction))
            {
                diag.reason = TerminationReason::Converged;
                return points;
            }
        }
        diag.reason = TerminationReason::MaxIterationsHit;
        return std::nullopt;
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b, Core::Memory::LinearArena& scratch)
    {
        GJKDiagnostics diag;
        return GJK_Intersection(a, b, scratch, diag);
    }

    // Back-compat overloads (existing call sites): route through the scratch-taking versions.
    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b)
    {
        Core::Memory::LinearArena scratch(8 * 1024);
        return GJK_Boolean(a, b, scratch);
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b)
    {
        Core::Memory::LinearArena scratch(8 * 1024);
        return GJK_Intersection(a, b, scratch);
    }
}
