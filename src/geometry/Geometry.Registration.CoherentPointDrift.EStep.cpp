module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Eigen/Dense>

module Geometry.Registration.CoherentPointDrift.EStep;

import Geometry.PointSampling;

namespace Geometry::CoherentPointDrift
{
    std::string_view ToString(const EStepPolicy value) noexcept
    {
        switch (value)
        {
        case EStepPolicy::Reference: return "reference";
        case EStepPolicy::Dense: return "dense";
        case EStepPolicy::Truncated: return "truncated";
        case EStepPolicy::Auto: return "auto";
        case EStepPolicy::FastGauss: return "fast_gauss";
        case EStepPolicy::Nystrom: return "nystrom";
        case EStepPolicy::Vulkan: return "vulkan";
        }
        return "unknown";
    }

    std::string_view BackendId(const EStepPolicy value) noexcept
    {
        switch (value)
        {
        case EStepPolicy::Reference: return "cpu_reference";
        case EStepPolicy::Dense: return "cpu_dense_parallel";
        case EStepPolicy::Truncated: return "cpu_truncated";
        case EStepPolicy::Auto: return "cpu_auto";
        case EStepPolicy::FastGauss: return "cpu_ifgt";
        case EStepPolicy::Nystrom: return "cpu_nystrom";
        case EStepPolicy::Vulkan: return "gpu_vulkan_fp32_dense";
        }
        return "unknown";
    }
}

namespace Geometry::CoherentPointDrift::EStep
{
    namespace
    {
        constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
        constexpr std::size_t kRowGrain = 64;

        // Axis-aligned box (lo, hi) of a non-empty point set, padded by `pad` on every side.
        std::array<Eigen::Vector3d, 2> PaddedBox(const PointSet points, const double pad)
        {
            Eigen::Vector3d lo = Eigen::Vector3d::Constant(std::numeric_limits<double>::infinity()), hi = -lo;
            for (std::size_t i = 0; i < points.Size(); ++i)
            {
                const Eigen::Vector3d p{points.X[i], points.Y[i], points.Z[i]};
                lo = lo.cwiseMin(p);
                hi = hi.cwiseMax(p);
            }
            return {lo.array() - pad, hi.array() + pad};
        }

        // METHOD-076: `count` points uniform over the union of two boxes. Each candidate picks a
        // box with probability proportional to its volume and a point in it, and is dropped with
        // probability 1/2 when it lies in both boxes, so kept points are uniform over the union.
        // The candidate stream depends only on the boxes, pattern and seed: a longer request
        // extends a shorter one by its prefix. Halton uses bases 2, 3, 5, 7, 11 shifted modulo 1
        // by seeded offsets (Cranley-Patterson). Stops early only after 64 candidates per point.
        void UnionBoxPoints(const std::array<Eigen::Vector3d, 2>& a, const std::array<Eigen::Vector3d, 2>& b,
                            const std::size_t count, const NystromBoxPattern pattern, const std::uint64_t seed,
                            std::vector<double>& x, std::vector<double>& y, std::vector<double>& z)
        {
            const double va = (a[1] - a[0]).prod(), vb = (b[1] - b[0]).prod();
            const auto inside = [](const Eigen::Vector3d& p, const std::array<Eigen::Vector3d, 2>& box) {
                return (p.array() >= box[0].array()).all() && (p.array() <= box[1].array()).all();
            };
            const auto radical = [](std::uint64_t index, const std::uint32_t base) {
                double result = 0.0, digit = 1.0 / double(base);
                for (; index > 0u; index /= base, digit /= double(base)) result += double(index % base) * digit;
                return result;
            };
            constexpr std::array<std::uint32_t, 5> kBases{2u, 3u, 5u, 7u, 11u};
            std::mt19937_64 random(seed);
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            std::array<double, 5> shift{};
            for (double& s : shift) s = unit(random);
            for (std::uint64_t k = 1u; x.size() < count && k <= 64u * std::uint64_t(count); ++k)
            {
                std::array<double, 5> u{};
                for (std::size_t d = 0; d < u.size(); ++d)
                    u[d] = pattern == NystromBoxPattern::Halton ? std::fmod(radical(k, kBases[d]) + shift[d], 1.0) : unit(random);
                const auto& box = u[0] * (va + vb) < va ? a : b;
                const Eigen::Vector3d p = box[0] + (box[1] - box[0]).cwiseProduct(Eigen::Vector3d(u[1], u[2], u[3]));
                if (inside(p, a) && inside(p, b) && u[4] >= 0.5) continue;
                x.push_back(p.x()); y.push_back(p.y()); z.push_back(p.z());
            }
        }

        // The calling thread's cancellation predicate (CancellationScope).
        thread_local const std::function<bool()>* tCancelled = nullptr;

        // Runs body(begin, end) over [0, count) in fixed-size chunks. Each index is handled
        // by exactly one call, so per-index outputs do not depend on the thread count. Under a
        // CancellationScope the remaining chunks are skipped once the predicate returns true.
        template <class Body>
        void ParallelFor(const std::size_t count, const std::size_t grain, const std::uint32_t threads, Body&& body)
        {
            const std::size_t chunks = (count + grain - 1) / grain;
            const std::size_t workers = std::min<std::size_t>(threads, chunks);
            const std::function<bool()>* cancelled = tCancelled;
            const auto stop = [cancelled] { return cancelled != nullptr && *cancelled && (*cancelled)(); };
            std::atomic<std::size_t> next{0};
            const auto work = [&]
            {
                for (std::size_t chunk = next.fetch_add(1); chunk < chunks && !stop(); chunk = next.fetch_add(1))
                    body(chunk * grain, std::min(count, (chunk + 1) * grain));
            };
            if (workers <= 1)
            {
                work();
                return;
            }
            std::vector<std::jthread> pool;
            pool.reserve(workers - 1);
            for (std::size_t t = 1; t < workers; ++t) pool.emplace_back(work);
            work();
        }

        [[nodiscard]] inline double Squared(const double ax, const double ay, const double az, const double bx,
                                            const double by, const double bz) noexcept
        {
            const double dx = ax - bx, dy = ay - by, dz = az - bz;
            return dx * dx + dy * dy + dz * dz;
        }

        // exp(x) for x <= 0 without branches, so loops over it vectorize: magic-constant rounding
        // to k = round(x / ln 2), Cody-Waite reduction and a degree-13 Taylor polynomial on
        // |r| <= ln(2)/2 (truncation error < 5e-18), scaled by 2^k from the exponent bits.
        // Relative error below 3e-16; arguments below -708 return 0 (std::exp: < 3.3e-308).
        [[nodiscard]] inline double ExpNonPositive(const double x) noexcept
        {
            constexpr double kLog2e = 1.4426950408889634, kLn2Hi = 0.6931471803691238, kLn2Lo = 1.9082149292705877e-10;
            constexpr double kShifter = 6755399441055744.0; // 1.5 * 2^52
            const double clamped = std::max(x, -708.0);
            const double shifted = clamped * kLog2e + kShifter;
            const double k = shifted - kShifter;
            const double r = (clamped - k * kLn2Hi) - k * kLn2Lo;
            double p = 1.0 / 6227020800.0;
            p = p * r + 1.0 / 479001600.0;
            p = p * r + 1.0 / 39916800.0;
            p = p * r + 1.0 / 3628800.0;
            p = p * r + 1.0 / 362880.0;
            p = p * r + 1.0 / 40320.0;
            p = p * r + 1.0 / 5040.0;
            p = p * r + 1.0 / 720.0;
            p = p * r + 1.0 / 120.0;
            p = p * r + 1.0 / 24.0;
            p = p * r + 1.0 / 6.0;
            p = p * r + 0.5;
            p = p * r + 1.0;
            p = p * r + 1.0;
            const std::int64_t integer = std::bit_cast<std::int64_t>(shifted) - std::bit_cast<std::int64_t>(kShifter);
            const double scale = std::bit_cast<double>((integer + 1023) << 52);
            return x >= -708.0 ? p * scale : 0.0;
        }

        // values[i] <- exp((minimum - values[i]) * inverseTwoSigma2 + logWeight[i]) with logWeight <= 0;
        // returns their sum (added in index order). Compiled for AVX2 and a baseline target.
        __attribute__((target_clones("avx2", "default")))
        double ExponentiateShifted(double* values, const double* logWeight, const std::size_t count, const double minimum,
                                   const double inverseTwoSigma2) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
                values[i] = ExpNonPositive((minimum - values[i]) * inverseTwoSigma2 + logWeight[i]);
            double sum = 0.0;
            for (std::size_t i = 0; i < count; ++i) sum += values[i];
            return sum;
        }

        // Dense row: out[i] = exp((minimum - |q - s_i|^2) * inverseTwoSigma2 + logWeight[i]) with minimum
        // the row's smallest squared distance and logWeight <= 0; returns the sum in index order.
        __attribute__((target_clones("avx2", "default")))
        double DenseRowTerms(const double* sx, const double* sy, const double* sz, const double* logWeight,
                             const std::size_t count, const double qx, const double qy, const double qz, const double minimum,
                             const double inverseTwoSigma2, double* out) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
                out[i] = ExpNonPositive((minimum - Squared(qx, qy, qz, sx[i], sy[i], sz[i])) * inverseTwoSigma2 + logWeight[i]);
            double sum = 0.0;
            for (std::size_t i = 0; i < count; ++i) sum += out[i];
            return sum;
        }

        // Weighted rows: values[i] <- exp(values[i] - max_j values[j]) for exponents already holding
        // the source weights; returns the sum (index order) and the maximum in `maximum`, so the
        // largest weighted term is exactly 1 whatever the weights.
        __attribute__((target_clones("avx2", "default")))
        double ExponentiateRelativeToMax(double* values, const std::size_t count, double& maximum) noexcept
        {
            double top = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < count; ++i) top = std::max(top, values[i]);
            maximum = count > 0 ? top : 0.0;
            for (std::size_t i = 0; i < count; ++i) values[i] = ExpNonPositive(values[i] - maximum);
            double sum = 0.0;
            for (std::size_t i = 0; i < count; ++i) sum += values[i];
            return sum;
        }

        // Weighted dense row exponents: out[i] = (minimum - |q - s_i|^2) * inverseTwoSigma2 + logWeight[i].
        __attribute__((target_clones("avx2", "default")))
        void DenseRowExponents(const double* sx, const double* sy, const double* sz, const double* logWeight,
                               const std::size_t count, const double qx, const double qy, const double qz,
                               const double minimum, const double inverseTwoSigma2, double* out) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
                out[i] = (minimum - Squared(qx, qy, qz, sx[i], sy[i], sz[i])) * inverseTwoSigma2 + logWeight[i];
        }

        // Adds scale * terms[i] (times x, y, z) to one block's partial P1 and PX.
        __attribute__((target_clones("avx2", "default")))
        void ScatterRow(const double* terms, const std::size_t count, const double scale, const double x, const double y,
                        const double z, double* p1, double* px, double* py, double* pz) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const double p = terms[i] * scale;
                p1[i] += p;
                px[i] += p * x;
                py[i] += p * y;
                pz[i] += p * z;
            }
        }

        // Source row of the two-pass form: out[j] = exp(-|t_j - q|^2 / 2 sigma^2 + logWeight - logDenominator[j]).
        __attribute__((target_clones("avx2", "default")))
        void SourceRowTerms(const double* tx, const double* ty, const double* tz, const double* logDenominator,
                            const std::size_t count, const double qx, const double qy, const double qz,
                            const double inverseTwoSigma2, const double logWeight, double* out) noexcept
        {
            // The argument is <= 0 up to rounding: the term is part of its row's denominator.
            for (std::size_t j = 0; j < count; ++j)
                out[j] = ExpNonPositive(
                    std::min(-Squared(tx[j], ty[j], tz[j], qx, qy, qz) * inverseTwoSigma2 + logWeight - logDenominator[j], 0.0));
        }

        // log(sum_m exp(a_m) + c) from the shifted sum S = sum_m exp(a_m - a_max), as in the reference.
        [[nodiscard]] inline double LogSumWithOutlier(const double aMax, const double shiftedSum, const double logC) noexcept
        {
            const double logOutlier = logC - aMax;
            return logOutlier > 700.0 ? logC + std::log1p(shiftedSum * std::exp(-logOutlier))
                                      : aMax + std::log(shiftedSum + std::exp(logOutlier));
        }

        // Static balanced kd-tree over double points; coordinates are copied in tree order.
        class KdTree
        {
        public:
            struct Node
            {
                std::array<double, 3> Lo{}, Hi{};
                std::uint32_t Begin{0u}, End{0u}, Left{kNone}, Right{kNone};
                double MaxRadius2{0.0};
            };

            void Build(const PointSet& points)
            {
                const std::size_t n = points.Size();
                m_Index.resize(n);
                for (std::size_t i = 0; i < n; ++i) m_Index[i] = std::uint32_t(i);
                m_Nodes.clear();
                if (n == 0) return;
                const std::array<const double*, 3> axes{points.X.data(), points.Y.data(), points.Z.data()};
                m_Nodes.push_back(Node{.Begin = 0u, .End = std::uint32_t(n)});
                std::vector<std::uint32_t> stack{0u};
                while (!stack.empty())
                {
                    const std::uint32_t id = stack.back();
                    stack.pop_back();
                    Node node = m_Nodes[id];
                    node.Lo.fill(std::numeric_limits<double>::infinity());
                    node.Hi.fill(-std::numeric_limits<double>::infinity());
                    for (std::uint32_t i = node.Begin; i < node.End; ++i)
                        for (int a = 0; a < 3; ++a)
                        {
                            node.Lo[a] = std::min(node.Lo[a], axes[a][m_Index[i]]);
                            node.Hi[a] = std::max(node.Hi[a], axes[a][m_Index[i]]);
                        }
                    if (node.End - node.Begin > kLeafSize)
                    {
                        int axis = 0;
                        for (int a = 1; a < 3; ++a)
                            if (node.Hi[a] - node.Lo[a] > node.Hi[axis] - node.Lo[axis]) axis = a;
                        const double* coordinate = axes[axis];
                        const std::uint32_t mid = node.Begin + (node.End - node.Begin) / 2u;
                        std::nth_element(m_Index.begin() + node.Begin, m_Index.begin() + mid, m_Index.begin() + node.End,
                                         [coordinate](const std::uint32_t a, const std::uint32_t b)
                                         { return coordinate[a] < coordinate[b] || (coordinate[a] == coordinate[b] && a < b); });
                        node.Left = std::uint32_t(m_Nodes.size());
                        node.Right = node.Left + 1u;
                        m_Nodes.push_back(Node{.Begin = node.Begin, .End = mid});
                        m_Nodes.push_back(Node{.Begin = mid, .End = node.End});
                        stack.push_back(node.Right);
                        stack.push_back(node.Left);
                    }
                    m_Nodes[id] = node;
                }
                m_X.resize(n); m_Y.resize(n); m_Z.resize(n);
                for (std::size_t i = 0; i < n; ++i)
                {
                    m_X[i] = points.X[m_Index[i]];
                    m_Y[i] = points.Y[m_Index[i]];
                    m_Z[i] = points.Z[m_Index[i]];
                }
            }

            [[nodiscard]] std::size_t Size() const noexcept { return m_Index.size(); }
            [[nodiscard]] std::uint32_t Original(const std::size_t slot) const noexcept { return m_Index[slot]; }
            [[nodiscard]] double X(const std::size_t slot) const noexcept { return m_X[slot]; }
            [[nodiscard]] double Y(const std::size_t slot) const noexcept { return m_Y[slot]; }
            [[nodiscard]] double Z(const std::size_t slot) const noexcept { return m_Z[slot]; }

            [[nodiscard]] double NearestSquared(const double qx, const double qy, const double qz) const
            {
                return Nearest(qx, qy, qz).first;
            }

            // Smallest squared distance and its slot (the first slot in tree order among ties).
            [[nodiscard]] std::pair<double, std::size_t> Nearest(const double qx, const double qy, const double qz) const
            {
                double best = std::numeric_limits<double>::infinity();
                std::size_t bestSlot = 0u;
                if (m_Nodes.empty()) return {best, bestSlot};
                std::array<std::uint32_t, 128> stack{};
                std::size_t top = 0;
                stack[top++] = 0u;
                while (top > 0)
                {
                    const Node& node = m_Nodes[stack[--top]];
                    if (BoxDistance2(node, qx, qy, qz) >= best) continue;
                    if (node.Left == kNone)
                    {
                        for (std::uint32_t i = node.Begin; i < node.End; ++i)
                            if (const double d2 = Squared(qx, qy, qz, m_X[i], m_Y[i], m_Z[i]); d2 < best)
                            {
                                best = d2;
                                bestSlot = i;
                            }
                        continue;
                    }
                    const double left = BoxDistance2(m_Nodes[node.Left], qx, qy, qz);
                    const double right = BoxDistance2(m_Nodes[node.Right], qx, qy, qz);
                    // Push the farther child first so the nearer one is visited next.
                    if (left <= right) { stack[top++] = node.Right; stack[top++] = node.Left; }
                    else { stack[top++] = node.Left; stack[top++] = node.Right; }
                }
                return {best, bestSlot};
            }

            // visit(slot, d2) for every point with d2 <= radius2, in tree order.
            template <class Visit>
            void ForEachWithin(const double qx, const double qy, const double qz, const double radius2, Visit&& visit) const
            {
                if (m_Nodes.empty()) return;
                std::array<std::uint32_t, 128> stack{};
                std::size_t top = 0;
                stack[top++] = 0u;
                while (top > 0)
                {
                    const Node& node = m_Nodes[stack[--top]];
                    if (BoxDistance2(node, qx, qy, qz) > radius2) continue;
                    if (node.Left == kNone)
                    {
                        for (std::uint32_t i = node.Begin; i < node.End; ++i)
                            if (const double d2 = Squared(qx, qy, qz, m_X[i], m_Y[i], m_Z[i]); d2 <= radius2)
                                visit(i, d2);
                        continue;
                    }
                    stack[top++] = node.Right;
                    stack[top++] = node.Left;
                }
            }

            // Per-point radii (tree order); refreshes the subtree maxima used for pruning.
            void SetRadii(std::span<const double> radius2)
            {
                m_Radius2.assign(radius2.begin(), radius2.end());
                for (std::size_t id = m_Nodes.size(); id-- > 0;)
                {
                    Node& node = m_Nodes[id];
                    if (node.Left == kNone)
                    {
                        double maximum = 0.0;
                        for (std::uint32_t i = node.Begin; i < node.End; ++i) maximum = std::max(maximum, m_Radius2[i]);
                        node.MaxRadius2 = maximum;
                    }
                    else
                        node.MaxRadius2 = std::max(m_Nodes[node.Left].MaxRadius2, m_Nodes[node.Right].MaxRadius2);
                }
            }

            // visit(slot, d2) for every point p with |q - p|^2 <= radius2(p), in tree order.
            template <class Visit>
            void ForEachReaching(const double qx, const double qy, const double qz, Visit&& visit) const
            {
                if (m_Nodes.empty()) return;
                std::array<std::uint32_t, 128> stack{};
                std::size_t top = 0;
                stack[top++] = 0u;
                while (top > 0)
                {
                    const Node& node = m_Nodes[stack[--top]];
                    if (BoxDistance2(node, qx, qy, qz) > node.MaxRadius2) continue;
                    if (node.Left == kNone)
                    {
                        for (std::uint32_t i = node.Begin; i < node.End; ++i)
                            if (const double d2 = Squared(qx, qy, qz, m_X[i], m_Y[i], m_Z[i]); d2 <= m_Radius2[i])
                                visit(i, d2);
                        continue;
                    }
                    stack[top++] = node.Right;
                    stack[top++] = node.Left;
                }
            }

        private:
            static constexpr std::uint32_t kLeafSize = 16u;

            [[nodiscard]] static double BoxDistance2(const Node& node, const double qx, const double qy, const double qz) noexcept
            {
                const double dx = std::max({node.Lo[0] - qx, 0.0, qx - node.Hi[0]});
                const double dy = std::max({node.Lo[1] - qy, 0.0, qy - node.Hi[1]});
                const double dz = std::max({node.Lo[2] - qz, 0.0, qz - node.Hi[2]});
                return dx * dx + dy * dy + dz * dz;
            }

            std::vector<Node> m_Nodes{};
            std::vector<std::uint32_t> m_Index{};
            std::vector<double> m_X{}, m_Y{}, m_Z{}, m_Radius2{};
        };

        [[nodiscard]] double Diagonal(const PointSet& a, const PointSet& b) noexcept
        {
            std::array<double, 3> lo{}, hi{};
            lo.fill(std::numeric_limits<double>::infinity());
            hi.fill(-std::numeric_limits<double>::infinity());
            for (const PointSet* set : {&a, &b})
                for (std::size_t i = 0; i < set->Size(); ++i)
                {
                    const std::array<double, 3> p{set->X[i], set->Y[i], set->Z[i]};
                    for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
                }
            double d2 = 0.0;
            for (int k = 0; k < 3; ++k) d2 += (hi[k] - lo[k]) * (hi[k] - lo[k]);
            return std::sqrt(d2);
        }
    }

    namespace
    {
        // Monomials u^a of total degree < Order in 3-D in the graded order of Yang et al.
        // (2003): term t = term Parent[t] times u[Axis[t]]; Constant[t] = 2^|a| / a!.
        struct MonomialLayout
        {
            std::uint32_t Order{0u};
            std::vector<std::uint32_t> Parent{};
            std::vector<std::uint8_t> Axis{};
            std::vector<double> Constant{};

            void Build(const std::uint32_t order)
            {
                if (order == Order) return;
                Order = order;
                Parent.assign(1, 0u);
                Axis.assign(1, 0u);
                Constant.assign(1, 1.0);
                std::vector<std::array<std::uint32_t, 3>> exponent{{0u, 0u, 0u}};
                std::array<std::size_t, 3> heads{0u, 0u, 0u};
                for (std::uint32_t degree = 1; degree < order; ++degree)
                {
                    const std::size_t previousEnd = Parent.size();
                    for (std::uint8_t axis = 0; axis < 3; ++axis)
                    {
                        const std::size_t start = heads[axis];
                        heads[axis] = Parent.size();
                        for (std::size_t j = start; j < previousEnd; ++j)
                        {
                            auto a = exponent[j];
                            ++a[axis];
                            exponent.push_back(a);
                            Parent.push_back(std::uint32_t(j));
                            Axis.push_back(axis);
                            Constant.push_back(Constant[j] * 2.0 / double(a[axis]));
                        }
                    }
                }
            }
            [[nodiscard]] std::size_t Size() const noexcept { return Parent.size(); }
            void Evaluate(const std::array<double, 3>& u, double* out) const noexcept
            {
                out[0] = 1.0;
                for (std::size_t t = 1; t < Parent.size(); ++t) out[t] = out[Parent[t]] * u[Axis[t]];
            }
        };

        // Gonzalez farthest-point clustering from point 0 (ties: lowest index). Radius[k] is the
        // largest point-to-nearest-center distance with the first k + 1 centers.
        struct Clustering
        {
            std::vector<std::uint32_t> Centers{};
            std::vector<double> Radius{};
            std::vector<std::uint32_t> Label{};
        };

        void FarthestPointClusters(const PointSet& points, const std::size_t count, const std::uint32_t requestedThreads,
                                   Clustering& out)
        {
            const std::size_t m = points.Size();
            // Each center is one O(M) sweep; below this size spawning workers per center costs more.
            const std::uint32_t threads = m >= 65536u ? requestedThreads : 1u;
            out.Centers.clear();
            out.Radius.clear();
            out.Label.assign(m, 0u);
            std::vector<double> distance(m, std::numeric_limits<double>::infinity());
            constexpr std::size_t kGrain = 4096;
            const std::size_t chunks = (m + kGrain - 1) / kGrain;
            std::vector<std::pair<double, std::size_t>> best(chunks);
            std::size_t next = 0;
            while (out.Centers.size() < std::min(count, m))
            {
                const std::uint32_t label = std::uint32_t(out.Centers.size());
                out.Centers.push_back(std::uint32_t(next));
                const double cx = points.X[next], cy = points.Y[next], cz = points.Z[next];
                ParallelFor(m, kGrain, threads, [&](const std::size_t begin, const std::size_t end)
                {
                    std::pair<double, std::size_t> local{-1.0, begin};
                    for (std::size_t i = begin; i < end; ++i)
                    {
                        const double d2 = Squared(points.X[i], points.Y[i], points.Z[i], cx, cy, cz);
                        if (d2 < distance[i]) { distance[i] = d2; out.Label[i] = label; }
                        if (distance[i] > local.first) local = {distance[i], i};
                    }
                    best[begin / kGrain] = local;
                });
                std::pair<double, std::size_t> farthest{-1.0, 0u};
                for (const auto& candidate : best)
                    if (candidate.first > farthest.first) farthest = candidate;
                out.Radius.push_back(std::sqrt(std::max(farthest.first, 0.0)));
                if (!(farthest.first > 0.0)) break;
                next = farthest.second;
            }
        }

        struct IfgtPlan
        {
            std::size_t Clusters{0u};
            std::uint32_t Order{0u};
            double Cutoff{0.0};  // center-to-target distance beyond which a cluster is skipped
            double UnitError{0.0}; // bound on |error| per unit of source weight
            double Cost{std::numeric_limits<double>::infinity()};
        };

        // Raykar et al. (2005): per unit weight |E| <= (2 r_x r_y / h^2)^p / p! + exp(-(r_y - r_x)^2 / h^2)
        // with r_x the source cluster radius and r_y the target cutoff from the center.
        IfgtPlan PlanIfgt(const std::vector<double>& radius, const double h, const double epsilon, const std::size_t targets,
                          const std::size_t sources, const std::size_t channels)
        {
            constexpr std::uint32_t kMaxOrder = 24u;
            IfgtPlan plan{};
            if (!(epsilon > 1e-300) || radius.empty()) return plan;
            const double reach = h * std::sqrt(std::log(2.0 / epsilon));
            for (std::size_t k = 1; k <= radius.size(); k = k < radius.size() && k * 2 > radius.size() ? radius.size() : k * 2)
            {
                const double rx = radius[k - 1];
                const double ry = rx + reach;
                const double a = 2.0 * rx * ry / (h * h);
                double term = 1.0;
                std::uint32_t order = 0u;
                for (std::uint32_t p = 1; p <= kMaxOrder; ++p)
                {
                    term *= a / double(p);
                    if (term <= 0.5 * epsilon) { order = p; break; }
                }
                if (order > 0u)
                {
                    const double terms = double(order) * double(order + 1) * double(order + 2) / 6.0;
                    const double cost = (double(sources) + double(targets) * double(k)) * terms * double(channels + 1);
                    if (cost < plan.Cost)
                        plan = {.Clusters = k, .Order = order, .Cutoff = ry,
                                .UnitError = term + std::exp(-(reach * reach) / (h * h)), .Cost = cost};
                }
                if (k == radius.size()) break;
            }
            return plan;
        }

        // out[c * targets + j] = sum_i weight[c * sources + i] exp(-|t_j - s_i|^2 / h^2) through the
        // improved fast Gauss transform with the plan's clusters (labels from FarthestPointClusters
        // run to exactly plan.Clusters centers).
        void ImprovedFastGaussTransform(const PointSet& sources, const Clustering& clusters, std::span<const double> weight,
                                        const std::size_t channels, const PointSet& targets, const double h,
                                        const IfgtPlan& plan, MonomialLayout& layout, std::span<double> out,
                                        const std::uint32_t threads)
        {
            layout.Build(plan.Order);
            const std::size_t terms = layout.Size(), m = sources.Size(), n = targets.Size(), k = plan.Clusters;
            const double inverseH = 1.0 / h;
            std::vector<std::uint32_t> start(k + 1, 0u), order(m);
            for (std::size_t i = 0; i < m; ++i) ++start[clusters.Label[i] + 1u];
            for (std::size_t c = 0; c < k; ++c) start[c + 1] += start[c];
            {
                std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
                for (std::size_t i = 0; i < m; ++i) order[fill[clusters.Label[i]]++] = std::uint32_t(i);
            }
            std::vector<double> coefficient(k * channels * terms, 0.0);
            ParallelFor(k, 1, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> mono(terms);
                for (std::size_t c = begin; c < end; ++c)
                {
                    const std::uint32_t center = clusters.Centers[c];
                    double* coef = coefficient.data() + c * channels * terms;
                    for (std::uint32_t slot = start[c]; slot < start[c + 1]; ++slot)
                    {
                        const std::uint32_t i = order[slot];
                        const std::array<double, 3> u{(sources.X[i] - sources.X[center]) * inverseH,
                                                      (sources.Y[i] - sources.Y[center]) * inverseH,
                                                      (sources.Z[i] - sources.Z[center]) * inverseH};
                        const double g = std::exp(-(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]));
                        layout.Evaluate(u, mono.data());
                        for (std::size_t ch = 0; ch < channels; ++ch)
                        {
                            const double wg = weight[ch * m + i] * g;
                            double* row = coef + ch * terms;
                            for (std::size_t t = 0; t < terms; ++t) row[t] += wg * mono[t];
                        }
                    }
                    for (std::size_t ch = 0; ch < channels; ++ch)
                        for (std::size_t t = 0; t < terms; ++t) coef[ch * terms + t] *= layout.Constant[t];
                }
            });
            const double cutoff2 = plan.Cutoff * plan.Cutoff;
            ParallelFor(n, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> mono(terms), sum(channels);
                for (std::size_t j = begin; j < end; ++j)
                {
                    std::fill(sum.begin(), sum.end(), 0.0);
                    for (std::size_t c = 0; c < k; ++c)
                    {
                        const std::uint32_t center = clusters.Centers[c];
                        const std::array<double, 3> d{targets.X[j] - sources.X[center], targets.Y[j] - sources.Y[center],
                                                      targets.Z[j] - sources.Z[center]};
                        const double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                        if (d2 > cutoff2) continue;
                        const std::array<double, 3> u{d[0] * inverseH, d[1] * inverseH, d[2] * inverseH};
                        const double g = std::exp(-d2 * inverseH * inverseH);
                        layout.Evaluate(u, mono.data());
                        const double* coef = coefficient.data() + c * channels * terms;
                        for (std::size_t ch = 0; ch < channels; ++ch)
                        {
                            double dot = 0.0;
                            for (std::size_t t = 0; t < terms; ++t) dot += coef[ch * terms + t] * mono[t];
                            sum[ch] += g * dot;
                        }
                    }
                    for (std::size_t ch = 0; ch < channels; ++ch) out[ch * n + j] = sum[ch];
                }
            });
        }
    }

    void ParallelRange(const std::size_t count, const std::size_t grain, const std::uint32_t threads,
                       const std::function<void(std::size_t, std::size_t)>& body)
    {
        ParallelFor(count, std::max<std::size_t>(grain, 1u), ResolveThreads(threads), body);
    }

    CancellationScope::CancellationScope(const std::function<bool()>* cancelled) noexcept : m_Previous(tCancelled)
    {
        tCancelled = cancelled;
    }

    CancellationScope::~CancellationScope() { tCancelled = m_Previous; }

    std::uint32_t ResolveThreads(const std::uint32_t requested) noexcept
    {
        if (requested > 0u) return requested;
        return std::max(1u, std::thread::hardware_concurrency());
    }

    struct Evaluator::Impl
    {
        std::vector<double> TargetX{}, TargetY{}, TargetZ{};
        KdTree TargetTree{}, SourceTree{};
        std::vector<double> LogDenominator{}, Radius2{}, RowBound{};
        std::vector<std::uint64_t> RowEvaluations{};
        // Blocked single pass: per row block, partial P1 and PX over all sources (4 x M each).
        std::vector<double> Partial{};
        bool SourceTreeCurrent{false}; // SourceTree already indexes this call's moved points
        // Source log-weights of this call shifted to max 0 (zeros when unweighted), the shift and
        // the sum of the shifted weights.
        std::vector<double> LogWeight{}, SlotWeight{};
        double LogWeightShift{0.0}, WeightSum{0.0};
        // Fast Gauss transform state: farthest-point order of the fixed target, scratch.
        static constexpr std::size_t kMaxClusters = 256u;
        Clustering TargetCurve{}, TargetClusters{}, SourceCurve{}, SourceClusters{};
        MonomialLayout SourceLayout{}, TargetLayout{};
        std::vector<double> Weights{}, Transformed{};
        std::uint64_t ExtraEvaluations{0u};

        // Typical scales of the previous E-step (1% quantiles), used to aim the fast Gauss
        // transform's absolute error: DenominatorScale ~ den_n / M, P1Scale ~ P1_m / sum_n 1/den_n.
        bool HasScales{false};
        double DenominatorScale{0.0}, P1Scale{0.0};
        std::uint64_t FixedEntries{0u};

        // Nystroem state: the fixed target's farthest-point order (cached), this call's source
        // landmarks, and the largest sigma^2 whose approximation was rejected (smaller kernels
        // are not retried, since the approximation only degrades as the kernel narrows).
        std::vector<std::uint32_t> TargetLandmarks{}; // progressive order of the fixed target (cached)
        // METHOD-076 variants: Fixed keeps the first Nystrom iteration's source (for its point order)
        // or its boxes; NystromDraw counts Evaluate calls that tried Nystrom (Resampled seeds).
        std::vector<double> FixedSourceX{}, FixedSourceY{}, FixedSourceZ{};
        std::vector<std::uint32_t> SourceLandmarks{};
        bool SourceLandmarksExhausted{false};
        std::array<std::array<Eigen::Vector3d, 2>, 2> Boxes{};
        bool BoxesFrozen{false};
        std::uint64_t NystromDraw{0u};
        std::uint32_t NystromVariant{0u}; // source | schedule << 8 | pattern << 16 of the cached state
        std::size_t NystromSourceSize{0u}; // source size of the cached state

        void ResetLandmarkVariant()
        {
            FixedSourceX.clear(); FixedSourceY.clear(); FixedSourceZ.clear();
            SourceLandmarks = {};
            SourceLandmarksExhausted = false;
            BoxesFrozen = false;
            NystromDraw = 0u;
        }
        std::vector<double> LandmarkX{}, LandmarkY{}, LandmarkZ{}, LandmarkZero{};
        double NystromRejectedSigma2{0.0};
        bool TargetLandmarksExhausted{false}; // the target order ended (duplicates) before the request
        std::size_t NystromLandmarks{0u}; // count that last passed (the next call starts there)

        std::uint64_t TargetGeneration{0u}; // process-unique per SetTarget, for external evaluators

        // Per-row first-order estimate of the relative error an external evaluator with fp32 kernel
        // terms adds to a row denominator (METHOD-056 review, METHOD-063). Coordinates travel as
        // float-float pairs and differences are formed as (x_hi - y_hi) + (x_lo - y_lo), so the
        // coordinate rounding term vanishes; what remains is about 5 ulp of the exponent
        // magnitude a = |w| + |x - y|^2 / 2 sigma^2 of the row's relevant terms, which lie within
        // kRelevantExponent of its largest one (bounded by the nearest source). Rows whose whole
        // kernel mass is below e^-25 of the uniform term get 0 (their responsibilities are that
        // small). Not a rigorous bound: errors of many terms partly cancel (C117).
        void ExternalRowEstimates(const PointSet& moved, const double sigma2, const double logC,
                                  const std::uint32_t threads, std::vector<double>& estimate)
        {
            constexpr double kRelevantExponent = 20.0, kUlp = 0x1p-24;
            const std::size_t n = TargetX.size(), m = moved.Size();
            if (!SourceTreeCurrent)
            {
                SourceTree.Build(moved);
                SourceTreeCurrent = true;
            }
            double weightSpread = 0.0;
            for (const double w : LogWeight) weightSpread = std::max(weightSpread, -w);
            const double inverse = 1.0 / (2.0 * sigma2);
            const double logMass = std::log(double(m)), logUniform = logC - LogWeightShift;
            estimate.assign(n, 0.0);
            ParallelFor(n, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                for (std::size_t j = begin; j < end; ++j)
                {
                    const double exponent = SourceTree.NearestSquared(TargetX[j], TargetY[j], TargetZ[j]) * inverse;
                    if (logMass - exponent < logUniform - 25.0) continue; // shifted weights are <= 0
                    estimate[j] = 5.0 * kUlp * (exponent + weightSpread + kRelevantExponent);
                }
            });
        }

        // External (Vulkan) rows; false when the evaluator is absent, fails or returns non-finite
        // or negative statistics (the caller then runs the CPU choice). Rows flagged in `cpuRows`
        // are skipped by the device and evaluated here exactly (denominator, Pt1 and their
        // P1/PX terms, added per source in row order); the others report their estimate as bound.
        bool ExternalRows(const Settings& settings, const PointSet& moved, const double sigma2, const double logC,
                          const std::vector<double>& estimate, const std::vector<std::uint32_t>& cpuRows,
                          const std::uint32_t threads, Sums& out)
        {
            const std::size_t n = TargetX.size(), m = moved.Size();
            if (!settings.External) return false;
            const bool anyCpuRow = std::ranges::any_of(cpuRows, [](const std::uint32_t flag) { return flag != 0u; });
            const ExternalRequest request{.Target = Target(), .Moved = moved, .TargetGeneration = TargetGeneration,
                .Sigma2 = sigma2, .LogOutlier = logC - LogWeightShift, .LogWeights = LogWeight,
                .SkipRows = anyCpuRow ? std::span<const std::uint32_t>(cpuRows) : std::span<const std::uint32_t>{},
                .LogDenominator = LogDenominator, .Pt1 = out.Pt1, .P1 = out.P1,
                .PXx = out.PXx, .PXy = out.PXy, .PXz = out.PXz};
            if (!settings.External(request)) return false;
            for (std::size_t j = 0; j < n; ++j)
            {
                if (anyCpuRow && cpuRows[j] != 0u) continue;
                if (!std::isfinite(LogDenominator[j]) || !(out.Pt1[j] >= 0.0) || !std::isfinite(out.Pt1[j])) return false;
                LogDenominator[j] += LogWeightShift;
                RowBound[j] = estimate[j];
                RowEvaluations[j] = m;
            }
            for (std::size_t i = 0; i < m; ++i)
                if (!(out.P1[i] >= 0.0) || !std::isfinite(out.P1[i]) || !std::isfinite(out.PXx[i]) ||
                    !std::isfinite(out.PXy[i]) || !std::isfinite(out.PXz[i]))
                    return false;
            if (!anyCpuRow) return true;

            // The skipped rows exactly, as the dense path computes them.
            std::vector<std::uint32_t> rows;
            for (std::uint32_t j = 0; j < n; ++j)
                if (cpuRows[j] != 0u) rows.push_back(j);
            AddExactRows(moved, sigma2, logC, rows, threads, out);
            out.ExternalCpuRows = std::uint32_t(rows.size());
            return true;
        }

        // The approximate pass omits these rows from both target and source statistics.
        void AddExactRows(const PointSet& moved, const double sigma2, const double logC,
                          const std::vector<std::uint32_t>& rows, const std::uint32_t threads, Sums& out)
        {
            if (!SourceTreeCurrent)
            {
                SourceTree.Build(moved);
                SourceTreeCurrent = true;
            }
            const std::size_t m = moved.Size();
            const double twoSigma2 = 2.0 * sigma2;
            ParallelFor(rows.size(), 1u, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> scratch;
                for (std::size_t k = begin; k < end; ++k)
                {
                    ExactRow(moved, rows[k], twoSigma2, logC, scratch, out);
                    RowBound[rows[k]] = 0.0;
                    RowEvaluations[rows[k]] = m; // doubled with the two-pass count below
                }
            });
            std::vector<double> x(rows.size()), y(rows.size()), z(rows.size()), logDen(rows.size());
            for (std::size_t k = 0; k < rows.size(); ++k)
            {
                x[k] = TargetX[rows[k]]; y[k] = TargetY[rows[k]]; z[k] = TargetZ[rows[k]];
                logDen[k] = LogDenominator[rows[k]];
            }
            const double inverseTwoSigma2 = 1.0 / twoSigma2;
            ParallelFor(m, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> p(rows.size());
                for (std::size_t i = begin; i < end; ++i)
                {
                    SourceRowTerms(x.data(), y.data(), z.data(), logDen.data(), rows.size(), moved.X[i], moved.Y[i],
                                   moved.Z[i], inverseTwoSigma2, LogWeight[i] + LogWeightShift, p.data());
                    for (std::size_t k = 0; k < rows.size(); ++k)
                    {
                        out.P1[i] += p[k];
                        out.PXx[i] += p[k] * x[k];
                        out.PXy[i] += p[k] * y[k];
                        out.PXz[i] += p[k] * z[k];
                    }
                }
            });
        }

        void UpdateScales(const std::vector<double>& p1)
        {
            const std::size_t n = LogDenominator.size(), m = p1.size();
            HasScales = false;
            if (n == 0 || m == 0) return;
            std::vector<double> scale(n);
            double weightSum = 0.0;
            // Denominators relative to the total source weight; P1 without its own source weight.
            const double logTotal = LogWeightShift + std::log(WeightSum);
            for (std::size_t j = 0; j < n; ++j)
            {
                scale[j] = std::exp(LogDenominator[j] - logTotal);
                weightSum += std::exp(-LogDenominator[j]);
            }
            const auto quantile = [](std::vector<double>& values)
            {
                const std::size_t k = values.size() / 100u;
                std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(k), values.end());
                return values[k];
            };
            DenominatorScale = quantile(scale);
            if (!(weightSum > 0.0) || !std::isfinite(weightSum)) return;
            std::vector<double> share(m);
            for (std::size_t i = 0; i < m; ++i) share[i] = p1[i] * std::exp(-(LogWeight[i] + LogWeightShift)) / weightSum;
            P1Scale = quantile(share);
            HasScales = std::isfinite(DenominatorScale) && DenominatorScale > 0.0 && std::isfinite(P1Scale) && P1Scale > 0.0;
        }

        bool Weighted{false};

        // Dense row terms relative to the row's largest term. Unweighted rows use the nearest
        // source (term exactly 1); weighted rows shift by the true maximum weighted exponent, so a
        // heavily down-weighted nearest source cannot underflow the row. Returns the sum; `shift`
        // receives the extra exponent (<= 0) beyond -d_min^2 / 2 sigma^2 + LogWeightShift.
        double DenseRow(const PointSet& moved, const double qx, const double qy, const double qz, const double minDistance,
                        const double inverseTwoSigma2, double* out, double& shift) const
        {
            const std::size_t m = moved.Size();
            if (!Weighted)
            {
                shift = 0.0;
                return DenseRowTerms(moved.X.data(), moved.Y.data(), moved.Z.data(), LogWeight.data(), m, qx, qy, qz,
                                     minDistance, inverseTwoSigma2, out);
            }
            DenseRowExponents(moved.X.data(), moved.Y.data(), moved.Z.data(), LogWeight.data(), m, qx, qy, qz, minDistance,
                              inverseTwoSigma2, out);
            return ExponentiateRelativeToMax(out, m, shift);
        }

        // Exact dense row j: log denominator and Pt1 (as the dense path computes them).
        void ExactRow(const PointSet& moved, const std::size_t j, const double twoSigma2, const double logC,
                      std::vector<double>& scratch, Sums& out)
        {
            const PointSet target = Target();
            const std::size_t m = moved.Size();
            scratch.resize(m);
            const double minDistance = SourceTree.NearestSquared(target.X[j], target.Y[j], target.Z[j]);
            double shift = 0.0;
            const double sum = DenseRow(moved, target.X[j], target.Y[j], target.Z[j], minDistance, 1.0 / twoSigma2,
                                        scratch.data(), shift);
            const double aMax = -minDistance / twoSigma2 + LogWeightShift + shift;
            LogDenominator[j] = LogSumWithOutlier(aMax, sum, logC);
            out.Pt1[j] = sum * std::exp(aMax - LogDenominator[j]);
        }

        // Plans both transforms from the previous E-step's scales (or a sampled estimate): the
        // absolute errors aim at tol times a typical denominator and P1 entry; entries whose
        // a-posteriori bound still exceeds tol are recomputed exactly. False when no plan exists
        // or (for Auto) when the plan is not clearly cheaper than the dense evaluation.
        bool PlanFastGauss(const PointSet& moved, const double sigma2, const double tolerance, const bool requireSavings,
                           const std::uint32_t threads, IfgtPlan& sourcePlan, IfgtPlan& targetPlan)
        {
            const std::size_t n = TargetX.size(), m = moved.Size();
            const double h = std::sqrt(2.0 * sigma2);
            double denominatorScale = DenominatorScale, p1Scale = P1Scale;
            if (!HasScales)
            {
                // Exact denominators of 16 spread rows (outliers excluded: they only lower it).
                std::vector<double> scratch;
                double smallest = std::numeric_limits<double>::infinity();
                for (std::size_t k = 0; k < std::min<std::size_t>(16u, n); ++k)
                {
                    const std::size_t j = k * n / std::min<std::size_t>(16u, n);
                    scratch.resize(m);
                    double total = 0.0;
                    for (std::size_t i = 0; i < m; ++i)
                        total += std::exp(-Squared(TargetX[j], TargetY[j], TargetZ[j], moved.X[i], moved.Y[i], moved.Z[i]) /
                                              (h * h) + LogWeight[i]);
                    smallest = std::min(smallest, total / WeightSum);
                }
                denominatorScale = p1Scale = smallest;
            }
            if (!(denominatorScale > 1e-280) || !(p1Scale > 1e-280)) return false;
            if (TargetCurve.Centers.empty()) FarthestPointClusters(Target(), kMaxClusters, threads, TargetCurve);
            FarthestPointClusters(moved, kMaxClusters, threads, SourceCurve);
            sourcePlan = PlanIfgt(SourceCurve.Radius, h, tolerance * denominatorScale, n, m, 1u);
            targetPlan = PlanIfgt(TargetCurve.Radius, h, tolerance * p1Scale, m, n, 4u);
            if (!std::isfinite(sourcePlan.Cost) || !std::isfinite(targetPlan.Cost)) return false;
            // Cost units: one expansion term and channel per point ~ one unit; one dense pair ~ kDensePair.
            constexpr double kDensePair = 12.0;
            // About 1% of rows and sources fall below the planned scale and are recomputed exactly.
            const double fixups = 0.01 * 2.0 * kDensePair * double(n) * double(m);
            return !requireSavings || sourcePlan.Cost + targetPlan.Cost + fixups < 0.5 * kDensePair * double(n) * double(m);
        }

        void FastGaussRows(const PointSet& moved, const double sigma2, const double logC, const double tolerance,
                           const IfgtPlan& sourcePlan, const IfgtPlan& targetPlan, Sums& out, const std::uint32_t threads)
        {
            const PointSet target = Target();
            const std::size_t n = target.Size(), m = moved.Size();
            const double h = std::sqrt(2.0 * sigma2), twoSigma2 = 2.0 * sigma2;
            if (!SourceTreeCurrent) SourceTree.Build(moved);
            SourceTreeCurrent = false;
            FarthestPointClusters(moved, sourcePlan.Clusters, threads, SourceClusters);
            Weights.resize(m);
            for (std::size_t i = 0; i < m; ++i) Weights[i] = std::exp(LogWeight[i]);
            Transformed.assign(n, 0.0);
            ImprovedFastGaussTransform(moved, SourceClusters, Weights, 1u, target, h, sourcePlan, SourceLayout, Transformed,
                                       threads);
            // Pass 1: |S_n - S~_n| <= Q e1 for the shifted source weights (sum Q); the denominator is
            // e^shift S_n + c, so its relative error is Q e1 e^shift / den_n.
            std::vector<std::uint32_t> exactRows;
            for (std::size_t j = 0; j < n; ++j)
            {
                const double sum = std::max(Transformed[j], 0.0);
                const double logDenominator = LogSumWithOutlier(LogWeightShift, sum, logC);
                // Measured against the approximate denominator: relative error <= b / (1 - b).
                const double measured = WeightSum * sourcePlan.UnitError * std::exp(LogWeightShift - logDenominator);
                const double bound = measured < 1.0 ? measured / (1.0 - measured) : std::numeric_limits<double>::infinity();
                if (!(bound <= tolerance) || !std::isfinite(logDenominator))
                {
                    exactRows.push_back(std::uint32_t(j));
                    RowBound[j] = 0.0;
                    continue;
                }
                LogDenominator[j] = logDenominator;
                out.Pt1[j] = sum * std::exp(LogWeightShift - logDenominator);
                RowBound[j] = bound;
            }
            ParallelFor(exactRows.size(), 8, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> scratch;
                for (std::size_t k = begin; k < end; ++k) ExactRow(moved, exactRows[k], twoSigma2, logC, scratch, out);
            });
            Weights.assign(4u * n, 0.0);
            double weightSum = 0.0;
            for (std::size_t j = 0; j < n; ++j)
            {
                const double w = std::exp(-LogDenominator[j]);
                weightSum += w;
                Weights[j] = w;
                Weights[n + j] = w * target.X[j];
                Weights[2u * n + j] = w * target.Y[j];
                Weights[3u * n + j] = w * target.Z[j];
            }
            FarthestPointClusters(target, targetPlan.Clusters, threads, TargetClusters);
            Transformed.assign(4u * m, 0.0);
            ImprovedFastGaussTransform(target, TargetClusters, Weights, 4u, moved, h, targetPlan, TargetLayout, Transformed,
                                       threads);
            // Pass 2: |P1_m - P1~_m| <= e2 sum_n w_n, i.e. relative e2 W / P1_m.
            std::vector<std::uint32_t> exactSources;
            double sourceBound = 0.0;
            for (std::size_t i = 0; i < m; ++i)
            {
                const double p1 = Transformed[i];
                const double measured = targetPlan.UnitError * weightSum / p1;
                const double bound = measured < 1.0 ? measured / (1.0 - measured) : std::numeric_limits<double>::infinity();
                if (!(p1 > 0.0) || !(bound <= tolerance))
                {
                    exactSources.push_back(std::uint32_t(i));
                    continue;
                }
                sourceBound = std::max(sourceBound, bound);
                const double weight = std::exp(LogWeight[i] + LogWeightShift);
                out.P1[i] = weight * p1;
                out.PXx[i] = weight * Transformed[m + i];
                out.PXy[i] = weight * Transformed[2u * m + i];
                out.PXz[i] = weight * Transformed[3u * m + i];
            }
            const double inverseTwoSigma2 = 1.0 / twoSigma2;
            ParallelFor(exactSources.size(), 8, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> p(n);
                for (std::size_t k = begin; k < end; ++k)
                {
                    const std::size_t i = exactSources[k];
                    SourceRowTerms(target.X.data(), target.Y.data(), target.Z.data(), LogDenominator.data(), n, moved.X[i],
                                   moved.Y[i], moved.Z[i], inverseTwoSigma2, LogWeight[i] + LogWeightShift, p.data());
                    double p1 = 0.0, px = 0.0, py = 0.0, pz = 0.0;
                    for (std::size_t j = 0; j < n; ++j)
                    {
                        p1 += p[j]; px += p[j] * target.X[j]; py += p[j] * target.Y[j]; pz += p[j] * target.Z[j];
                    }
                    out.P1[i] = p1; out.PXx[i] = px; out.PXy[i] = py; out.PXz[i] = pz;
                }
            });
            // P1 inherits the denominators' relative error through the weights.
            for (std::size_t j = 0; j < n; ++j) RowBound[j] += sourceBound;
            FixedEntries = exactRows.size() + exactSources.size();
            ExtraEvaluations = std::uint64_t(m + n * sourcePlan.Clusters + n + m * targetPlan.Clusters) +
                               std::uint64_t(exactRows.size()) * m + std::uint64_t(exactSources.size()) * n;
        }

        // Fixed row blocks for the single-pass E-step; 0 selects the two-pass fallback because
        // the partial sums would exceed the memory budget. Depends only on N and M.
        [[nodiscard]] static std::size_t RowBlocks(const std::size_t n, const std::size_t m, const std::size_t budget,
                                                   const bool truncated) noexcept
        {
            // A dense row scatters into all 4 M partial values of its block; beyond cache-sized
            // partials that traffic dominates and the two-pass form (shared, read-only arrays) wins.
            constexpr std::size_t kDenseBlockBytes = std::size_t{512} << 10;
            if (!truncated && 4u * sizeof(double) * m > kDenseBlockBytes) return 0u;
            const std::size_t wanted = std::min<std::size_t>(32u, (n + 127u) / 128u);
            const std::size_t affordable = budget / (4u * sizeof(double) * m);
            return affordable >= wanted ? std::max<std::size_t>(wanted, 1u) : 0u;
        }

        // One pass over the rows: each row's kernel terms are evaluated once (shifted by its
        // largest term, as in the reference), normalized by the row denominator and scattered
        // into its block's partial sums; blocks are then reduced in block order.
        void BlockedRows(const PointSet& moved, const double twoSigma2, const double logC, const bool truncated,
                         const double tolerance, const std::size_t blocks, Sums& out, const std::uint32_t threads)
        {
            const PointSet target = Target();
            const std::size_t n = target.Size(), m = moved.Size();
            if (!SourceTreeCurrent) SourceTree.Build(moved);
            SourceTreeCurrent = false;
            const double tau = std::log(double(m) / tolerance);
            const double inverseTwoSigma2 = 1.0 / twoSigma2;
            Partial.assign(blocks * 4u * m, 0.0);
            // Truncated rows run in target-tree order and accumulate by source-tree slot, so a
            // block's neighborhoods are spatially coherent and contiguous in memory.
            if (truncated)
            {
                SlotWeight.resize(m);
                for (std::size_t slot = 0; slot < m; ++slot) SlotWeight[slot] = LogWeight[SourceTree.Original(slot)];
            }
            ParallelFor(blocks, 1, threads, [&](const std::size_t firstBlock, const std::size_t lastBlock)
            {
                std::vector<double> value, weight;
                std::vector<std::uint32_t> index;
                value.reserve(truncated ? 256u : m);
                weight.reserve(truncated ? 256u : 0u);
                index.reserve(truncated ? 256u : 0u);
                for (std::size_t b = firstBlock; b < lastBlock; ++b)
                {
                    double* p1 = Partial.data() + b * 4u * m;
                    double* px = p1 + m;
                    double* py = px + m;
                    double* pz = py + m;
                    for (std::size_t row = b * n / blocks; row < (b + 1) * n / blocks; ++row)
                    {
                        const std::size_t j = truncated ? TargetTree.Original(row) : row;
                        const double x = target.X[j], y = target.Y[j], z = target.Z[j];
                        // Exact nearest squared distance (same arithmetic as the row terms), so the
                        // largest term is exp(0) = 1.
                        const auto [minDistance, nearestSlot] = SourceTree.Nearest(x, y, z);
                        // Shifted weight of the nearest source: its term is the kept lower bound.
                        const double nearestWeight = LogWeight[SourceTree.Original(nearestSlot)];
                        // Truncated rows index sources by tree slot (partials in slot order).
                        value.clear();
                        weight.clear();
                        index.clear();
                        double sum = 0.0, shift = 0.0;
                        if (truncated)
                        {
                            SourceTree.ForEachWithin(x, y, z, minDistance + twoSigma2 * (tau - nearestWeight),
                                                     [&](const std::size_t slot, const double d2)
                            {
                                index.push_back(std::uint32_t(slot));
                                weight.push_back(SlotWeight[slot]);
                                value.push_back(d2);
                            });
                            if (!Weighted)
                                sum = ExponentiateShifted(value.data(), weight.data(), value.size(), minDistance, inverseTwoSigma2);
                            else
                            {
                                for (std::size_t k = 0; k < value.size(); ++k)
                                    value[k] = (minDistance - value[k]) * inverseTwoSigma2 + weight[k];
                                sum = ExponentiateRelativeToMax(value.data(), value.size(), shift);
                            }
                        }
                        else
                        {
                            value.resize(m);
                            sum = DenseRow(moved, x, y, z, minDistance, inverseTwoSigma2, value.data(), shift);
                        }
                        const double aMax = -minDistance / twoSigma2 + LogWeightShift + shift;
                        const double logDenominator = LogSumWithOutlier(aMax, sum, logC);
                        const double inverse = std::exp(aMax - logDenominator);
                        LogDenominator[j] = logDenominator;
                        out.Pt1[j] = sum * inverse;
                        RowEvaluations[j] = value.size();
                        if (truncated)
                        {
                            // Each dropped term is at most tol/M times the nearest term, so the dropped
                            // mass is at most (M - kept)/M tol e^{w_nearest} in the row's shifted units;
                            // the denominator is (sum + c e^{-a_max}) in those units.
                            const double outlierShifted = std::exp(std::min(logC - aMax, 700.0));
                            RowBound[j] = (double(m - value.size()) / double(m)) * tolerance * std::exp(nearestWeight - shift) /
                                          (sum + outlierShifted);
                        }
                        if (!truncated)
                            ScatterRow(value.data(), m, inverse, x, y, z, p1, px, py, pz);
                        else
                            for (std::size_t k = 0; k < value.size(); ++k)
                            {
                                const std::size_t i = index[k];
                                const double p = value[k] * inverse;
                                p1[i] += p;
                                px[i] += p * x;
                                py[i] += p * y;
                                pz[i] += p * z;
                            }
                    }
                }
            });
            ParallelFor(m, 1024, threads, [&](const std::size_t begin, const std::size_t end)
            {
                for (std::size_t k = begin; k < end; ++k)
                {
                    double p1 = 0.0, px = 0.0, py = 0.0, pz = 0.0;
                    for (std::size_t b = 0; b < blocks; ++b)
                    {
                        const double* block = Partial.data() + b * 4u * m;
                        p1 += block[k]; px += block[m + k]; py += block[2u * m + k]; pz += block[3u * m + k];
                    }
                    const std::size_t i = truncated ? SourceTree.Original(k) : k;
                    out.P1[i] = p1; out.PXx[i] = px; out.PXy[i] = py; out.PXz[i] = pz;
                }
            });
        }

        [[nodiscard]] PointSet Target() const noexcept { return {TargetX, TargetY, TargetZ}; }

        void Resize(const std::size_t n, const std::size_t m, Sums& out)
        {
            LogDenominator.assign(n, 0.0);
            Radius2.assign(n, 0.0);
            RowBound.assign(n, 0.0);
            RowEvaluations.assign(n, 0u);
            out.P1.assign(m, 0.0); out.PXx.assign(m, 0.0); out.PXy.assign(m, 0.0); out.PXz.assign(m, 0.0);
            out.Pt1.assign(n, 0.0);
        }

        void DenseRows(const PointSet& moved, const double twoSigma2, const double logC, Sums& out, const std::uint32_t threads)
        {
            const PointSet target = Target();
            const std::size_t m = moved.Size();
            if (!SourceTreeCurrent) SourceTree.Build(moved);
            SourceTreeCurrent = false;
            ParallelFor(target.Size(), kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> e(m);
                for (std::size_t j = begin; j < end; ++j)
                {
                    const double x = target.X[j], y = target.Y[j], z = target.Z[j];
                    const double minDistance = SourceTree.NearestSquared(x, y, z);
                    double shift = 0.0;
                    const double sum = DenseRow(moved, x, y, z, minDistance, 1.0 / twoSigma2, e.data(), shift);
                    const double aMax = -minDistance / twoSigma2 + LogWeightShift + shift;
                    LogDenominator[j] = LogSumWithOutlier(aMax, sum, logC);
                    out.Pt1[j] = sum * std::exp(aMax - LogDenominator[j]);
                    RowEvaluations[j] = m;
                }
            });
            const double inverseTwoSigma2 = 1.0 / twoSigma2;
            const std::size_t n = target.Size();
            ParallelFor(m, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> p(n);
                for (std::size_t i = begin; i < end; ++i)
                {
                    const double qx = moved.X[i], qy = moved.Y[i], qz = moved.Z[i];
                    SourceRowTerms(target.X.data(), target.Y.data(), target.Z.data(), LogDenominator.data(), n, qx, qy, qz,
                                   inverseTwoSigma2, LogWeight[i] + LogWeightShift, p.data());
                    double p1 = 0.0, px = 0.0, py = 0.0, pz = 0.0;
                    for (std::size_t j = 0; j < n; ++j)
                    {
                        p1 += p[j];
                        px += p[j] * target.X[j];
                        py += p[j] * target.Y[j];
                        pz += p[j] * target.Z[j];
                    }
                    out.P1[i] = p1; out.PXx[i] = px; out.PXy[i] = py; out.PXz[i] = pz;
                }
            });
        }

        void TruncatedRows(const PointSet& moved, const double twoSigma2, const double logC, const double tolerance,
                           Sums& out, const std::uint32_t threads)
        {
            const std::size_t n = TargetTree.Size(), m = moved.Size();
            if (!SourceTreeCurrent) SourceTree.Build(moved);
            SourceTreeCurrent = false;
            // r_n^2 = d_min^2 + 2 sigma^2 ln(M / tol): every dropped term is at most tol/M times the
            // nearest (largest) term, so the dropped mass is at most tol times the kept mass.
            const double tau = std::log(double(m) / tolerance);
            const double extra = twoSigma2 * tau;
            std::vector<double> radius2Tree(n);
            ParallelFor(n, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                for (std::size_t slot = begin; slot < end; ++slot)
                {
                    const std::size_t j = TargetTree.Original(slot);
                    const double x = TargetTree.X(slot), y = TargetTree.Y(slot), z = TargetTree.Z(slot);
                    const auto [minDistance, nearestSlot] = SourceTree.Nearest(x, y, z);
                    const double nearestWeight = LogWeight[SourceTree.Original(nearestSlot)];
                    const double radius2 = minDistance + extra - twoSigma2 * nearestWeight;
                    // The largest kept weighted exponent (0 when unweighted) keeps the row from underflowing.
                    double shift = -std::numeric_limits<double>::infinity();
                    SourceTree.ForEachWithin(x, y, z, radius2, [&](const std::size_t sourceSlot, const double d2)
                    {
                        shift = std::max(shift, (minDistance - d2) / twoSigma2 + LogWeight[SourceTree.Original(sourceSlot)]);
                    });
                    double sum = 0.0;
                    std::uint64_t kept = 0;
                    SourceTree.ForEachWithin(x, y, z, radius2, [&](const std::size_t sourceSlot, const double d2)
                    {
                        sum += std::exp((minDistance - d2) / twoSigma2 + LogWeight[SourceTree.Original(sourceSlot)] - shift);
                        ++kept;
                    });
                    const double aMax = -minDistance / twoSigma2 + LogWeightShift + shift;
                    LogDenominator[j] = LogSumWithOutlier(aMax, sum, logC);
                    out.Pt1[j] = sum * std::exp(aMax - LogDenominator[j]);
                    radius2Tree[slot] = radius2;
                    RowEvaluations[j] = kept;
                    // Dropped mass <= (M - kept) exp(-radius2 / 2 sigma^2) = (M - kept)/M tol e^{a_max};
                    // the denominator is at least e^{a_max} (sum + c e^{-a_max}).
                    const double outlierShifted = std::exp(std::min(logC - aMax, 700.0));
                    RowBound[j] = (double(m - kept) / double(m)) * tolerance * std::exp(nearestWeight - shift) / (sum + outlierShifted);
                }
            });
            TargetTree.SetRadii(radius2Tree);
            ParallelFor(m, kRowGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    double p1 = 0.0, px = 0.0, py = 0.0, pz = 0.0;
                    TargetTree.ForEachReaching(moved.X[i], moved.Y[i], moved.Z[i], [&](const std::size_t slot, const double d2)
                    {
                        const double p = std::exp(-d2 / twoSigma2 + LogWeight[i] + LogWeightShift -
                                                  LogDenominator[TargetTree.Original(slot)]);
                        p1 += p;
                        px += p * TargetTree.X(slot);
                        py += p * TargetTree.Y(slot);
                        pz += p * TargetTree.Z(slot);
                    });
                    out.P1[i] = p1; out.PXx[i] = px; out.PXy[i] = py; out.PXz[i] = pz;
                }
            });
        }

        // Nystroem E-step (METHOD-053): K_mn ~= k(y_m, Z) W^+ k(Z, x_n) with W = K(Z, Z) on
        // landmarks Z (farthest points, half from each set), evaluated in landmark space in three
        // passes (sources, targets, sources), each reduced over fixed chunks in chunk order so
        // the result does not depend on the thread count. Returns false when a denominator or
        // P1 entry is not positive, or when the sampled relative error (exact rows: 32 target
        // denominators and 32 source P1 entries) exceeds the limit; `out` is then unspecified.
        // Extends a cached order by a longer one, keeping the cached identities as the prefix (for
        // progressive orders the result equals the longer order; sample elimination picks another set
        // for a larger count).
        static void ExtendOrder(std::vector<std::uint32_t>& cached, const std::vector<std::uint32_t>& longer)
        {
            for (const std::uint32_t i : longer)
                if (std::find(cached.begin(), cached.end(), i) == cached.end()) cached.push_back(i);
        }

        // The landmarks of one Nystrom attempt (NystromLandmarkSchedule / Source, METHOD-076).
        void BuildLandmarks(const PointSet target, const PointSet moved, const std::size_t landmarks, const double sigma2,
                            const Settings& settings)
        {
            const std::size_t n = target.Size(), m = moved.Size();
            const auto schedule = settings.NystromSchedule;
            // Another source starts the variant's draws afresh (fixed orders and boxes belong to it).
            if (m != NystromSourceSize)
            {
                ResetLandmarkVariant();
                NystromSourceSize = m;
            }
            LandmarkX.clear(); LandmarkY.clear(); LandmarkZ.clear();
            if (settings.NystromSource == NystromLandmarkSource::BoundingBox)
            {
                if (!BoxesFrozen)
                {
                    const double pad = std::sqrt(sigma2);
                    Boxes = {PaddedBox(target, pad), PaddedBox(moved, pad)};
                    BoxesFrozen = schedule == NystromLandmarkSchedule::Fixed;
                }
                const std::uint64_t seed =
                    settings.NystromSampling.Seed + (schedule == NystromLandmarkSchedule::Resampled ? NystromDraw : 0u);
                UnionBoxPoints(Boxes[0], Boxes[1], landmarks, settings.NystromPattern, seed, LandmarkX, LandmarkY, LandmarkZ);
                return;
            }
            const std::size_t half = std::max<std::size_t>(1u, landmarks / 2u);
            const auto append = [&](const PointSet set, const std::span<const std::uint32_t> order) {
                for (std::size_t k = 0; k < std::min(half, order.size()); ++k)
                {
                    const std::size_t i = order[k];
                    LandmarkX.push_back(set.X[i]); LandmarkY.push_back(set.Y[i]); LandmarkZ.push_back(set.Z[i]);
                }
            };
            if (schedule == NystromLandmarkSchedule::Resampled)
            {
                // Every seed of the sampling methods moves with the draw, and so does the start point.
                const auto draw = [&](const PointSet set) {
                    PointSampling::Params params = settings.NystromSampling;
                    const auto shift = std::uint32_t(NystromDraw);
                    params.Seed += NystromDraw;
                    params.FirstIndex = std::uint32_t((std::uint64_t(params.FirstIndex) + NystromDraw) % set.Size());
                    params.Poisson.GridOriginSeed += shift;
                    params.Poisson.ShuffleSeed += shift;
                    params.Poisson.PhaseOrderSeed += shift;
                    params.GreedySeed += shift;
                    return SamplePoints(set, half, params);
                };
                append(target, draw(target));
                append(moved, draw(moved));
                return;
            }
            // Mixed and Fixed: progressive orders, a longer request extends the cached target prefix.
            if (TargetLandmarks.size() < std::min(half, n) && !TargetLandmarksExhausted)
            {
                ExtendOrder(TargetLandmarks, SamplePoints(target, half, settings.NystromSampling));
                TargetLandmarksExhausted = TargetLandmarks.size() < std::min(half, n);
            }
            append(target, TargetLandmarks);
            if (schedule == NystromLandmarkSchedule::Mixed)
            {
                append(moved, SamplePoints(moved, half, settings.NystromSampling));
                return;
            }
            // Fixed: identities from the source of the first Nystrom iteration, positions current.
            if (FixedSourceX.empty())
            {
                FixedSourceX.assign(moved.X.begin(), moved.X.end());
                FixedSourceY.assign(moved.Y.begin(), moved.Y.end());
                FixedSourceZ.assign(moved.Z.begin(), moved.Z.end());
                SourceLandmarks = {};
                SourceLandmarksExhausted = false;
            }
            if (SourceLandmarks.size() < std::min(half, m) && !SourceLandmarksExhausted)
            {
                ExtendOrder(SourceLandmarks, SamplePoints({FixedSourceX, FixedSourceY, FixedSourceZ}, half, settings.NystromSampling));
                SourceLandmarksExhausted = SourceLandmarks.size() < std::min(half, m);
            }
            append(moved, SourceLandmarks);
        }

        bool NystromRows(const PointSet& moved, const double sigma2, const double logC, const std::size_t landmarks,
                         const double errorLimit, const Settings& settings, Sums& out,
                         const std::uint32_t threads, double& sampledError, std::uint64_t& evaluations, double& spent)
        {
            const PointSet target = Target();
            const std::size_t n = target.Size(), m = moved.Size();
            BuildLandmarks(target, moved, landmarks, sigma2, settings);
            const std::size_t l = LandmarkX.size();
            if (l == 0u) return false;
            LandmarkZero.assign(l, 0.0);
            const double inverseTwoSigma2 = 0.5 / sigma2;
            const auto kernelRow = [&](const double x, const double y, const double z, double* row)
            {
                (void)DenseRowTerms(LandmarkX.data(), LandmarkY.data(), LandmarkZ.data(), LandmarkZero.data(), l, x, y, z,
                                    0.0, inverseTwoSigma2, row);
            };
            Eigen::MatrixXd gram{Eigen::Index(l), Eigen::Index(l)};
            for (std::size_t a = 0; a < l; ++a)
                kernelRow(LandmarkX[a], LandmarkY[a], LandmarkZ[a], gram.col(Eigen::Index(a)).data());
            evaluations += std::uint64_t(l) * l;
            const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(gram);
            if (eigen.info() != Eigen::Success) return false;
            const double top = eigen.eigenvalues().maxCoeff();
            if (!(top > 0.0)) return false;
            // Pseudo-inverse on the numerically positive spectrum.
            constexpr double kCutoff = 1.0e-10;
            const Eigen::VectorXd inverse =
                eigen.eigenvalues().unaryExpr([&](const double v) { return v > kCutoff * top ? 1.0 / v : 0.0; });
            const Eigen::MatrixXd pseudoInverse = eigen.eigenvectors() * inverse.asDiagonal() * eigen.eigenvectors().transpose();

            constexpr std::size_t kGrain = 1024;
            // Pass 1 (sources): a = sum_m w_m k(Z, y_m).
            const std::size_t sourceChunks = (m + kGrain - 1) / kGrain, targetChunks = (n + kGrain - 1) / kGrain;
            std::vector<double> partial(std::max(sourceChunks, targetChunks) * 4u * l, 0.0);
            ParallelFor(m, kGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> row(l);
                double* sum = partial.data() + (begin / kGrain) * l;
                for (std::size_t i = begin; i < end; ++i)
                {
                    kernelRow(moved.X[i], moved.Y[i], moved.Z[i], row.data());
                    const double weight = std::exp(LogWeight[i]);
                    for (std::size_t k = 0; k < l; ++k) sum[k] += weight * row[k];
                }
            });
            Eigen::VectorXd a = Eigen::VectorXd::Zero(Eigen::Index(l));
            evaluations += std::uint64_t(m) * l;
            for (std::size_t c = 0; c < sourceChunks; ++c)
                for (std::size_t k = 0; k < l; ++k) a[Eigen::Index(k)] += partial[c * l + k];
            const Eigen::VectorXd g = pseudoInverse * a;

            // Pass 2 (targets): shifted sums S_n = k(x_n, Z) g, denominators, Pt1, and
            // h = sum_n d_n k(Z, x_n) (1, x_n) with d_n = e^{shift} / den_n.
            std::fill(partial.begin(), partial.end(), 0.0);
            std::vector<std::uint8_t> repair(n, 0u);
            ParallelFor(n, kGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> row(l);
                double* sum = partial.data() + (begin / kGrain) * 4u * l;
                for (std::size_t j = begin; j < end; ++j)
                {
                    kernelRow(target.X[j], target.Y[j], target.Z[j], row.data());
                    double shifted = 0.0;
                    for (std::size_t k = 0; k < l; ++k) shifted += row[k] * g[Eigen::Index(k)];
                    if (!(shifted > 0.0) || !std::isfinite(shifted)) { repair[j] = 1u; continue; }
                    LogDenominator[j] = LogSumWithOutlier(LogWeightShift, shifted, logC);
                    const double d = std::exp(LogWeightShift - LogDenominator[j]);
                    out.Pt1[j] = shifted * d;
                    for (std::size_t k = 0; k < l; ++k)
                    {
                        const double t = d * row[k];
                        sum[k] += t;
                        sum[l + k] += t * target.X[j];
                        sum[2u * l + k] += t * target.Y[j];
                        sum[3u * l + k] += t * target.Z[j];
                    }
                }
            });
            evaluations += std::uint64_t(n) * l;
            std::vector<std::uint32_t> exactRows;
            for (std::uint32_t j = 0; j < n; ++j)
                if (repair[j] != 0u) exactRows.push_back(j);
            const double repairCost = 2.0 * double(m) * double(exactRows.size());
            if (spent + repairCost >= 0.5 * double(n) * double(m)) return false;
            Eigen::MatrixXd h = Eigen::MatrixXd::Zero(Eigen::Index(l), 4);
            for (std::size_t c = 0; c < targetChunks; ++c)
                for (Eigen::Index column = 0; column < 4; ++column)
                    for (std::size_t k = 0; k < l; ++k)
                        h(Eigen::Index(k), column) += partial[(c * 4u + std::size_t(column)) * l + k];
            const Eigen::MatrixXd q = pseudoInverse * h;

            // Pass 3 (sources): P1 and PX.
            ParallelFor(m, kGrain, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> row(l);
                for (std::size_t i = begin; i < end; ++i)
                {
                    kernelRow(moved.X[i], moved.Y[i], moved.Z[i], row.data());
                    double v[4]{0.0, 0.0, 0.0, 0.0};
                    for (std::size_t k = 0; k < l; ++k)
                        for (Eigen::Index column = 0; column < 4; ++column) v[column] += row[k] * q(Eigen::Index(k), column);
                    const double weight = std::exp(LogWeight[i]);
                    out.P1[i] = weight * v[0]; out.PXx[i] = weight * v[1]; out.PXy[i] = weight * v[2]; out.PXz[i] = weight * v[3];
                }
            });
            evaluations += std::uint64_t(m) * l;
            if (!exactRows.empty())
            {
                spent += repairCost;
                AddExactRows(moved, sigma2, logC, exactRows, threads, out);
                evaluations += 2u * std::uint64_t(m) * exactRows.size();
                for (const auto j : exactRows) RowEvaluations[j] = 0u; // counted above, also on rejection
            }
            for (std::size_t i = 0; i < m; ++i)
                if (!(out.P1[i] >= 0.0) || !std::isfinite(out.P1[i]) || !std::isfinite(out.PXx[i]) ||
                    !std::isfinite(out.PXy[i]) || !std::isfinite(out.PXz[i])) return false;

            // A-posteriori check on exact rows spread over both sets.
            const std::size_t samples = 32u;
            const std::size_t targetSamples = std::min(samples, n), sourceSamples = std::min(samples, m);
            std::vector<double> errors(targetSamples + sourceSamples, 0.0);
            ParallelFor(targetSamples + sourceSamples, 1, threads, [&](const std::size_t begin, const std::size_t end)
            {
                std::vector<double> buffer(std::max(n, m));
                for (std::size_t r = begin; r < end; ++r)
                {
                    if (r < targetSamples)
                    {
                        const std::size_t j = r * n / targetSamples;
                        if (repair[j] != 0u) continue; // ExactRow already used a stable exponent shift.
                        const double exact = DenseRowTerms(moved.X.data(), moved.Y.data(), moved.Z.data(), LogWeight.data(),
                                                           m, target.X[j], target.Y[j], target.Z[j], 0.0, inverseTwoSigma2,
                                                           buffer.data());
                        const double logExact = LogSumWithOutlier(LogWeightShift, exact, logC);
                        errors[r] = std::abs(std::expm1(LogDenominator[j] - logExact));
                    }
                    else
                    {
                        const std::size_t i = (r - targetSamples) * m / sourceSamples;
                        SourceRowTerms(target.X.data(), target.Y.data(), target.Z.data(), LogDenominator.data(), n,
                                       moved.X[i], moved.Y[i], moved.Z[i], inverseTwoSigma2, LogWeight[i] + LogWeightShift,
                                       buffer.data());
                        double exact = 0.0;
                        for (std::size_t j = 0; j < n; ++j) exact += buffer[j];
                        errors[r] = exact > 0.0 ? std::abs(out.P1[i] - exact) / exact
                                                : (out.P1[i] > 0.0 ? std::numeric_limits<double>::infinity() : 0.0);
                    }
                }
            });
            sampledError = 0.0;
            for (const double e : errors) sampledError = std::isnan(e) ? std::numeric_limits<double>::infinity()
                                                                       : std::max(sampledError, e);
            for (std::size_t r = 0; r < targetSamples; ++r)
                if (repair[r * n / targetSamples] == 0u) evaluations += m;
            evaluations += std::uint64_t(sourceSamples) * n;
            return sampledError <= errorLimit;
        }
    };

    Evaluator::Evaluator() : m_Impl(std::make_unique<Impl>()) {}

    PointSet Evaluator::NystromLandmarks() const noexcept
    {
        return {m_Impl->LandmarkX, m_Impl->LandmarkY, m_Impl->LandmarkZ};
    }
    Evaluator::~Evaluator() = default;
    Evaluator::Evaluator(Evaluator&&) noexcept = default;
    Evaluator& Evaluator::operator=(Evaluator&&) noexcept = default;

    void Evaluator::SetTarget(const PointSet target)
    {
        Impl& s = *m_Impl;
        s.TargetX.assign(target.X.begin(), target.X.end());
        s.TargetY.assign(target.Y.begin(), target.Y.end());
        s.TargetZ.assign(target.Z.begin(), target.Z.end());
        s.TargetTree.Build(s.Target());
        // Fast-Gauss state belongs to the previous target.
        s.TargetCurve = {};
        s.TargetClusters = {};
        s.HasScales = false;
        s.TargetLandmarks = {};
        s.TargetLandmarksExhausted = false;
        s.ResetLandmarkVariant();
        s.NystromRejectedSigma2 = 0.0;
        s.NystromLandmarks = 0u;
        static std::atomic<std::uint64_t> generations{0u};
        s.TargetGeneration = ++generations;
    }

    bool Evaluator::Evaluate(const PointSet moved, const double sigma2, const double logOutlier, const Settings& settings,
                             Sums& out, const std::span<const double> sourceLogWeights)
    {
        Impl& s = *m_Impl;
        const std::size_t n = s.TargetX.size(), m = moved.Size();
        if (n == 0 || m == 0 || !(sigma2 > 0.0) || !(settings.Tolerance > 0.0 && settings.Tolerance < 1.0))
            return false;
        if (!sourceLogWeights.empty() &&
            (sourceLogWeights.size() != m || !std::ranges::all_of(sourceLogWeights, [](const double w) { return std::isfinite(w); })))
            return false;
        s.Weighted = !sourceLogWeights.empty();
        s.LogWeightShift = sourceLogWeights.empty() ? 0.0 : *std::ranges::max_element(sourceLogWeights);
        s.LogWeight.resize(m);
        s.WeightSum = 0.0;
        for (std::size_t i = 0; i < m; ++i)
        {
            s.LogWeight[i] = sourceLogWeights.empty() ? 0.0 : sourceLogWeights[i] - s.LogWeightShift;
            s.WeightSum += std::exp(s.LogWeight[i]);
        }
        const std::uint32_t threads = ResolveThreads(settings.Threads);
        const double twoSigma2 = 2.0 * sigma2;
        s.Resize(n, m, out);

        EStepPolicy used = settings.Policy == EStepPolicy::Reference ? EStepPolicy::Dense : settings.Policy;
        IfgtPlan sourcePlan{}, targetPlan{};
        s.SourceTreeCurrent = false;
        // Nystroem first; a rejected approximation is redone exactly with the Auto choice
        // between truncated and dense (the fast Gauss transform loses to dense in 3-D).
        double sampledError = 0.0;
        std::uint64_t nystromEvaluations = 0u;
        bool nystrom = false, allowFastGauss = true, wantExternal = false, external = false;
        if (used == EStepPolicy::Vulkan)
        {
            // The device runs what Auto would run densely; truncation stays on the CPU.
            used = EStepPolicy::Auto;
            allowFastGauss = false;
            wantExternal = true;
        }
        if (used == EStepPolicy::Nystrom)
        {
            if (!(settings.NystromLandmarks >= 2u && std::isfinite(settings.NystromErrorLimit) && settings.NystromErrorLimit > 0.0) ||
                !ValidNystromVariant(settings.NystromSource, settings.NystromSchedule, settings.NystromPattern,
                                     settings.NystromSampling.Method))
                return false;
            // Another variant (or another source size for Fixed) starts its draws afresh.
            const std::uint32_t variant = std::uint32_t(settings.NystromSource) | std::uint32_t(settings.NystromSchedule) << 8u |
                                          std::uint32_t(settings.NystromPattern) << 16u;
            if (variant != s.NystromVariant)
            {
                s.NystromVariant = variant;
                s.ResetLandmarkVariant();
            }
            // Landmarks start at the count that last passed (at least the setting) and double while
            // the approximation is rejected, as long as all attempts together stay below half the
            // dense cost: (2M + N) L kernel terms plus the single-threaded landmark
            // eigendecomposition (measured at about 2 L^3 kernel terms). Small inputs run exactly.
            constexpr std::size_t kLandmarkCap = 4096u;
            const std::size_t cap = std::min(kLandmarkCap, n + m);
            const double dense = double(n) * double(m);
            double spent = 0.0;
            std::size_t count = std::max<std::size_t>(settings.NystromLandmarks, s.NystromLandmarks);
            while (sigma2 > s.NystromRejectedSigma2)
            {
                const std::size_t landmarks = std::min(count, cap);
                const double l = double(landmarks);
                spent += (2.0 * double(m) + double(n)) * l + 2.0 * l * l * l;
                if (spent >= 0.5 * dense) break;
                std::uint64_t evaluations = 0u;
                nystrom = s.NystromRows(moved, sigma2, logOutlier, landmarks, settings.NystromErrorLimit,
                                        settings, out, threads, sampledError, evaluations, spent);
                nystromEvaluations += evaluations;
                if (nystrom)
                {
                    s.NystromLandmarks = landmarks;
                    break;
                }
                s.Resize(n, m, out);
                if (landmarks >= cap) break;
                count = 2u * landmarks;
            }
            ++s.NystromDraw; // one draw per iteration, shared by its doubling attempts
            // Narrower kernels need more landmarks still: do not retry below this sigma^2 (every
            // variant, also when a resampled draw might pass).
            if (!nystrom) s.NystromRejectedSigma2 = std::max(s.NystromRejectedSigma2, sigma2);
            if (!nystrom)
            {
                used = EStepPolicy::Auto;
                allowFastGauss = false;
            }
        }
        if (used == EStepPolicy::Auto)
        {
            // Truncation pays while it keeps few of the pairs: measure the kept share on 64 spread
            // rows. Otherwise the fast Gauss transform when its plan is clearly cheaper.
            used = EStepPolicy::Dense;
            const double extra = twoSigma2 * std::log(double(m) / settings.Tolerance);
            if (std::sqrt(extra) < Diagonal(s.Target(), moved))
            {
                s.SourceTree.Build(moved);
                s.SourceTreeCurrent = true;
                const std::size_t samples = std::min<std::size_t>(64u, n);
                std::uint64_t kept = 0u;
                for (std::size_t k = 0; k < samples; ++k)
                {
                    const std::size_t j = k * n / samples;
                    const double minDistance = s.SourceTree.NearestSquared(s.TargetX[j], s.TargetY[j], s.TargetZ[j]);
                    s.SourceTree.ForEachWithin(s.TargetX[j], s.TargetY[j], s.TargetZ[j], minDistance + extra,
                                               [&](std::size_t, double) { ++kept; });
                }
                // Measured break-even: a kept pair costs about 3.7 dense pairs (neighbor search and
                // scatter), so truncation pays below about a quarter of the pairs.
                if (double(kept) < 0.25 * double(samples) * double(m)) used = EStepPolicy::Truncated;
            }
            if (used == EStepPolicy::Dense && allowFastGauss &&
                s.PlanFastGauss(moved, sigma2, settings.Tolerance, true, threads, sourcePlan, targetPlan))
                used = EStepPolicy::FastGauss;
        }
        else if (used == EStepPolicy::FastGauss &&
                 !s.PlanFastGauss(moved, sigma2, settings.Tolerance, false, threads, sourcePlan, targetPlan))
            used = EStepPolicy::Dense; // no plan meets the bound
        bool externalFallback = false;
        // Kernels too narrow for fp32 terms (see ExternalErrorEstimate) stay on the exact CPU pass.
        // Rows too narrow for fp32 terms (see ExternalRowEstimates) run exactly on the CPU; when
        // they are the majority the whole iteration does.
        std::vector<double> rowEstimate;
        std::vector<std::uint32_t> cpuRows;
        std::size_t cpuRowCount = 0u;
        if (wantExternal && used == EStepPolicy::Dense)
        {
            s.ExternalRowEstimates(moved, sigma2, logOutlier, threads, rowEstimate);
            cpuRows.resize(n);
            for (std::size_t j = 0; j < n; ++j)
            {
                cpuRows[j] = rowEstimate[j] > kExternalErrorLimit ? 1u : 0u;
                cpuRowCount += cpuRows[j];
            }
        }
        out.ExternalCpuRows = 0u;
        if (wantExternal && used == EStepPolicy::Dense && 2u * cpuRowCount <= n)
        {
            external = s.ExternalRows(settings, moved, sigma2, logOutlier, rowEstimate, cpuRows, threads, out);
            if (external) used = EStepPolicy::Vulkan;
            else
            {
                externalFallback = true;
                s.Resize(n, m, out);
            }
        }
        s.ExtraEvaluations = 0u;
        s.FixedEntries = 0u;
        // The external form is two-pass (every pair twice), like blocks == 0.
        const std::size_t blocks = used == EStepPolicy::FastGauss || nystrom || external
            ? 0u : Impl::RowBlocks(n, m, settings.PartialBudgetBytes, used == EStepPolicy::Truncated);
        if (nystrom || external) {}
        else if (used == EStepPolicy::FastGauss)
            s.FastGaussRows(moved, sigma2, logOutlier, settings.Tolerance, sourcePlan, targetPlan, out, threads);
        else if (blocks > 0u)
            s.BlockedRows(moved, twoSigma2, logOutlier, used == EStepPolicy::Truncated, settings.Tolerance, blocks, out, threads);
        else if (used == EStepPolicy::Truncated)
            s.TruncatedRows(moved, twoSigma2, logOutlier, settings.Tolerance, out, threads);
        else
            s.DenseRows(moved, twoSigma2, logOutlier, out, threads);

        out.Used = used;
        out.LogDenominatorSum = 0.0;
        out.Matched = 0.0;
        out.ErrorBound = 0.0;
        out.KernelEvaluations = 0u;
        for (std::size_t j = 0; j < n; ++j)
        {
            out.LogDenominatorSum += s.LogDenominator[j];
            out.Matched += out.Pt1[j];
            out.ErrorBound = std::max(out.ErrorBound, s.RowBound[j]);
            out.KernelEvaluations += s.RowEvaluations[j];
        }
        // The two-pass fallback evaluates every kept pair twice.
        if (blocks == 0u && used != EStepPolicy::FastGauss && !nystrom) out.KernelEvaluations *= 2u;
        out.KernelEvaluations += s.ExtraEvaluations + nystromEvaluations;
        out.SampledError = nystrom ? sampledError : 0.0;
        out.ExternalFallback = externalFallback;
        s.SourceTreeCurrent = false;
        s.UpdateScales(out.P1);
        return std::isfinite(out.LogDenominatorSum) && out.Matched > std::numeric_limits<double>::min() * double(n);
    }

    std::vector<std::uint32_t> SamplePoints(const PointSet points, const std::size_t count,
                                            const PointSampling::Params& params)
    {
        return SamplePoints(PointSampling::Order(PointSampling::PointView{points.X, points.Y, points.Z}, params, count),
                            points.Size(), count);
    }

    std::vector<std::uint32_t> SamplePoints(const PointSampling::Result& order, const std::size_t points,
                                            const std::size_t count)
    {
        std::vector<std::uint32_t> out;
        if (!order.Succeeded() || order.Order.size() > count ||
            (!order.Clearance.empty() && order.Clearance.size() != order.Order.size()))
            return out;
        std::vector<bool> seen(points, false);
        for (const std::uint32_t i : order.Order)
        {
            if (i >= points || seen[i]) return out;
            seen[i] = true;
        }
        for (std::size_t k = 0; k < order.Order.size(); ++k)
        {
            if (k > 0 && !order.Clearance.empty() && !(order.Clearance[k] > 0.0)) break;
            out.push_back(order.Order[k]);
        }
        return out;
    }

    bool BuildLowRankGaussianKernel(const PointSet points, const double beta, const std::uint32_t rank,
                                    const std::uint32_t threads, LowRankKernel& out,
                                    const PointSampling::Params& landmarkSampling)
    {
        out = {};
        const std::size_t m = points.Size();
        if (m == 0 || rank == 0u || !(beta > 0.0) || !std::isfinite(beta)) return false;
        const std::uint32_t workers = ResolveThreads(threads);
        const double inverse = -1.0 / (2.0 * beta * beta);
        const auto kernel = [&](const std::size_t a, const std::size_t b)
        {
            return std::exp(inverse * Squared(points.X[a], points.Y[a], points.Z[a], points.X[b], points.Y[b], points.Z[b]));
        };

        // Exact farthest-point landmarks from point 0 (Geometry.PointSampling; ties keep the
        // lowest index), cut before the first duplicate of an earlier landmark.
        const std::size_t landmarkCount = std::min<std::size_t>(m, std::max<std::size_t>(2u * rank, rank + 32u));
        std::vector<std::size_t> landmarks;
        for (const std::uint32_t i : SamplePoints(points, landmarkCount, landmarkSampling)) landmarks.push_back(i);
        if (landmarks.empty()) return false;
        const Eigen::Index count = Eigen::Index(landmarks.size());

        // G ~= C W^+ C^T = F F^T with C_il = k(p_i, z_l), W = k(z, z) and F = C U S^{-1/2} on W's
        // numerically positive spectrum. C and F are formed block by block and never stored whole.
        Eigen::MatrixXd w(count, count);
        for (Eigen::Index a = 0; a < count; ++a)
            for (Eigen::Index b = 0; b < count; ++b) w(a, b) = kernel(landmarks[std::size_t(a)], landmarks[std::size_t(b)]);
        const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> wEigen(w);
        if (wEigen.info() != Eigen::Success) return false;
        const Eigen::VectorXd& s = wEigen.eigenvalues();
        const double floor = 1.0e-10 * s.maxCoeff();
        std::vector<Eigen::Index> keep;
        for (Eigen::Index k = 0; k < count; ++k)
            if (s(k) > floor) keep.push_back(k);
        if (keep.empty()) return false;
        Eigen::MatrixXd whiten(count, Eigen::Index(keep.size()));
        for (std::size_t k = 0; k < keep.size(); ++k)
            whiten.col(Eigen::Index(k)) = wEigen.eigenvectors().col(keep[k]) / std::sqrt(s(keep[k]));
        const Eigen::Index columns = whiten.cols();

        constexpr std::size_t kBlock = 2048;
        const std::size_t blocks = (m + kBlock - 1) / kBlock;
        const auto kernelBlock = [&](const std::size_t b)
        {
            const std::size_t first = b * kBlock, last = std::min(m, first + kBlock);
            Eigen::MatrixXd block(Eigen::Index(last - first), count);
            for (std::size_t i = first; i < last; ++i)
                for (Eigen::Index l = 0; l < count; ++l) block(Eigen::Index(i - first), l) = kernel(i, landmarks[std::size_t(l)]);
            return block;
        };
        // B = F^T F: row blocks fold into at most 64 fixed groups (block b -> group b mod G, in
        // block order), summed in group order; thread-count independent, O(G L^2) memory.
        const std::size_t groups = std::min<std::size_t>(blocks, 64u);
        std::vector<Eigen::MatrixXd> partial(groups, Eigen::MatrixXd::Zero(columns, columns));
        ParallelFor(groups, 1, workers, [&](const std::size_t begin, const std::size_t end)
        {
            for (std::size_t g = begin; g < end; ++g)
                for (std::size_t b = g; b < blocks; b += groups)
                {
                    const Eigen::MatrixXd fb = kernelBlock(b) * whiten;
                    partial[g].noalias() += fb.transpose() * fb;
                }
        });
        Eigen::MatrixXd gram = Eigen::MatrixXd::Zero(columns, columns);
        for (const Eigen::MatrixXd& block : partial) gram += block;
        const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> bEigen(gram);
        if (bEigen.info() != Eigen::Success) return false;
        const Eigen::VectorXd& lambda = bEigen.eigenvalues(); // ascending
        const double top = lambda.maxCoeff();
        std::vector<Eigen::Index> order;
        for (Eigen::Index k = columns; k-- > 0 && order.size() < rank;)
            if (lambda(k) > 1.0e-12 * top) order.push_back(k);
        if (order.empty()) return false;
        const Eigen::Index r = Eigen::Index(order.size());
        // Basis = F V Lambda^{-1/2} = C (whiten V Lambda^{-1/2}).
        Eigen::MatrixXd project(columns, r);
        out.Eigenvalues.resize(std::size_t(r));
        for (Eigen::Index k = 0; k < r; ++k)
        {
            project.col(k) = bEigen.eigenvectors().col(order[std::size_t(k)]) / std::sqrt(lambda(order[std::size_t(k)]));
            out.Eigenvalues[std::size_t(k)] = lambda(order[std::size_t(k)]);
        }
        const Eigen::MatrixXd toBasis = whiten * project;
        Eigen::MatrixXd basis(Eigen::Index(m), r);
        ParallelFor(blocks, 1, workers, [&](const std::size_t begin, const std::size_t end)
        {
            for (std::size_t b = begin; b < end; ++b)
            {
                const Eigen::MatrixXd cb = kernelBlock(b);
                basis.middleRows(Eigen::Index(b * kBlock), cb.rows()).noalias() = cb * toBasis;
            }
        });

        // A-posteriori error on up to 32 evenly spaced exact rows.
        const std::size_t samples = std::min<std::size_t>(m, 32u);
        std::vector<double> errorSq(samples, 0.0), normSq(samples, 0.0);
        const Eigen::VectorXd eigen = Eigen::Map<const Eigen::VectorXd>(out.Eigenvalues.data(), r);
        ParallelFor(samples, 1, workers, [&](const std::size_t begin, const std::size_t end)
        {
            for (std::size_t s2 = begin; s2 < end; ++s2)
            {
                const std::size_t row = s2 * m / samples;
                const Eigen::VectorXd weighted = basis.row(Eigen::Index(row)).transpose().cwiseProduct(eigen);
                const Eigen::VectorXd approximate = basis * weighted; // one GEMV over the column-major basis
                for (std::size_t i = 0; i < m; ++i)
                {
                    const double exact = kernel(row, i);
                    const double approx = approximate(Eigen::Index(i));
                    errorSq[s2] += (exact - approx) * (exact - approx);
                    normSq[s2] += exact * exact;
                }
            }
        });
        double errorTotal = 0.0, normTotal = 0.0;
        for (std::size_t k = 0; k < samples; ++k) { errorTotal += errorSq[k]; normTotal += normSq[k]; }
        out.EstimatedRelativeError = normTotal > 0.0 ? std::sqrt(errorTotal / normTotal) : 0.0;
        out.Rank = std::uint32_t(r);
        out.Landmarks = std::uint32_t(count);
        out.Basis.assign(basis.data(), basis.data() + basis.size());
        out.LandmarkIndices.assign(landmarks.begin(), landmarks.end());
        out.Extension.assign(toBasis.data(), toBasis.data() + toBasis.size());
        return std::isfinite(out.EstimatedRelativeError);
    }
}
