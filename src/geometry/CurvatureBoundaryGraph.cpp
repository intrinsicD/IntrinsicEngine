#include "CurvatureBoundaryGraph.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <tuple>
#include <utility>

namespace Geometry::CurvatureSegmentation::BoundaryDetail
{
namespace
{
using Index = std::uint32_t;
using Labels = std::vector<Index>;
struct Link
{
    Index To, EdgeId;
};
using Adjacency = std::vector<std::vector<Link>>;
constexpr Index invalid = std::numeric_limits<Index>::max();

struct Statistics
{
    double Weight{}, Variance{};
    std::array<double, 2> Mean{};
};
double MergeIncrease(const Statistics &a, const Statistics &b)
{
    if (a.Weight == 0 || b.Weight == 0)
        return 0;
    const double dx = a.Mean[0] - b.Mean[0], dy = a.Mean[1] - b.Mean[1];
    const double weight = std::min(a.Weight, b.Weight) *
                          (std::max(a.Weight, b.Weight) / (a.Weight + b.Weight));
    const double scale = std::sqrt(weight);
    const double distance = std::hypot(scale * dx, scale * dy);
    return distance * distance;
}
void Accumulate(Statistics &a, const Statistics &b)
{
    if (b.Weight == 0)
        return;
    if (a.Weight == 0)
    {
        a = b;
        return;
    }
    const double increase = MergeIncrease(a, b), weight = a.Weight + b.Weight;
    for (int i = 0; i < 2; ++i)
        a.Mean[i] = a.Weight >= b.Weight
                        ? a.Mean[i] + (b.Weight / weight) * (b.Mean[i] - a.Mean[i])
                        : b.Mean[i] + (a.Weight / weight) * (a.Mean[i] - b.Mean[i]);
    a.Weight = weight;
    a.Variance += b.Variance + increase;
}
Statistics Singleton(const Sample &s)
{
    return {s.Weight, 0, s.Value};
}

double Energy(std::span<const Edge> edges, const Labels &labels,
              std::span<const Sample> samples = {}, double regionCost = 0)
{
    double value = 0;
    for (const auto &e : edges)
        if (labels[e.A] != labels[e.B])
            value += e.Cost;
    if (!samples.empty() || regionCost > 0)
    {
        const Index count = *std::max_element(labels.begin(), labels.end()) + 1;
        std::vector<Statistics> statistics(count);
        std::vector<bool> active(count);
        for (Index v = 0; v < labels.size(); ++v)
        {
            active[labels[v]] = true;
            if (!samples.empty())
                Accumulate(statistics[labels[v]], Singleton(samples[v]));
        }
        for (Index c = 0; c < count; ++c)
            if (active[c])
                value += regionCost + statistics[c].Variance;
    }
    return value;
}

Index ConnectedLabels(const Adjacency &adjacency, Labels &labels)
{
    Labels compact(labels.size(), invalid), queue;
    Index count = 0;
    for (Index v = 0; v < labels.size(); ++v)
    {
        if (compact[v] != invalid)
            continue;
        queue.clear();
        queue.push_back(v);
        compact[v] = count;
        for (std::size_t i = 0; i < queue.size(); ++i)
            for (auto link : adjacency[queue[i]])
                if (compact[link.To] == invalid && labels[link.To] == labels[v])
                {
                    compact[link.To] = count;
                    queue.push_back(link.To);
                }
        ++count;
    }
    labels.swap(compact);
    return count;
}

struct Aggregate
{
    double Cost{};
    bool Hard{};
};
struct Join
{
    double Gain;
    Index A, B;
    bool operator<(const Join &other) const
    {
        if (Gain != other.Gain)
            return Gain < other.Gain;
        return std::tie(A, B) > std::tie(other.A, other.B);
    }
};

bool Contract(std::span<const Edge> edges, Labels &labels, double tolerance,
              const Options &options, Result &result, std::span<const Sample> samples,
              std::span<const double> nodeAreas = {})
{
    const Index count = *std::max_element(labels.begin(), labels.end()) + 1;
    std::vector<std::map<Index, Aggregate>> graph(count);
    Labels parent(count);
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<Statistics> statistics(count);
    std::vector<double> regionAreas(count);
    const bool cleanup = !nodeAreas.empty();
    if (cleanup)
        for (Index v = 0; v < labels.size(); ++v)
            regionAreas[labels[v]] += nodeAreas[v];
    const auto eligible = [&](Index a, Index b)
    {
        return !cleanup || regionAreas[a] < options.MinimumRegionArea ||
               regionAreas[b] < options.MinimumRegionArea;
    };
    if (!samples.empty())
        for (Index v = 0; v < labels.size(); ++v)
            Accumulate(statistics[labels[v]], Singleton(samples[v]));
    const auto gain = [&](Index a, Index b, double cost)
    { return cost + options.RegionCost - MergeIncrease(statistics[a], statistics[b]); };
    for (auto e : edges)
    {
        Index a = labels[e.A], b = labels[e.B];
        if (a == b)
            continue;
        auto &x = graph[a][b];
        x.Cost += e.Cost;
        x.Hard |= e.Hard;
        auto &y = graph[b][a];
        y.Cost += e.Cost;
        y.Hard |= e.Hard;
    }
    std::priority_queue<Join> queue;
    const auto push = [&](Index a, Index b, const Aggregate &edge)
    {
        if (edge.Hard || !eligible(a, b) ||
            (!cleanup && edge.Cost + options.RegionCost <= tolerance))
            return;
        const double value = gain(a, b, edge.Cost);
        if (cleanup || value > tolerance)
            queue.push({value, std::min(a, b), std::max(a, b)});
    };
    std::size_t liveEdges = 0;
    for (Index a = 0; a < count; ++a)
        for (const auto &[b, edge] : graph[a])
            if (a < b)
            {
                ++liveEdges;
                push(a, b, edge);
            }
    while (!queue.empty())
    {
        auto candidate = queue.top();
        queue.pop();
        Index a = candidate.A, b = candidate.B;
        if (parent[a] != a || parent[b] != b)
            continue;
        auto it = graph[a].find(b);
        if (it == graph[a].end() || it->second.Hard || !eligible(a, b) ||
            gain(a, b, it->second.Cost) != candidate.Gain)
            continue;
        if (++result.AttemptedMoves > options.MaximumMoves)
            return false;
        // Merge the smaller sparse adjacency into the larger one.
        if (graph[a].size() < graph[b].size())
            std::swap(a, b);
        graph[a].erase(b);
        graph[b].erase(a);
        --liveEdges;
        parent[b] = a;
        Accumulate(statistics[a], statistics[b]);
        regionAreas[a] += regionAreas[b];
        for (const auto &[c, edge] : graph[b])
        {
            graph[c].erase(b);
            if (graph[a].contains(c))
                --liveEdges;
            auto &target = graph[a][c];
            target.Cost += edge.Cost;
            target.Hard |= edge.Hard;
            graph[c][a] = target;
            if (samples.empty())
                push(a, c, target);
        }
        // A changed regional mean changes gains even on untouched boundary edges.
        if (!samples.empty())
            for (const auto &[c, edge] : graph[a])
                push(a, c, edge);
        graph[b].clear();
        ++result.Contractions;
        if (queue.size() > 4 * std::max<std::size_t>(liveEdges, 1))
        {
            // Discard stale proposals before they dominate workspace.
            queue = {};
            for (Index u = 0; u < count; ++u)
                if (parent[u] == u)
                    for (const auto &[v, edge] : graph[u])
                        if (u < v)
                            push(u, v, edge);
        }
    }
    for (auto &label : labels)
    {
        Index root = label;
        while (parent[root] != root)
            root = parent[root];
        while (parent[label] != label)
        {
            Index next = parent[label];
            parent[label] = root;
            label = next;
        }
        label = root;
    }
    return true;
}

struct FlowArc
{
    Index To, Reverse;
    double Residual;
};

// Positive capacities propose a contour; acceptance always uses the original
// signed energy. Iterative blocking-flow paths avoid mesh-sized call stacks.
bool ProposeSplit(Index region, Index fresh, const std::vector<Index> &vertices,
                  const Adjacency &adjacency, std::span<const Edge> edges, Labels &labels,
                  Labels &local, double tolerance, const Options &options, Result &result,
                  bool &improved, std::span<const Sample> samples)
{
    std::vector<Index> negative, internal;
    Statistics original;
    if (!samples.empty())
        for (auto v : vertices)
            Accumulate(original, Singleton(samples[v]));
    double positiveScale = 0, negativeCost = 0;
    for (Index v : vertices)
        for (auto link : adjacency[v])
            if (v < link.To && labels[link.To] == region)
            {
                internal.push_back(link.EdgeId);
                const double cost = edges[link.EdgeId].Cost;
                positiveScale = std::max(positiveScale, cost);
                if (cost < 0)
                {
                    negativeCost += cost;
                    negative.push_back(link.EdgeId);
                }
            }
    // Any split adds at least one connected region. Even retaining every
    // negative edge and eliminating all variance cannot beat this bound.
    if (options.RegionCost + negativeCost - original.Variance >= -tolerance)
        return true;
    std::sort(negative.begin(), negative.end(),
              [&](Index a, Index b)
              {
                  return std::tie(edges[a].Cost, edges[a].A, edges[a].B) <
                         std::tie(edges[b].Cost, edges[b].A, edges[b].B);
              });
    for (Index i = 0; i < vertices.size(); ++i)
        local[vertices[i]] = i;
    const Index n = static_cast<Index>(vertices.size());
    std::vector<std::pair<Index, Index>> terminals;
    if (original.Variance > tolerance)
    {
        const auto farthest = [&](const std::array<double, 2> &mean)
        {
            Index best = vertices.front();
            double distance = -1;
            for (auto v : vertices)
            {
                const double dx = samples[v].Value[0] - mean[0],
                             dy = samples[v].Value[1] - mean[1];
                if (dx * dx + dy * dy > distance)
                {
                    distance = dx * dx + dy * dy;
                    best = v;
                }
            }
            return best;
        };
        const Index a = farthest(original.Mean), b = farthest(samples[a].Value);
        if (a != b)
            terminals.emplace_back(a, b);
    }
    const auto negativeTrials = std::min<std::size_t>(
        options.MaximumSplitTrials - terminals.size(), negative.size());
    for (std::size_t trial = 0; trial < negativeTrials; ++trial)
    {
        const auto e = edges[negative[trial * negative.size() / negativeTrials]];
        terminals.emplace_back(e.A, e.B);
    }
    for (auto [globalSource, globalSink] : terminals)
    {
        const Index source = local[globalSource], sink = local[globalSink];
        double capacityScale = positiveScale;
        std::vector<double> costA(n), costB(n);
        if (!samples.empty())
            for (Index i = 0; i < n; ++i)
            {
                const auto &point = samples[vertices[i]];
                for (int axis = 0; axis < 2; ++axis)
                {
                    const double a =
                        point.Value[axis] - samples[globalSource].Value[axis];
                    const double b = point.Value[axis] - samples[globalSink].Value[axis];
                    costA[i] += point.Weight * a * a;
                    costB[i] += point.Weight * b * b;
                }
                // A per-node constant cannot affect the cut. Removing it
                // avoids artificial source-to-sink flow through every node.
                const double constant = std::min(costA[i], costB[i]);
                costA[i] -= constant;
                costB[i] -= constant;
                capacityScale = std::max({capacityScale, costA[i], costB[i]});
            }
        std::vector<std::vector<FlowArc>> graph(n);
        for (auto id : internal)
        {
            auto e = edges[id];
            if (e.Cost <= 0)
                continue;
            Index a = local[e.A], b = local[e.B];
            const Index ai = static_cast<Index>(graph[a].size()),
                        bi = static_cast<Index>(graph[b].size());
            const double capacity = e.Cost / capacityScale;
            graph[a].push_back({b, bi, capacity});
            graph[b].push_back({a, ai, capacity});
        }
        const auto directed = [&](Index a, Index b, double cost)
        {
            if (a == b || cost <= 0)
                return;
            const Index ai = static_cast<Index>(graph[a].size()),
                        bi = static_cast<Index>(graph[b].size());
            graph[a].push_back({b, bi, cost / capacityScale});
            graph[b].push_back({a, ai, 0});
        };
        for (Index i = 0; i < n; ++i)
        {
            directed(source, i, costB[i]);
            directed(i, sink, costA[i]);
        }
        std::vector<int> level(n);
        Labels queue, next(n), path, arcs;
        for (;;)
        {
            std::fill(level.begin(), level.end(), -1);
            queue.clear();
            queue.push_back(source);
            level[source] = 0;
            for (std::size_t i = 0; i < queue.size(); ++i)
            {
                if (level[sink] >= 0 && level[queue[i]] >= level[sink])
                    break;
                for (const auto &arc : graph[queue[i]])
                {
                    if (++result.FlowEdgeVisits > options.MaximumFlowEdgeVisits)
                        return false;
                    if (arc.Residual > 0 && level[arc.To] < 0)
                    {
                        level[arc.To] = level[queue[i]] + 1;
                        queue.push_back(arc.To);
                    }
                }
            }
            if (level[sink] < 0)
                break;
            std::fill(next.begin(), next.end(), 0);
            path.clear();
            arcs.clear();
            path.push_back(source);
            while (!path.empty())
            {
                const Index v = path.back();
                if (v == sink)
                {
                    double flow = std::numeric_limits<double>::infinity();
                    for (std::size_t i = 0; i < arcs.size(); ++i)
                        flow = std::min(flow, graph[path[i]][arcs[i]].Residual);
                    for (std::size_t i = 0; i < arcs.size(); ++i)
                    {
                        auto &arc = graph[path[i]][arcs[i]];
                        arc.Residual = std::max(0.0, arc.Residual - flow);
                        graph[arc.To][arc.Reverse].Residual += flow;
                    }
                    path.resize(1);
                    arcs.clear();
                    continue;
                }
                while (next[v] < graph[v].size())
                {
                    if (++result.FlowEdgeVisits > options.MaximumFlowEdgeVisits)
                        return false;
                    auto &arc = graph[v][next[v]];
                    if (arc.Residual > 0 && level[arc.To] == level[v] + 1)
                        break;
                    ++next[v];
                }
                if (next[v] == graph[v].size())
                {
                    level[v] = -1;
                    path.pop_back();
                    if (!arcs.empty())
                    {
                        arcs.pop_back();
                        if (!path.empty())
                            ++next[path.back()];
                    }
                }
                else
                {
                    arcs.push_back(next[v]);
                    path.push_back(graph[v][next[v]].To);
                }
            }
        }
        double delta = 0;
        for (auto id : internal)
        {
            auto e = edges[id];
            if ((level[local[e.A]] >= 0) != (level[local[e.B]] >= 0))
                delta += e.Cost;
        }
        // The number and fit of connected output pieces matter for the
        // regional objective, even when one side of the binary cut disconnects.
        if (!samples.empty() || options.RegionCost > 0)
        {
            std::vector<bool> seen(n);
            Labels pending;
            std::size_t pieces = 0;
            double variance = 0;
            for (Index seed = 0; seed < n; ++seed)
            {
                if (seen[seed])
                    continue;
                ++pieces;
                Statistics stats;
                pending.clear();
                pending.push_back(seed);
                seen[seed] = true;
                for (std::size_t j = 0; j < pending.size(); ++j)
                {
                    const Index v = vertices[pending[j]];
                    if (!samples.empty())
                        Accumulate(stats, Singleton(samples[v]));
                    for (auto link : adjacency[v])
                        if (labels[link.To] == region)
                        {
                            const Index next = local[link.To];
                            if (!seen[next] && (level[next] >= 0) == (level[seed] >= 0))
                            {
                                seen[next] = true;
                                pending.push_back(next);
                            }
                        }
                }
                variance += stats.Variance;
            }
            delta +=
                variance - original.Variance + options.RegionCost * double(pieces - 1);
        }
        if (delta < -tolerance)
        {
            for (auto v : vertices)
                if (level[local[v]] >= 0)
                {
                    labels[v] = fresh;
                    ++result.AcceptedMoves;
                }
            improved = true;
            return true;
        }
    }
    return true;
}

struct Move
{
    double Gain;
    Index Vertex;
    std::uint64_t Version;
    bool operator<(const Move &other) const
    {
        if (Gain != other.Gain)
            return Gain < other.Gain;
        return Vertex > other.Vertex;
    }
};

// Temporary moves may disconnect a label. Connected components are relabeled
// after the pass, which changes no edge cut or objective value. Hard constraints
// hold even in temporary states; only the best improving prefix is committed.
bool RefinePair(Index a, Index b, const std::vector<Index> &vertices,
                const Adjacency &adjacency, std::span<const Edge> edges, Labels &labels,
                std::vector<std::uint64_t> &versions, std::vector<std::uint64_t> &locked,
                std::uint64_t stamp, double tolerance, const Options &options,
                Result &result, bool &improved,
                std::vector<std::pair<Index, Index>> &accepted)
{
    std::priority_queue<Move> queue;
    const auto push = [&](Index v)
    {
        if (locked[v] == stamp)
            return;
        const Index from = labels[v], to = from == a ? b : a;
        double gain = 0;
        bool valid = true, touchesTarget = false;
        for (auto link : adjacency[v])
        {
            auto e = edges[link.EdgeId];
            const Index other = labels[link.To];
            if (other == to)
            {
                gain += e.Cost;
                touchesTarget = true;
                if (e.Hard)
                    valid = false;
            }
            else if (other == from)
                gain -= e.Cost;
        }
        const auto version = ++versions[v];
        if (valid && touchesTarget)
            queue.push({gain, v, version});
    };
    for (auto v : vertices)
        push(v);
    std::vector<std::pair<Index, Index>> history;
    history.reserve(vertices.size());
    double cumulative = 0, best = tolerance;
    std::size_t prefix = 0;
    while (!queue.empty())
    {
        const auto move = queue.top();
        queue.pop();
        const Index v = move.Vertex;
        if (locked[v] == stamp || versions[v] != move.Version)
            continue;
        if (++result.AttemptedMoves > options.MaximumMoves)
        {
            for (auto it = history.rbegin(); it != history.rend(); ++it)
                labels[it->first] = it->second;
            return false;
        }
        locked[v] = stamp;
        history.emplace_back(v, labels[v]);
        labels[v] = labels[v] == a ? b : a;
        cumulative += move.Gain;
        if (cumulative > best)
        {
            best = cumulative;
            prefix = history.size();
        }
        for (auto link : adjacency[v])
            if (labels[link.To] == a || labels[link.To] == b)
                push(link.To);
        if (history.size() - prefix >= options.MaximumNonImprovingMoves)
            break;
    }
    accepted.assign(history.begin(), history.begin() + prefix);
    for (std::size_t i = history.size(); i > prefix; --i)
        labels[history[i - 1].first] = history[i - 1].second;
    if (prefix)
    {
        result.AcceptedMoves += prefix;
        improved = true;
    }
    return true;
}
} // namespace

Result Solve(Index nodes, std::span<const Edge> input, const Options &options,
             std::span<const Sample> samples, std::span<const double> nodeAreas)
{
    Result result;
    if (!nodes || nodes >= invalid / 2 || input.size() >= invalid ||
        !options.MaximumSweeps || !options.MaximumMoves ||
        !options.MaximumFlowEdgeVisits || !options.MaximumSplitTrials ||
        options.MaximumSplitTrials > 32 || !options.MaximumNonImprovingMoves ||
        options.ExactNodeLimit > 8 || !std::isfinite(options.RegionCost) ||
        options.RegionCost < 0 || (!samples.empty() && samples.size() != nodes) ||
        !std::isfinite(options.MinimumRegionArea) || options.MinimumRegionArea < 0 ||
        (options.MinimumRegionArea > 0 && nodeAreas.size() != nodes) ||
        !std::isfinite(options.RelativeTolerance) || options.RelativeTolerance < 0 ||
        options.RelativeTolerance > 1e-6)
        return result;
    double totalArea = 0;
    if (options.MinimumRegionArea > 0)
        for (double area : nodeAreas)
        {
            if (!std::isfinite(area) || area <= 0)
                return result;
            totalArea += area;
        }
    if (!std::isfinite(totalArea))
        return result;
    double sampleScale = 0, totalWeight = 0;
    for (auto point : samples)
    {
        if (!std::isfinite(point.Weight) || point.Weight <= 0 ||
            !std::isfinite(point.Value[0]) || !std::isfinite(point.Value[1]))
            return result;
        totalWeight += point.Weight;
        sampleScale += point.Weight * (point.Value[0] * point.Value[0] +
                                       point.Value[1] * point.Value[1]);
    }
    if (!std::isfinite(sampleScale) || !std::isfinite(totalWeight))
        return result;
    std::vector<Edge> edges(input.begin(), input.end());
    for (auto &e : edges)
    {
        if (e.A >= nodes || e.B >= nodes || e.A == e.B || !std::isfinite(e.Cost))
            return result;
        if (e.B < e.A)
            std::swap(e.A, e.B);
    }
    std::sort(edges.begin(), edges.end(),
              [](auto a, auto b)
              {
                  return std::tie(a.A, a.B, a.Cost, a.Hard) <
                         std::tie(b.A, b.B, b.Cost, b.Hard);
              });
    Adjacency adjacency(nodes);
    double scale = 0;
    for (Index i = 0; i < edges.size(); ++i)
    {
        auto e = edges[i];
        adjacency[e.A].push_back({e.B, i});
        adjacency[e.B].push_back({e.A, i});
        scale += std::abs(e.Cost);
        result.LowerBound += e.Hard ? e.Cost : std::min(0.0, e.Cost);
    }
    if (!std::isfinite(scale) || !std::isfinite(result.LowerBound))
        return result;
    scale += sampleScale + double(nodes) * options.RegionCost;
    if (!std::isfinite(scale))
        return result;
    result.LowerBound += options.RegionCost;
    const double tolerance = scale * options.RelativeTolerance;
    Labels labels(nodes);
    std::iota(labels.begin(), labels.end(), 0);
    result.InitialEnergy = Energy(edges, labels, samples, options.RegionCost);
    if (nodes <= options.ExactNodeLimit)
    {
        Labels candidate(nodes, 0), bestLabels = labels;
        double best = result.InitialEnergy;
        const auto enumerate = [&](auto &&self, Index v, Index maximum) -> bool
        {
            if (v == nodes)
            {
                if (++result.AttemptedMoves > options.MaximumMoves)
                    return false;
                auto connected = candidate;
                ConnectedLabels(adjacency, connected);
                const double value =
                    Energy(edges, connected, samples, options.RegionCost);
                if (value < best || (value == best && connected < bestLabels))
                {
                    best = value;
                    bestLabels = std::move(connected);
                }
                return true;
            }
            for (Index label = 0; label <= maximum + 1; ++label)
            {
                bool feasible = true;
                for (auto link : adjacency[v])
                    if (link.To < v && edges[link.EdgeId].Hard &&
                        candidate[link.To] == label)
                    {
                        feasible = false;
                        break;
                    }
                if (!feasible)
                    continue;
                candidate[v] = label;
                if (!self(self, v + 1, std::max(maximum, label)))
                    return false;
            }
            return true;
        };
        if (!enumerate(enumerate, 1, 0))
        {
            result.State = Status::WorkLimit;
            return result;
        }
        labels = std::move(bestLabels);
        result.Exact = true;
    }
    else
    {
        if (!Contract(edges, labels, tolerance, options, result, samples))
        {
            result.State = Status::WorkLimit;
            return result;
        }
        ConnectedLabels(adjacency, labels);
        std::vector<std::uint64_t> versions(nodes), locked(nodes);
        Labels local(nodes);
        std::uint64_t stamp = 0;
        bool converged = false;
        for (Index sweep = 0; sweep < options.MaximumSweeps; ++sweep)
        {
            ++result.Sweeps;
            const double before = Energy(edges, labels, samples, options.RegionCost);
            const Index count = *std::max_element(labels.begin(), labels.end()) + 1;
            // Pairs are reconstructed each sweep; graph storage is sparse.
            std::map<std::pair<Index, Index>, bool> pairs;
            for (auto e : edges)
                if (labels[e.A] != labels[e.B])
                    pairs[{std::min(labels[e.A], labels[e.B]),
                           std::max(labels[e.A], labels[e.B])}] = true;
            // Membership is updated after each accepted prefix, without whole-graph scans
            // per pair.
            std::vector<std::vector<Index>> members(count * 2);
            std::vector<Index> position(nodes);
            for (Index v = 0; v < nodes; ++v)
            {
                position[v] = static_cast<Index>(members[labels[v]].size());
                members[labels[v]].push_back(v);
            }
            bool improved = false;
            const auto refine = [&](Index a, Index b) -> bool
            {
                const Index smaller = members[a].size() <= members[b].size() ? a : b;
                const Index other = smaller == a ? b : a;
                std::vector<Index> vertices;
                for (auto v : members[smaller])
                    for (auto link : adjacency[v])
                        if (labels[link.To] == other)
                        {
                            vertices.push_back(v);
                            vertices.push_back(link.To);
                        }
                if (vertices.empty())
                    return true;
                std::sort(vertices.begin(), vertices.end());
                vertices.erase(std::unique(vertices.begin(), vertices.end()),
                               vertices.end());
                std::vector<std::pair<Index, Index>> accepted;
                if (!RefinePair(a, b, vertices, adjacency, edges, labels, versions,
                                locked, ++stamp, tolerance, options, result, improved,
                                accepted))
                    return false;
                for (auto [v, old] : accepted)
                {
                    const Index now = labels[v];
                    auto &from = members[old];
                    const Index last = from.back();
                    from[position[v]] = last;
                    position[last] = position[v];
                    from.pop_back();
                    position[v] = static_cast<Index>(members[now].size());
                    members[now].push_back(v);
                }
                return true;
            };
            for (const auto &[pair, unused] : pairs)
                if (samples.empty() && options.RegionCost == 0 &&
                    !members[pair.first].empty() && !members[pair.second].empty() &&
                    !refine(pair.first, pair.second))
                {
                    result.State = Status::WorkLimit;
                    return result;
                }
            for (Index a = 0; a < count; ++a)
                if (members[a].size() > 1 &&
                    !ProposeSplit(a, count + a, members[a], adjacency, edges, labels,
                                  local, tolerance, options, result, improved, samples))
                {
                    result.State = Status::WorkLimit;
                    return result;
                }
            ConnectedLabels(adjacency, labels);
            const auto contractions = result.Contractions;
            if (!Contract(edges, labels, tolerance, options, result, samples))
            {
                result.State = Status::WorkLimit;
                return result;
            }
            ConnectedLabels(adjacency, labels);
            const double after = Energy(edges, labels, samples, options.RegionCost);
            if (!std::isfinite(after) || after > before + tolerance)
            {
                result.State = Status::InvariantFailure;
                return result;
            }
            if (!improved && contractions == result.Contractions)
            {
                converged = true;
                break;
            }
        }
        if (!converged)
        {
            result.State = Status::WorkLimit;
            return result;
        }
    }
    ConnectedLabels(adjacency, labels);
    result.OptimizedEnergy = Energy(edges, labels, samples, options.RegionCost);
    if (options.MinimumRegionArea > 0)
    {
        const auto before = result.Contractions;
        if (!Contract(edges, labels, tolerance, options, result, samples, nodeAreas))
        {
            result.State = Status::WorkLimit;
            return result;
        }
        result.AreaMerges = static_cast<Index>(result.Contractions - before);
        if (result.AreaMerges)
            result.Exact = false;
        const Index count = ConnectedLabels(adjacency, labels);
        std::vector<double> regionAreas(count);
        for (Index v = 0; v < nodes; ++v)
            regionAreas[labels[v]] += nodeAreas[v];
        for (double area : regionAreas)
            result.UnmergeableSmallRegions += area < options.MinimumRegionArea;
    }
    for (auto e : edges)
        if (e.Hard && labels[e.A] == labels[e.B])
        {
            result.State = Status::InvariantFailure;
            return result;
        }
    result.Energy = Energy(edges, labels, samples, options.RegionCost);
    if (!std::isfinite(result.Energy) ||
        result.OptimizedEnergy > result.InitialEnergy + tolerance ||
        result.Energy < result.LowerBound - tolerance)
    {
        result.State = Status::InvariantFailure;
        return result;
    }
    result.Labels = std::move(labels);
    result.State = Status::Success;
    return result;
}
} // namespace Geometry::CurvatureSegmentation::BoundaryDetail
