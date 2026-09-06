module;
#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <queue>
#include <set>
#include <utility>
#include <vector>
module Geometry.HalfedgeMesh.CurvatureExtrema;
import Geometry.HalfedgeMesh;
import Geometry.Curvature;
namespace Geometry::CurvatureExtrema
{
    namespace
    {
        using V = glm::dvec3;
        constexpr auto None = std::numeric_limits<std::uint32_t>::max();
        struct Face
        {
            std::array<std::uint32_t, 3> Verts;
            V Normal;
            double Area;
        };
        struct Neighbor
        {
            std::uint32_t Vertex;
            double Distance;
        };
        struct Surface
        {
            std::vector<V> Positions, Normals;
            std::vector<double> Areas;
            std::vector<bool> Boundary, Barrier;
            std::vector<Face> Faces;
            std::vector<std::uint32_t> SourceFaces;
            std::vector<std::vector<Neighbor>> Adjacent, Support;
            std::vector<std::vector<std::uint32_t>> IncidentFaces;
            V Origin{};
            double Diagonal{};
        };
        bool Finite(V v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        std::pair<V, V> Basis(V n)
        {
            V axis = std::abs(n.x) < 0.8 ? V{1, 0, 0} : V{0, 1, 0};
            V x = glm::normalize(glm::cross(n, axis));
            return {x, glm::cross(n, x)};
        }
        V Gradient(const Surface& s, const Face& f, const std::array<double, 3>& values)
        {
            const V a = s.Positions[f.Verts[0]], b = s.Positions[f.Verts[1]],
                    c = s.Positions[f.Verts[2]];
            return (values[0] * glm::cross(f.Normal, c - b) +
                    values[1] * glm::cross(f.Normal, a - c) +
                    values[2] * glm::cross(f.Normal, b - a)) /
                   (2 * f.Area);
        }
        Status Assemble(const HalfedgeMesh::Mesh& mesh, Surface& s, Diagnostics& d,
                        const Params& params)
        {
            if (mesh.IsSubmeshView())
                return Status::UnsupportedMesh;
            if (!mesh.VertexCount() || !mesh.FaceCount())
                return Status::EmptyMesh;
            const auto count = mesh.VerticesSize();
            s.Positions.resize(count);
            s.Normals.resize(count);
            s.Areas.resize(count);
            s.Adjacent.resize(count);
            s.IncidentFaces.resize(count);
            s.Boundary.resize(count);
            V low{std::numeric_limits<double>::infinity()},
                high{-std::numeric_limits<double>::infinity()};
            for (auto v : mesh.LiveVertices())
            {
                V p{mesh.Position(v)};
                if (!Finite(p))
                    return Status::InvalidGeometry;
                s.Positions[v.Index] = p;
                low = glm::min(low, p);
                high = glm::max(high, p);
            }
            s.Origin = low;
            s.Diagonal = glm::length(high - low);
            d.BoundingBoxDiagonal = s.Diagonal;
            if (!(s.Diagonal > 0) || !std::isfinite(s.Diagonal))
                return Status::InvalidGeometry;
            for (auto v : mesh.LiveVertices())
                s.Positions[v.Index] = (s.Positions[v.Index] - low) / s.Diagonal;
            for (auto face : mesh.LiveFaces())
            {
                Face f{};
                std::size_t n = 0;
                for (auto v : mesh.VerticesAroundFace(face))
                {
                    if (n == 3)
                        return Status::UnsupportedMesh;
                    f.Verts[n++] = v.Index;
                }
                if (n != 3)
                    return Status::UnsupportedMesh;
                V a = s.Positions[f.Verts[0]], b = s.Positions[f.Verts[1]],
                  c = s.Positions[f.Verts[2]];
                V cross = glm::cross(b - a, c - a);
                double twice = glm::length(cross);
                double longest = std::max(
                    {glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c)});
                if (!(longest > 0) || !std::isfinite(twice) ||
                    twice / longest < Curvature::kMinimumReliableTriangleQuality)
                    return Status::InvalidGeometry;
                f.Area = twice / 2;
                f.Normal = cross / twice;
                for (auto v : f.Verts)
                {
                    s.Areas[v] += f.Area / 3;
                    s.Normals[v] += cross;
                    s.IncidentFaces[v].push_back(static_cast<std::uint32_t>(s.Faces.size()));
                }
                s.Faces.push_back(f);
                s.SourceFaces.push_back(face.Index);
            }
            for (auto v : mesh.LiveVertices())
            {
                double norm = glm::length(s.Normals[v.Index]);
                if (s.Areas[v.Index] > 0 && norm <= 1e-14 * s.Areas[v.Index])
                    return Status::InvalidGeometry;
                if (norm > 0)
                    s.Normals[v.Index] /= norm;
            }
            for (auto edge : mesh.LiveEdges())
            {
                auto h = mesh.Halfedge(edge, 0);
                auto a = mesh.FromVertex(h).Index, b = mesh.ToVertex(h).Index;
                double length = glm::length(s.Positions[a] - s.Positions[b]);
                if (!(length > 0))
                    return Status::InvalidGeometry;
                s.Adjacent[a].push_back({b, length});
                s.Adjacent[b].push_back({a, length});
                if (mesh.IsBoundary(edge))
                    s.Boundary[a] = s.Boundary[b] = true;
            }
            for (auto& neighbors : s.Adjacent)
                std::sort(neighbors.begin(), neighbors.end(),
                          [](auto a, auto b) { return a.Vertex < b.Vertex; });
            d.BoundaryVertices = std::count(s.Boundary.begin(), s.Boundary.end(), true);
            s.Barrier = s.Boundary;
            std::vector<std::uint32_t> byFace(mesh.FacesSize(), None);
            for (std::uint32_t i = 0; i < s.SourceFaces.size(); ++i)
                byFace[s.SourceFaces[i]] = i;
            for (auto edge : mesh.LiveEdges())
            {
                if (mesh.IsBoundary(edge))
                    continue;
                auto h = mesh.Halfedge(edge, 0);
                auto a = mesh.Face(h), b = mesh.Face(mesh.Halfedge(edge, 1));
                double angle = std::acos(std::clamp(
                    glm::dot(s.Faces[byFace[a.Index]].Normal, s.Faces[byFace[b.Index]].Normal),
                    -1.0, 1.0));
                if (angle <= params.HardDihedralDegrees * std::numbers::pi / 180)
                    continue;
                s.Barrier[mesh.FromVertex(h).Index] = s.Barrier[mesh.ToVertex(h).Index] = true;
            }
            d.SharpVertices =
                std::count(s.Barrier.begin(), s.Barrier.end(), true) - d.BoundaryVertices;
            return Status::Success;
        }
        Status Neighborhoods(Surface& s, const Params& p, Diagnostics& d)
        {
            const double radius = p.RadiusRatio * p.ScaleFactors.back();
            const auto n = s.Positions.size();
            s.Support.resize(n);
            std::vector<double> distances(n);
            std::vector<std::uint32_t> stamps(n, None);
            using Entry = std::pair<double, std::uint32_t>;
            for (std::uint32_t source = 0; source < n; ++source)
            {
                if (s.Areas[source] == 0 || s.Barrier[source])
                    continue;
                std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
                queue.emplace(0, source);
                distances[source] = 0;
                stamps[source] = source;
                while (!queue.empty())
                {
                    auto [dist, v] = queue.top();
                    queue.pop();
                    if (dist != distances[v])
                        continue;
                    if (s.Support[source].size() >= p.MaximumNeighbors)
                        return Status::WorkLimit;
                    s.Support[source].push_back({v, dist});
                    for (auto edge : s.Adjacent[v])
                    {
                        if (++d.EdgeVisits > p.MaximumWorkItems)
                            return Status::WorkLimit;
                        if (s.Barrier[edge.Vertex])
                            continue;
                        double next = dist + edge.Distance;
                        if (next >= radius)
                            continue;
                        if (stamps[edge.Vertex] != source || next < distances[edge.Vertex])
                        {
                            stamps[edge.Vertex] = source;
                            distances[edge.Vertex] = next;
                            queue.emplace(next, edge.Vertex);
                        }
                    }
                }
                d.NeighborhoodSamples += s.Support[source].size();
                d.PeakNeighborhood = std::max(d.PeakNeighborhood, s.Support[source].size());
            }
            return Status::Success;
        }
        struct Field
        {
            double Max{}, Min{}, Residual{};
            V MaxDirection{}, MinDirection{};
            bool Valid{};
        };
        double Weight(double distance, double radius, double area)
        {
            double t = distance / radius;
            return t >= 1 ? 0 : area * (1 - t * t) * (1 - t * t);
        }
        std::vector<Field> FitShape(const Surface& s, double radius)
        {
            std::vector<Field> result(s.Positions.size());
            for (std::uint32_t v = 0; v < result.size(); ++v)
            {
                if (s.Areas[v] == 0 || s.Barrier[v])
                    continue;
                auto [x, y] = Basis(s.Normals[v]);
                Eigen::Matrix3d matrix = Eigen::Matrix3d::Zero();
                Eigen::Vector3d rhs = Eigen::Vector3d::Zero();
                double total = 0;
                std::size_t samples = 0;
                // Fit S=-dN in a tangent frame. Edge-hinge dyad eigenvectors are not
                // interchangeable with these geometric bending directions.
                for (auto neighbor : s.Support[v])
                {
                    auto j = neighbor.Vertex;
                    if (j == v || s.Barrier[j] || glm::dot(s.Normals[v], s.Normals[j]) < 0.25)
                        continue;
                    double w = Weight(neighbor.Distance, radius, s.Areas[j]);
                    if (w == 0)
                        continue;
                    V delta = (s.Positions[j] - s.Positions[v]) / radius,
                      dn = s.Normals[v] - s.Normals[j];
                    double u = glm::dot(delta, x), t = glm::dot(delta, y);
                    Eigen::Vector3d a{u, t, 0}, b{0, u, t};
                    matrix.noalias() += w * (a * a.transpose() + b * b.transpose());
                    rhs.noalias() += w * (a * glm::dot(dn, x) + b * glm::dot(dn, y));
                    total += w;
                    ++samples;
                }
                if (samples < 5 || total == 0)
                    continue;
                matrix /= total;
                rhs /= total;
                Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> condition(matrix);
                if (condition.info() != Eigen::Success ||
                    condition.eigenvalues()[0] < 1e-6 * condition.eigenvalues()[2])
                    continue;
                Eigen::Vector3d solution = matrix.ldlt().solve(rhs);
                Eigen::Matrix2d shape;
                shape << solution[0], solution[1], solution[1], solution[2];
                Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eig(shape);
                if (eig.info() != Eigen::Success || !solution.allFinite())
                    continue;
                double residual = 0;
                for (auto neighbor : s.Support[v])
                {
                    auto j = neighbor.Vertex;
                    if (j == v || s.Barrier[j] || glm::dot(s.Normals[v], s.Normals[j]) < 0.25)
                        continue;
                    double w = Weight(neighbor.Distance, radius, s.Areas[j]);
                    if (w == 0)
                        continue;
                    V delta = (s.Positions[j] - s.Positions[v]) / radius,
                      dn = s.Normals[v] - s.Normals[j];
                    Eigen::Vector2d error =
                        shape * Eigen::Vector2d{glm::dot(delta, x), glm::dot(delta, y)} -
                        Eigen::Vector2d{glm::dot(dn, x), glm::dot(dn, y)};
                    residual += w * error.squaredNorm();
                }
                auto& f = result[v];
                f.Valid = true;
                f.Min = eig.eigenvalues()[0] / radius;
                f.Max = eig.eigenvalues()[1] / radius;
                f.MinDirection = x * eig.eigenvectors()(0, 0) + y * eig.eigenvectors()(1, 0);
                f.MaxDirection = x * eig.eigenvectors()(0, 1) + y * eig.eigenvectors()(1, 1);
                f.Residual = std::sqrt(residual / total);
            }
            return result;
        }
        struct Signal
        {
            V Direction{};
            double Extremality{}, Value{}, Other{}, Residual{};
            bool Valid{};
        };
        std::vector<Signal> PrincipalSignal(const Surface& s, const std::vector<Field>& field,
                                            double radius, bool ridge, const Params& p)
        {
            std::vector<V> gradients(field.size());
            std::vector<double> areas(field.size());
            for (const auto& f : s.Faces)
            {
                if (!std::all_of(f.Verts.begin(), f.Verts.end(),
                                 [&](auto v) { return field[v].Valid; }))
                    continue;
                std::array<double, 3> values{};
                for (int j = 0; j < 3; ++j)
                    values[j] = ridge ? field[f.Verts[j]].Max : field[f.Verts[j]].Min;
                V g = Gradient(s, f, values);
                for (auto v : f.Verts)
                {
                    gradients[v] += f.Area * g;
                    areas[v] += f.Area;
                }
            }
            std::vector<Signal> signal(field.size());
            for (std::size_t v = 0; v < field.size(); ++v)
            {
                const auto& f = field[v];
                if (!f.Valid || areas[v] == 0)
                    continue;
                const double sum = std::abs(f.Max) + std::abs(f.Min);
                if (sum == 0 || (f.Max - f.Min) / sum < p.MinimumAnisotropy)
                    continue;
                auto& a = signal[v];
                a.Valid = true;
                a.Direction = ridge ? f.MaxDirection : f.MinDirection;
                a.Value = ridge ? f.Max : f.Min;
                a.Other = ridge ? f.Min : f.Max;
                a.Residual = f.Residual;
                a.Extremality = glm::dot(gradients[v] / areas[v], a.Direction) * radius * radius;
            }
            // One physical-radius average of the sign-covariant extremality field.
            // It filters derivatives, never vertex positions, and cannot average
            // opposite arbitrary eigenvector signs into false zero contours.
            auto smoothed = signal;
            for (std::size_t v = 0; v < signal.size(); ++v)
            {
                if (!signal[v].Valid)
                    continue;
                double total = 0, value = 0;
                for (auto neighbor : s.Support[v])
                {
                    const auto& b = signal[neighbor.Vertex];
                    if (!b.Valid)
                        continue;
                    double align = glm::dot(signal[v].Direction, b.Direction);
                    if (std::abs(align) < 0.5)
                        continue;
                    double w = Weight(neighbor.Distance, radius * 0.5, s.Areas[neighbor.Vertex]);
                    total += w;
                    value += w * (align < 0 ? -b.Extremality : b.Extremality);
                }
                if (total > 0)
                    smoothed[v].Extremality = value / total;
            }
            return smoothed;
        }
        std::vector<Signal> MeanSignal(const Surface& s, const std::vector<Field>& field,
                                       double radius, bool ridge, const Params& p)
        {
            std::vector<Signal> signal(field.size());
            for (std::size_t v = 0; v < field.size(); ++v)
            {
                if (!field[v].Valid)
                    continue;
                auto [x, y] = Basis(s.Normals[v]);
                Eigen::Matrix<double, 5, 5> matrix = Eigen::Matrix<double, 5, 5>::Zero();
                Eigen::Matrix<double, 5, 1> rhs = Eigen::Matrix<double, 5, 1>::Zero();
                double total = 0, mean = (field[v].Max + field[v].Min) / 2;
                std::size_t samples = 0;
                for (auto neighbor : s.Support[v])
                {
                    auto j = neighbor.Vertex;
                    if (j == v || !field[j].Valid)
                        continue;
                    double w = Weight(neighbor.Distance, radius, s.Areas[j]);
                    if (w == 0)
                        continue;
                    V delta = (s.Positions[j] - s.Positions[v]) / radius;
                    double u = glm::dot(delta, x), t = glm::dot(delta, y);
                    Eigen::Matrix<double, 5, 1> row;
                    row << u, t, u * u / 2, u * t, t * t / 2;
                    double value = radius * ((field[j].Max + field[j].Min) / 2 - mean);
                    matrix.noalias() += w * row * row.transpose();
                    rhs.noalias() += w * row * value;
                    total += w;
                    ++samples;
                }
                if (samples < 8 || total == 0)
                    continue;
                matrix /= total;
                rhs /= total;
                Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 5, 5>> condition(matrix);
                if (condition.info() != Eigen::Success ||
                    condition.eigenvalues()[0] < 1e-7 * condition.eigenvalues()[4])
                    continue;
                auto solution = matrix.ldlt().solve(rhs).eval();
                if (!solution.allFinite())
                    continue;
                Eigen::Matrix2d hessian;
                hessian << solution[2], solution[3], solution[3], solution[4];
                Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eig(hessian);
                if (eig.info() != Eigen::Success)
                    continue;
                double low = eig.eigenvalues()[0], high = eig.eigenvalues()[1],
                       magnitude = std::max(std::abs(low), std::abs(high));
                if (magnitude < p.MinimumSharpness ||
                    (high - low) / magnitude < p.MinimumAnisotropy)
                    continue;
                int index = ridge ? 0 : 1;
                if (ridge ? low >= -p.MinimumSharpness : high <= p.MinimumSharpness)
                    continue;
                auto& a = signal[v];
                a.Valid = true;
                a.Value = mean;
                a.Residual = field[v].Residual;
                a.Direction = x * eig.eigenvectors()(0, index) + y * eig.eigenvectors()(1, index);
                a.Extremality = solution[0] * eig.eigenvectors()(0, index) +
                                solution[1] * eig.eigenvectors()(1, index);
            }
            return signal;
        }

        using PointMap = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;
        std::uint32_t Intern(Result& result, const Surface& s, PointMap& points, std::uint32_t a,
                             std::uint32_t b, double t)
        {
            if (a > b)
            {
                std::swap(a, b);
                t = 1 - t;
            }
            if (t == 0)
                b = a;
            if (t == 1)
            {
                a = b;
                t = 0;
            }
            auto [it, inserted] =
                points.try_emplace({a, b}, static_cast<std::uint32_t>(result.Points.size()));
            if (inserted)
                result.Points.push_back(
                    {glm::vec3{s.Origin +
                               s.Diagonal * ((1 - t) * s.Positions[a] + t * s.Positions[b])},
                     a, b, t});
            return it->second;
        }
        void Trace(const Surface& s, const std::vector<Signal>& signal, double radius, Kind kind,
                   std::uint8_t scale, const Params& params, Result& result)
        {
            PointMap points;
            std::set<std::pair<std::uint32_t, std::uint32_t>> segments;
            const bool ridge = kind == Kind::PrincipalRidge || kind == Kind::MeanRidge;
            const bool principal = kind == Kind::PrincipalRidge || kind == Kind::PrincipalValley;
            auto& diagnostics = result.Diagnostic.Scales[scale];
            for (std::uint32_t fi = 0; fi < s.Faces.size(); ++fi)
            {
                const auto& f = s.Faces[fi];
                if (!std::all_of(f.Verts.begin(), f.Verts.end(),
                                 [&](auto v) { return signal[v].Valid; }))
                    continue;
                std::array<V, 3> directions;
                std::array<double, 3> e;
                for (int j = 0; j < 3; ++j)
                {
                    const auto& a = signal[f.Verts[j]];
                    double sign = glm::dot(signal[f.Verts[0]].Direction, a.Direction) < 0 ? -1 : 1;
                    directions[j] = sign * a.Direction;
                    e[j] = sign * a.Extremality;
                    if (std::abs(e[j]) < 1e-10)
                        e[j] = 0;
                }
                if (glm::dot(directions[0], directions[1]) <= 0 ||
                    glm::dot(directions[0], directions[2]) <= 0 ||
                    glm::dot(directions[1], directions[2]) <= 0)
                {
                    ++diagnostics.SingularTriangles;
                    continue;
                }
                if (e[0] == 0 && e[1] == 0 && e[2] == 0)
                {
                    ++diagnostics.PlateauTriangles;
                    continue;
                }
                struct Hit
                {
                    std::uint32_t A, B;
                    double T;
                    V Bary;
                };
                std::vector<Hit> hits;
                for (int j = 0; j < 3; ++j)
                {
                    if (e[j] == 0)
                    {
                        V bary{};
                        bary[j] = 1;
                        hits.push_back({f.Verts[j], f.Verts[j], 0, bary});
                    }
                    int k = (j + 1) % 3;
                    if ((e[j] < 0 && e[k] > 0) || (e[j] > 0 && e[k] < 0))
                    {
                        double t = e[j] / (e[j] - e[k]);
                        const double length =
                            glm::length(s.Positions[f.Verts[j]] - s.Positions[f.Verts[k]]);
                        // Coalesce crossings within float publication precision to the same
                        // source vertex on both incident faces. Otherwise orientation-roundoff
                        // can create zero-length edges and disconnected duplicate endpoints.
                        constexpr double tolerance = 4.0 * std::numeric_limits<float>::epsilon();
                        V bary{};
                        if (t * length <= tolerance)
                        {
                            bary[j] = 1;
                            hits.push_back({f.Verts[j], f.Verts[j], 0, bary});
                        }
                        else if ((1 - t) * length <= tolerance)
                        {
                            bary[k] = 1;
                            hits.push_back({f.Verts[k], f.Verts[k], 0, bary});
                        }
                        else
                        {
                            bary[j] = 1 - t;
                            bary[k] = t;
                            hits.push_back({f.Verts[j], f.Verts[k], t, bary});
                        }
                    }
                }
                for (std::size_t i = 0; i < hits.size(); ++i)
                    if (hits[i].A == hits[i].B)
                        for (std::size_t j = hits.size(); j-- > i + 1;)
                            if (hits[j].A == hits[i].A && hits[j].B == hits[i].B)
                                hits.erase(hits.begin() + static_cast<std::ptrdiff_t>(j));
                if (hits.size() != 2)
                    continue;
                V bary = (hits[0].Bary + hits[1].Bary) / 2.0, direction{};
                double value = 0, other = 0, residual = 0;
                for (int j = 0; j < 3; ++j)
                {
                    direction += bary[j] * directions[j];
                    value += bary[j] * signal[f.Verts[j]].Value;
                    other += bary[j] * signal[f.Verts[j]].Other;
                    residual += bary[j] * signal[f.Verts[j]].Residual;
                }
                double norm = glm::length(direction);
                if (norm < 0.1)
                    continue;
                direction /= norm;
                double sharpness = glm::dot(Gradient(s, f, e), direction) * radius;
                if (ridge ? value <= 0 || sharpness >= -params.MinimumSharpness
                          : value >= 0 || sharpness <= params.MinimumSharpness)
                    continue;
                double strength = std::abs(value) * radius;
                if (strength < params.MinimumStrength ||
                    (principal && std::abs(value) <= std::abs(other)))
                    continue;
                const auto position = [&](const Hit& h)
                {
                    return glm::vec3{s.Origin + s.Diagonal * ((1 - h.T) * s.Positions[h.A] +
                                                              h.T * s.Positions[h.B])};
                };
                if (position(hits[0]) == position(hits[1]))
                    continue;
                auto a = Intern(result, s, points, hits[0].A, hits[0].B, hits[0].T);
                auto b = Intern(result, s, points, hits[1].A, hits[1].B, hits[1].T);
                if (a > b)
                    std::swap(a, b);
                if (a == b || !segments.emplace(a, b).second)
                    continue;
                double confidence = (strength / (strength + params.MinimumStrength + 0.01)) *
                                    (std::abs(sharpness) /
                                     (std::abs(sharpness) + 10 * params.MinimumSharpness + 0.001)) /
                                    (1 + residual / 0.05);
                result.Segments.push_back({a, b, s.SourceFaces[fi], 0, kind, scale,
                                           static_cast<std::uint8_t>(1u << scale), strength,
                                           std::abs(sharpness), confidence, residual});
                ++diagnostics.SegmentCounts[static_cast<unsigned>(kind)];
            }
        }
        void SharpEdges(const HalfedgeMesh::Mesh& mesh, const Surface& s, const Params& p,
                        Result& result)
        {
            PointMap points;
            std::vector<std::uint32_t> faceIndex(mesh.FacesSize(), None);
            for (std::uint32_t i = 0; i < s.SourceFaces.size(); ++i)
                faceIndex[s.SourceFaces[i]] = i;
            for (auto edge : mesh.LiveEdges())
            {
                if (mesh.IsBoundary(edge))
                    continue;
                auto h = mesh.Halfedge(edge, 0);
                auto a = mesh.Face(h), b = mesh.Face(mesh.Halfedge(edge, 1));
                if (!a.IsValid() || !b.IsValid())
                    continue;
                double angle = std::acos(std::clamp(glm::dot(s.Faces[faceIndex[a.Index]].Normal,
                                                             s.Faces[faceIndex[b.Index]].Normal),
                                                    -1.0, 1.0));
                if (angle <= p.HardDihedralDegrees * std::numbers::pi / 180)
                    continue;
                auto va = mesh.FromVertex(h).Index, vb = mesh.ToVertex(h).Index;
                result.Segments.push_back({Intern(result, s, points, va, va, 0),
                                           Intern(result, s, points, vb, vb, 0), a.Index, 0,
                                           Kind::SharpEdge, 3, 7, angle, 0, 1, 0});
            }
        }
        void ConnectCurves(Result& result)
        {
            std::vector<std::uint32_t> parent(result.Points.size()), degree(parent.size());
            std::iota(parent.begin(), parent.end(), 0);
            auto root = [&](std::uint32_t v)
            {
                while (parent[v] != v)
                {
                    parent[v] = parent[parent[v]];
                    v = parent[v];
                }
                return v;
            };
            for (const auto& s : result.Segments)
            {
                auto a = root(s.PointA), b = root(s.PointB);
                if (a != b)
                    parent[std::max(a, b)] = std::min(a, b);
                ++degree[s.PointA];
                ++degree[s.PointB];
            }
            std::map<std::uint32_t, std::uint32_t> curves;
            for (auto& s : result.Segments)
            {
                auto [it, inserted] = curves.try_emplace(
                    root(s.PointA), static_cast<std::uint32_t>(result.Curves.size()));
                if (inserted)
                    result.Curves.push_back({s.Signal, s.Scale, 0, 0, 0, 0});
                s.Curve = it->second;
                auto& c = result.Curves[s.Curve];
                ++c.SegmentCount;
                c.Length += glm::length(V{result.Points[s.PointA].Position} -
                                        V{result.Points[s.PointB].Position});
            }
            for (std::uint32_t v = 0; v < degree.size(); ++v)
            {
                if (!degree[v])
                    continue;
                auto& c = result.Curves[curves.at(root(v))];
                c.Endpoints += degree[v] == 1;
                c.Junctions += degree[v] > 2;
            }
        }
        double SegmentDistance(V point, V a, V b)
        {
            V delta = b - a;
            double length = glm::dot(delta, delta);
            double t = length > 0 ? std::clamp(glm::dot(point - a, delta) / length, 0.0, 1.0) : 0;
            return glm::length(point - (a + t * delta));
        }
        Status MatchScales(const Surface& s, const Params& p, Result& result)
        {
            std::uint32_t faceSlots = 0;
            for (auto f : s.SourceFaces)
                faceSlots = std::max(faceSlots, f + 1);
            std::array<std::vector<std::uint32_t>, 12> byFace;
            for (auto& array : byFace)
                array.resize(faceSlots, None);
            for (std::uint32_t i = 0; i < result.Segments.size(); ++i)
            {
                const auto& seg = result.Segments[i];
                if (seg.Signal != Kind::SharpEdge)
                    byFace[static_cast<unsigned>(seg.Signal) * 3 + seg.Scale][seg.Face] = i;
            }
            std::vector<std::uint32_t> localFace(faceSlots, None);
            for (std::uint32_t i = 0; i < s.SourceFaces.size(); ++i)
                localFace[s.SourceFaces[i]] = i;
            for (auto& seg : result.Segments)
            {
                if (seg.Signal == Kind::SharpEdge)
                    continue;
                V a = (V{result.Points[seg.PointA].Position} - s.Origin) / s.Diagonal;
                V b = (V{result.Points[seg.PointB].Position} - s.Origin) / s.Diagonal;
                if (glm::length(b - a) == 0)
                    continue;
                V tangent = glm::normalize(b - a), mid = (a + b) / 2.0;
                const auto& face = s.Faces[localFace[seg.Face]];
                for (std::uint8_t scale = 0; scale < 3; ++scale)
                {
                    if (scale == seg.Scale)
                        continue;
                    double tolerance = 0.5 * p.RadiusRatio *
                                       std::min(p.ScaleFactors[scale], p.ScaleFactors[seg.Scale]);
                    bool matched = false;
                    // Search connected surface neighborhoods, not ambient nearest
                    // neighbors that can jump to the opposite side of a thin part.
                    for (auto vertex : face.Verts)
                    {
                        double reach = tolerance + glm::length(mid - s.Positions[vertex]);
                        for (auto neighbor : s.Support[vertex])
                        {
                            if (neighbor.Distance > reach)
                                break;
                            for (auto fi : s.IncidentFaces[neighbor.Vertex])
                            {
                                if (++result.Diagnostic.MatchCandidateVisits +
                                        result.Diagnostic.EdgeVisits >
                                    p.MaximumWorkItems)
                                    return Status::WorkLimit;
                                auto index = byFace[static_cast<unsigned>(seg.Signal) * 3 + scale]
                                                   [s.SourceFaces[fi]];
                                if (index == None)
                                    continue;
                                const auto& other = result.Segments[index];
                                V c = (V{result.Points[other.PointA].Position} - s.Origin) /
                                      s.Diagonal;
                                V d = (V{result.Points[other.PointB].Position} - s.Origin) /
                                      s.Diagonal;
                                if (glm::length(d - c) == 0)
                                    continue;
                                if (SegmentDistance(mid, c, d) <= tolerance &&
                                    std::abs(glm::dot(tangent, glm::normalize(d - c))) >= 0.7 &&
                                    glm::dot(face.Normal, s.Faces[fi].Normal) > 0.5)
                                {
                                    matched = true;
                                    break;
                                }
                            }
                            if (matched)
                                break;
                        }
                        if (matched)
                            break;
                    }
                    if (matched)
                        seg.PersistentScaleMask |= static_cast<std::uint8_t>(1u << scale);
                }
            }
            return Status::Success;
        }
    } // namespace
    const char* ToString(Status status) noexcept
    {
        switch (status)
        {
        case Status::Success:
            return "success";
        case Status::EmptyMesh:
            return "empty_mesh";
        case Status::InvalidParameters:
            return "invalid_parameters";
        case Status::UnsupportedMesh:
            return "unsupported_mesh";
        case Status::InvalidGeometry:
            return "invalid_geometry";
        case Status::WorkLimit:
            return "work_limit";
        }
        return "unknown";
    }
    const char* ToString(Kind kind) noexcept
    {
        switch (kind)
        {
        case Kind::PrincipalRidge:
            return "principal_ridge";
        case Kind::PrincipalValley:
            return "principal_valley";
        case Kind::MeanRidge:
            return "mean_ridge";
        case Kind::MeanValley:
            return "mean_valley";
        case Kind::SharpEdge:
            return "sharp_edge";
        }
        return "unknown";
    }
    Result Extract(const HalfedgeMesh::Mesh& mesh, const Params& params)
    {
        const auto start = std::chrono::steady_clock::now();
        Result result;
        auto finish = [&](Status state)
        {
            result.Diagnostic.State = state;
            result.Diagnostic.TotalMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (state != Status::Success)
            {
                result.Points.clear();
                result.Segments.clear();
                result.Curves.clear();
            }
            return std::move(result);
        };
        auto finiteNonnegative = [](double v) { return std::isfinite(v) && v >= 0; };
        if (!std::isfinite(params.RadiusRatio) || params.RadiusRatio <= 0 ||
            !finiteNonnegative(params.MinimumStrength) ||
            !finiteNonnegative(params.MinimumSharpness) ||
            !finiteNonnegative(params.MinimumAnisotropy) || params.MinimumAnisotropy > 1 ||
            !finiteNonnegative(params.HardDihedralDegrees) || params.HardDihedralDegrees > 180 ||
            params.MaximumNeighbors < 9 || !params.MaximumWorkItems)
            return finish(Status::InvalidParameters);
        double previous = 0;
        for (double factor : params.ScaleFactors)
        {
            if (!std::isfinite(factor) || factor <= previous || params.RadiusRatio * factor > 1)
                return finish(Status::InvalidParameters);
            previous = factor;
        }
        auto elapsed = [](auto t)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t)
                .count();
        };
        Surface surface;
        auto state = Assemble(mesh, surface, result.Diagnostic, params);
        if (state != Status::Success)
            return finish(state);
        auto phaseStart = std::chrono::steady_clock::now();
        state = Neighborhoods(surface, params, result.Diagnostic);
        result.Diagnostic.NeighborhoodMilliseconds = elapsed(phaseStart);
        if (state != Status::Success)
            return finish(state);
        for (std::uint8_t scale = 0; scale < 3; ++scale)
        {
            const double radius = params.RadiusRatio * params.ScaleFactors[scale];
            result.Diagnostic.Scales[scale].Radius = radius * surface.Diagonal;
            phaseStart = std::chrono::steady_clock::now();
            auto field = FitShape(surface, radius);
            result.Diagnostic.FieldMilliseconds += elapsed(phaseStart);
            result.Diagnostic.Scales[scale].SupportedVertices =
                std::count_if(field.begin(), field.end(), [](const auto& f) { return f.Valid; });
            for (unsigned kind = 0; kind < 4; ++kind)
            {
                phaseStart = std::chrono::steady_clock::now();
                auto signal = kind < 2 ? PrincipalSignal(surface, field, radius, kind == 0, params)
                                       : MeanSignal(surface, field, radius, kind == 2, params);
                result.Diagnostic.FieldMilliseconds += elapsed(phaseStart);
                phaseStart = std::chrono::steady_clock::now();
                Trace(surface, signal, radius, static_cast<Kind>(kind), scale, params, result);
                result.Diagnostic.TraceMilliseconds += elapsed(phaseStart);
            }
        }
        SharpEdges(mesh, surface, params, result);
        ConnectCurves(result);
        phaseStart = std::chrono::steady_clock::now();
        state = MatchScales(surface, params, result);
        result.Diagnostic.MatchMilliseconds = elapsed(phaseStart);
        return finish(state);
    }
} // namespace Geometry::CurvatureExtrema
