module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.ImplicitPlaneField;

import Geometry.Plane;
import Geometry.Validation;

namespace Geometry::Implicit
{
    namespace
    {
        static constexpr std::string_view kClosestPointProperty = "i:closest_point";
        static constexpr std::string_view kNormalProperty = "i:normal";
        static constexpr std::string_view kSignedDistanceProperty = "i:signed_distance";
        static constexpr std::string_view kMaxPlaneErrorProperty = "i:max_plane_error";
        static constexpr std::string_view kSupportRadiusProperty = "i:support_radius";
        static constexpr std::string_view kFlagsProperty = "i:flags";
        static constexpr std::string_view kSourcePrimitiveProperty = "i:source_primitive";

        static constexpr float kEpsilon = 1.0e-6f;

        struct TrianglePrimitive
        {
            glm::vec3 A;
            glm::vec3 B;
            glm::vec3 C;
            std::uint32_t PrimitiveIndex{std::numeric_limits<std::uint32_t>::max()};
        };

        struct ClosestPointSample
        {
            glm::vec3 QueryPoint{0.0f};
            glm::vec3 ClosestPoint{0.0f};
            glm::vec3 Normal{0.0f, 1.0f, 0.0f};
            float SignedDistance{0.0f};
            float UnsignedDistance{0.0f};
            std::uint32_t PrimitiveIndex{std::numeric_limits<std::uint32_t>::max()};
            bool Valid{false};
        };

        [[nodiscard]] bool NormalizeSafe(glm::vec3& v)
        {
            const float len2 = glm::dot(v, v);
            if (!std::isfinite(len2) || len2 <= (kEpsilon * kEpsilon))
                return false;
            v *= 1.0f / std::sqrt(len2);
            return true;
        }

        [[nodiscard]] glm::vec3 TriangleNormal(
            const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c)
        {
            return glm::cross(b - a, c - a);
        }

        [[nodiscard]] glm::vec3 ClosestPointOnTriangle(
            const glm::vec3& p,
            const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c)
        {
            // Christer Ericson, Real-Time Collision Detection.
            const glm::vec3 ab = b - a;
            const glm::vec3 ac = c - a;
            const glm::vec3 ap = p - a;

            const float d1 = glm::dot(ab, ap);
            const float d2 = glm::dot(ac, ap);
            if (d1 <= 0.0f && d2 <= 0.0f) return a;

            const glm::vec3 bp = p - b;
            const float d3 = glm::dot(ab, bp);
            const float d4 = glm::dot(ac, bp);
            if (d3 >= 0.0f && d4 <= d3) return b;

            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            {
                const float v = d1 / (d1 - d3);
                return a + v * ab;
            }

            const glm::vec3 cp = p - c;
            const float d5 = glm::dot(ab, cp);
            const float d6 = glm::dot(ac, cp);
            if (d6 >= 0.0f && d5 <= d6) return c;

            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            {
                const float w = d2 / (d2 - d6);
                return a + w * ac;
            }

            const float va = d3 * d6 - d5 * d4;
            if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            {
                const glm::vec3 bc = c - b;
                const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return b + w * bc;
            }

            const float denom = 1.0f / (va + vb + vc);
            const float v = vb * denom;
            const float w = vc * denom;
            return a + ab * v + ac * w;
        }

        struct MeshClosestPointOracle
        {
            explicit MeshClosestPointOracle(const Halfedge::Mesh& mesh)
            {
                BuildTriangles(mesh);
            }

            [[nodiscard]] bool Empty() const noexcept
            {
                return m_Triangles.empty();
            }

            [[nodiscard]] ClosestPointSample Query(const glm::vec3& queryPoint) const
            {
                ClosestPointSample out;
                out.QueryPoint = queryPoint;
                if (m_Triangles.empty())
                    return out;

                float bestDist2 = std::numeric_limits<float>::max();
                for (const TrianglePrimitive& tri : m_Triangles)
                {
                    const glm::vec3 cp = ClosestPointOnTriangle(queryPoint, tri.A, tri.B, tri.C);
                    const glm::vec3 diff = queryPoint - cp;
                    const float dist2 = glm::dot(diff, diff);
                    if (dist2 >= bestDist2)
                        continue;

                    glm::vec3 n = TriangleNormal(tri.A, tri.B, tri.C);
                    if (!NormalizeSafe(n))
                        continue;

                    bestDist2 = dist2;
                    out.ClosestPoint = cp;
                    out.Normal = n;
                    out.UnsignedDistance = std::sqrt(std::max(0.0f, dist2));
                    out.SignedDistance = glm::dot(queryPoint - cp, n);
                    out.PrimitiveIndex = tri.PrimitiveIndex;
                    out.Valid = true;
                }

                return out;
            }

        private:
            void BuildTriangles(const Halfedge::Mesh& mesh)
            {
                for (std::size_t faceIndex = 0; faceIndex < mesh.FacesSize(); ++faceIndex)
                {
                    const FaceHandle f{static_cast<PropertyIndex>(faceIndex)};
                    if (!mesh.IsValid(f) || mesh.IsDeleted(f))
                        continue;

                    std::vector<VertexHandle> faceVertices;
                    for (const VertexHandle v : mesh.VerticesAroundFace(f))
                    {
                        if (mesh.IsValid(v) && !mesh.IsDeleted(v))
                            faceVertices.push_back(v);
                    }

                    if (faceVertices.size() < 3u)
                        continue;

                    const glm::vec3 p0 = mesh.Position(faceVertices[0]);
                    for (std::size_t i = 1; i + 1 < faceVertices.size(); ++i)
                    {
                        const glm::vec3 p1 = mesh.Position(faceVertices[i]);
                        const glm::vec3 p2 = mesh.Position(faceVertices[i + 1u]);
                        if (!Validation::IsFinite(p0) || !Validation::IsFinite(p1) || !Validation::IsFinite(p2))
                            continue;

                        m_Triangles.push_back(TrianglePrimitive{
                            .A = p0,
                            .B = p1,
                            .C = p2,
                            .PrimitiveIndex = static_cast<std::uint32_t>(m_Triangles.size())
                        });
                    }
                }
            }

            std::vector<TrianglePrimitive> m_Triangles;
        };

        [[nodiscard]] float DegreesToRadians(float degrees)
        {
            return degrees * 0.01745329251994329577f;
        }

        [[nodiscard]] float CosDegrees(float degrees)
        {
            return std::cos(DegreesToRadians(degrees));
        }

        [[nodiscard]] float NodeDiagonal(const AABB& aabb)
        {
            return glm::length(aabb.Max - aabb.Min);
        }

        [[nodiscard]] float NodeHalfDiagonal(const AABB& aabb)
        {
            return 0.5f * NodeDiagonal(aabb);
        }

        [[nodiscard]] bool PointInExpandedAabb(const AABB& aabb, const glm::vec3& p, float expand)
        {
            const glm::vec3 e(expand);
            return p.x >= (aabb.Min.x - e.x) && p.y >= (aabb.Min.y - e.y) && p.z >= (aabb.Min.z - e.z)
                && p.x <= (aabb.Max.x + e.x) && p.y <= (aabb.Max.y + e.y) && p.z <= (aabb.Max.z + e.z);
        }

        [[nodiscard]] Plane MakePlane(const glm::vec3& point, const glm::vec3& normal)
        {
            Plane plane;
            plane.Normal = normal;
            plane.Distance = -glm::dot(normal, point);
            plane.Normalize();
            return plane;
        }

        void CreateNode(Octree& tree, const AABB& aabb)
        {
            tree.m_Nodes.push_back(Octree::Node{});
            tree.m_Nodes.back().Aabb = aabb;
            tree.NodeProperties.PushBack();
        }

        [[nodiscard]] AABB ComputeMeshBounds(const Halfedge::Mesh& mesh)
        {
            AABB bounds{};
            bool found = false;
            for (std::size_t vertexIndex = 0; vertexIndex < mesh.VerticesSize(); ++vertexIndex)
            {
                const VertexHandle v{static_cast<PropertyIndex>(vertexIndex)};
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;
                const glm::vec3 p = mesh.Position(v);
                if (!Validation::IsFinite(p))
                    continue;

                if (!found)
                {
                    bounds.Min = p;
                    bounds.Max = p;
                    found = true;
                }
                else
                {
                    bounds.Min = glm::min(bounds.Min, p);
                    bounds.Max = glm::max(bounds.Max, p);
                }
            }
            return bounds;
        }

        [[nodiscard]] std::array<glm::vec3, 15> ProbePoints(const AABB& aabb)
        {
            const glm::vec3 c = aabb.GetCenter();
            return {
                c,
                glm::vec3{aabb.Min.x, aabb.Min.y, aabb.Min.z},
                glm::vec3{aabb.Max.x, aabb.Min.y, aabb.Min.z},
                glm::vec3{aabb.Min.x, aabb.Max.y, aabb.Min.z},
                glm::vec3{aabb.Max.x, aabb.Max.y, aabb.Min.z},
                glm::vec3{aabb.Min.x, aabb.Min.y, aabb.Max.z},
                glm::vec3{aabb.Max.x, aabb.Min.y, aabb.Max.z},
                glm::vec3{aabb.Min.x, aabb.Max.y, aabb.Max.z},
                glm::vec3{aabb.Max.x, aabb.Max.y, aabb.Max.z},
                glm::vec3{c.x, c.y, aabb.Min.z},
                glm::vec3{c.x, c.y, aabb.Max.z},
                glm::vec3{c.x, aabb.Min.y, c.z},
                glm::vec3{c.x, aabb.Max.y, c.z},
                glm::vec3{aabb.Min.x, c.y, c.z},
                glm::vec3{aabb.Max.x, c.y, c.z}
            };
        }

        struct BuildContext
        {
            MeshClosestPointOracle Oracle;
            BuildParams Params;
            BuildStats* Stats{nullptr};

            NodeProperty<glm::vec3> ClosestPoint;
            NodeProperty<glm::vec3> Normal;
            NodeProperty<float> SignedDistance;
            NodeProperty<float> MaxPlaneError;
            NodeProperty<float> SupportRadius;
            NodeProperty<std::uint32_t> Flags;
            NodeProperty<std::uint32_t> SourcePrimitive;

            explicit BuildContext(const Halfedge::Mesh& mesh, const BuildParams& params, BuildStats& stats, PlaneField& field)
                : Oracle(mesh), Params(params), Stats(&stats),
                  ClosestPoint(field.Hierarchy().GetOrAddNodeProperty<glm::vec3>(std::string(kClosestPointProperty), glm::vec3(0.0f))),
                  Normal(field.Hierarchy().GetOrAddNodeProperty<glm::vec3>(std::string(kNormalProperty), glm::vec3(0.0f, 1.0f, 0.0f))),
                  SignedDistance(field.Hierarchy().GetOrAddNodeProperty<float>(std::string(kSignedDistanceProperty), 0.0f)),
                  MaxPlaneError(field.Hierarchy().GetOrAddNodeProperty<float>(std::string(kMaxPlaneErrorProperty), 0.0f)),
                  SupportRadius(field.Hierarchy().GetOrAddNodeProperty<float>(std::string(kSupportRadiusProperty), 0.0f)),
                  Flags(field.Hierarchy().GetOrAddNodeProperty<std::uint32_t>(std::string(kFlagsProperty), 0u)),
                  SourcePrimitive(field.Hierarchy().GetOrAddNodeProperty<std::uint32_t>(std::string(kSourcePrimitiveProperty), std::numeric_limits<std::uint32_t>::max()))
            {}
        };

        void BuildNodeRecursive(BuildContext& ctx, PlaneField& field, Octree::NodeIndex nodeIndex, std::size_t depth)
        {
            auto& tree = field.Hierarchy();
            ctx.Stats->MaxReachedDepth = std::max(ctx.Stats->MaxReachedDepth, depth);

            const AABB nodeAabb = tree.m_Nodes[nodeIndex].Aabb;
            const glm::vec3 center = nodeAabb.GetCenter();
            const ClosestPointSample centerSample = ctx.Oracle.Query(center);
            const float halfDiag = NodeHalfDiagonal(nodeAabb);
            const float supportRadius = std::max(kEpsilon, ctx.Params.SupportRadiusFactor * halfDiag);

            const NodeHandle h{nodeIndex};
            ctx.SupportRadius[h] = supportRadius;
            ctx.Flags[h] = 0u;
            ctx.MaxPlaneError[h] = 0.0f;
            ctx.SourcePrimitive[h] = centerSample.PrimitiveIndex;

            if (!centerSample.Valid)
            {
                tree.m_Nodes[nodeIndex].IsLeaf = true;
                return;
            }

            ctx.ClosestPoint[h] = centerSample.ClosestPoint;
            ctx.Normal[h] = centerSample.Normal;
            ctx.SignedDistance[h] = centerSample.SignedDistance;

            const float narrowBand = std::max(kEpsilon, ctx.Params.NarrowBandFactor * halfDiag);
            if (std::abs(centerSample.SignedDistance) > narrowBand)
            {
                tree.m_Nodes[nodeIndex].IsLeaf = true;
                return;
            }

            const Plane representativePlane = MakePlane(centerSample.ClosestPoint, centerSample.Normal);
            const float maxAllowedPlaneError = std::max(kEpsilon, ctx.Params.MaxPlaneErrorFraction * NodeDiagonal(nodeAabb));
            const float normalCompatibilityCos = CosDegrees(ctx.Params.MaxNormalDeviationDegrees);
            const float ambiguityCos = CosDegrees(std::max(ctx.Params.MaxNormalDeviationDegrees * 2.0f, 45.0f));

            float maxPlaneError = 0.0f;
            float minNormalDot = 1.0f;
            bool ambiguous = false;

            for (const glm::vec3& probePoint : ProbePoints(nodeAabb))
            {
                const ClosestPointSample probe = ctx.Oracle.Query(probePoint);
                if (!probe.Valid)
                    continue;

                const float planeValue = static_cast<float>(Geometry::SignedDistance(representativePlane, probePoint));
                const float error = std::abs(planeValue - probe.SignedDistance);
                maxPlaneError = std::max(maxPlaneError, error);

                const float ndot = glm::dot(centerSample.Normal, probe.Normal);
                minNormalDot = std::min(minNormalDot, ndot);

                if (ctx.Params.DetectAmbiguity && ndot < ambiguityCos && probe.UnsignedDistance <= narrowBand)
                    ambiguous = true;
            }

            ctx.MaxPlaneError[h] = maxPlaneError;

            const bool qualityAccept = (maxPlaneError <= maxAllowedPlaneError) && (minNormalDot >= normalCompatibilityCos);
            const bool canSubdivide = depth < ctx.Params.MaxDepth;
            const bool forceSplit = depth < ctx.Params.MinDepth;

            if ((!forceSplit && qualityAccept) || !canSubdivide)
            {
                std::uint32_t flags = static_cast<std::uint32_t>(PlaneFieldNodeFlags::Active);
                if (ambiguous)
                    flags |= static_cast<std::uint32_t>(PlaneFieldNodeFlags::Ambiguous);
                if (minNormalDot < ambiguityCos)
                    flags |= static_cast<std::uint32_t>(PlaneFieldNodeFlags::SharpFeatureCandidate);

                ctx.Flags[h] = flags;
                tree.m_Nodes[nodeIndex].IsLeaf = true;
                ++ctx.Stats->ActiveLeafCount;
                if (ambiguous)
                    ++ctx.Stats->AmbiguousLeafCount;
                return;
            }

            // Save base child index before creating children — push_back may
            // reallocate m_Nodes, invalidating any prior references/pointers.
            const auto baseChild = static_cast<Octree::NodeIndex>(tree.m_Nodes.size());
            tree.m_Nodes[nodeIndex].IsLeaf = false;
            tree.m_Nodes[nodeIndex].ChildMask = 0xFFu;
            tree.m_Nodes[nodeIndex].BaseChildIndex = baseChild;

            const glm::vec3 split = nodeAabb.GetCenter();
            for (int child = 0; child < 8; ++child)
            {
                glm::vec3 childMin{
                    (child & 1) ? split.x : nodeAabb.Min.x,
                    (child & 2) ? split.y : nodeAabb.Min.y,
                    (child & 4) ? split.z : nodeAabb.Min.z
                };
                glm::vec3 childMax{
                    (child & 1) ? nodeAabb.Max.x : split.x,
                    (child & 2) ? nodeAabb.Max.y : split.y,
                    (child & 4) ? nodeAabb.Max.z : split.z
                };
                CreateNode(tree, AABB{.Min = childMin, .Max = childMax});
            }

            for (int child = 0; child < 8; ++child)
                BuildNodeRecursive(ctx, field, baseChild + child, depth + 1u);
        }

        [[nodiscard]] std::optional<NodeProperty<glm::vec3>> ClosestPointProperty(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<glm::vec3>(std::string(kClosestPointProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        [[nodiscard]] std::optional<NodeProperty<glm::vec3>> NormalProperty(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<glm::vec3>(std::string(kNormalProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        [[nodiscard]] std::optional<NodeProperty<float>> SignedDistanceProperty(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<float>(std::string(kSignedDistanceProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        [[nodiscard]] std::optional<NodeProperty<float>> MaxPlaneErrorPropertyHandle(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<float>(std::string(kMaxPlaneErrorProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        [[nodiscard]] std::optional<NodeProperty<float>> SupportRadiusPropertyHandle(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<float>(std::string(kSupportRadiusProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        [[nodiscard]] std::optional<NodeProperty<std::uint32_t>> FlagsProperty(const PlaneField& field)
        {
            auto prop = field.Hierarchy().GetNodeProperty<std::uint32_t>(std::string(kFlagsProperty));
            if (!prop) return std::nullopt;
            return prop;
        }

        struct FieldEvalSample
        {
            float Value{0.0f};
            glm::vec3 Normal{0.0f, 1.0f, 0.0f};
            bool Valid{false};
        };

        [[nodiscard]] FieldEvalSample EvaluateWithNormal(const PlaneField& field, const glm::vec3& worldPoint)
        {
            FieldEvalSample out;
            const auto influencingLeaves = field.CollectInfluencingLeaves(worldPoint);
            if (influencingLeaves.empty())
                return out;

            const auto cpProp = ClosestPointProperty(field);
            const auto nProp = NormalProperty(field);
            const auto srProp = SupportRadiusPropertyHandle(field);
            if (!cpProp || !nProp || !srProp)
                return out;

            glm::vec3 bestNormal = (*nProp)[NodeHandle{influencingLeaves.front()}];
            float bestAbsValue = std::numeric_limits<float>::max();
            for (const auto nodeIndex : influencingLeaves)
            {
                const NodeHandle h{nodeIndex};
                const float localValue = glm::dot((*nProp)[h], worldPoint - (*cpProp)[h]);
                const float absValue = std::abs(localValue);
                if (absValue < bestAbsValue)
                {
                    bestAbsValue = absValue;
                    bestNormal = (*nProp)[h];
                }
            }

            float sumW = 0.0f;
            float sumF = 0.0f;
            glm::vec3 sumN(0.0f);
            for (const auto nodeIndex : influencingLeaves)
            {
                const NodeHandle h{nodeIndex};
                const glm::vec3 n = (*nProp)[h];
                if (glm::dot(n, bestNormal) < field.BlendNormalCompatibilityDot())
                    continue;

                const glm::vec3 p = (*cpProp)[h];
                const float radius = std::max(kEpsilon, (*srProp)[h]);
                const float planeValue = glm::dot(n, worldPoint - p);
                const float centerDistance = glm::length(worldPoint - field.Hierarchy().m_Nodes[nodeIndex].Aabb.GetCenter());
                const float weight = 1.0f / std::max(kEpsilon, centerDistance / radius + std::abs(planeValue));

                sumW += weight;
                sumF += weight * planeValue;
                sumN += weight * n;
            }

            if (sumW <= kEpsilon)
                return out;

            out.Value = sumF / sumW;
            if (glm::dot(sumN, sumN) > (kEpsilon * kEpsilon))
                out.Normal = glm::normalize(sumN);
            else
                out.Normal = bestNormal;
            out.Valid = true;
            return out;
        }
    }

    bool PlaneField::IsActive(const NodeIndex nodeIndex) const
    {
        const auto flags = FlagsProperty(*this);
        if (!flags || nodeIndex >= Hierarchy().m_Nodes.size())
            return false;
        return HasAnyFlag(static_cast<PlaneFieldNodeFlags>((*flags)[NodeHandle{nodeIndex}]), PlaneFieldNodeFlags::Active);
    }

    bool PlaneField::IsAmbiguous(const NodeIndex nodeIndex) const
    {
        const auto flags = FlagsProperty(*this);
        if (!flags || nodeIndex >= Hierarchy().m_Nodes.size())
            return false;
        return HasAnyFlag(static_cast<PlaneFieldNodeFlags>((*flags)[NodeHandle{nodeIndex}]), PlaneFieldNodeFlags::Ambiguous);
    }

    std::optional<glm::vec3> PlaneField::ClosestPoint(const NodeIndex nodeIndex) const
    {
        const auto prop = ClosestPointProperty(*this);
        if (!prop || nodeIndex >= Hierarchy().m_Nodes.size())
            return std::nullopt;
        return (*prop)[NodeHandle{nodeIndex}];
    }

    std::optional<glm::vec3> PlaneField::Normal(const NodeIndex nodeIndex) const
    {
        const auto prop = NormalProperty(*this);
        if (!prop || nodeIndex >= Hierarchy().m_Nodes.size())
            return std::nullopt;
        return (*prop)[NodeHandle{nodeIndex}];
    }

    std::optional<float> PlaneField::SignedDistance(const NodeIndex nodeIndex) const
    {
        const auto prop = SignedDistanceProperty(*this);
        if (!prop || nodeIndex >= Hierarchy().m_Nodes.size())
            return std::nullopt;
        return (*prop)[NodeHandle{nodeIndex}];
    }

    std::optional<float> PlaneField::MaxPlaneError(const NodeIndex nodeIndex) const
    {
        const auto prop = MaxPlaneErrorPropertyHandle(*this);
        if (!prop || nodeIndex >= Hierarchy().m_Nodes.size())
            return std::nullopt;
        return (*prop)[NodeHandle{nodeIndex}];
    }

    std::optional<float> PlaneField::SupportRadius(const NodeIndex nodeIndex) const
    {
        const auto prop = SupportRadiusPropertyHandle(*this);
        if (!prop || nodeIndex >= Hierarchy().m_Nodes.size())
            return std::nullopt;
        return (*prop)[NodeHandle{nodeIndex}];
    }

    std::vector<PlaneField::NodeIndex> PlaneField::CollectInfluencingLeaves(const glm::vec3& worldPoint) const
    {
        std::vector<NodeIndex> out;
        if (Hierarchy().m_Nodes.empty())
            return out;

        const auto flags = FlagsProperty(*this);
        const auto support = SupportRadiusPropertyHandle(*this);
        if (!flags || !support)
            return out;

        static constexpr std::size_t kMaxStack = 256;
        std::array<NodeIndex, kMaxStack> stack{};
        std::size_t stackSize = 0;
        stack[stackSize++] = 0u;

        while (stackSize > 0u)
        {
            const NodeIndex nodeIndex = stack[--stackSize];
            const auto& node = Hierarchy().m_Nodes[nodeIndex];
            const float nodeSupport = (*support)[NodeHandle{nodeIndex}];

            if (!PointInExpandedAabb(node.Aabb, worldPoint, nodeSupport))
                continue;

            if (node.IsLeaf)
            {
                if (HasAnyFlag(static_cast<PlaneFieldNodeFlags>((*flags)[NodeHandle{nodeIndex}]), PlaneFieldNodeFlags::Active))
                    out.push_back(nodeIndex);
                continue;
            }

            if (node.BaseChildIndex == Octree::kInvalidIndex)
                continue;

            std::uint32_t childOffset = 0u;
            for (int child = 0; child < 8; ++child)
            {
                if (!node.ChildExists(child))
                    continue;
                if (stackSize >= kMaxStack)
                    break;
                stack[stackSize++] = node.BaseChildIndex + childOffset;
                ++childOffset;
            }
        }

        return out;
    }

    std::optional<float> PlaneField::Evaluate(const glm::vec3& worldPoint) const
    {
        const auto eval = EvaluateWithNormal(*this, worldPoint);
        if (!eval.Valid)
            return std::nullopt;
        return eval.Value;
    }

    std::optional<glm::vec3> PlaneField::Project(
        const glm::vec3& worldPoint,
        const std::size_t maxIterations,
        const float convergenceEpsilon) const
    {
        glm::vec3 current = worldPoint;
        for (std::size_t iteration = 0; iteration < maxIterations; ++iteration)
        {
            const auto eval = EvaluateWithNormal(*this, current);
            if (!eval.Valid)
                return std::nullopt;

            if (std::abs(eval.Value) <= convergenceEpsilon)
                return current;

            current -= eval.Value * eval.Normal;
        }
        return current;
    }

    std::optional<BuildResult> BuildPlaneField(const Halfedge::Mesh& mesh, const BuildParams& params)
    {
        if (mesh.VertexCount() == 0u || mesh.FaceCount() == 0u)
            return std::nullopt;

        BuildResult result;
        result.Status = BuildStatus::Success;
        result.Field.Hierarchy().m_Nodes.clear();
        result.Field.Hierarchy().NodeProperties.Clear();
        result.Field.Hierarchy().ElementAabbs.clear();

        auto& tree = result.Field.Hierarchy();
        (void)tree.GetOrAddNodeProperty<glm::vec3>(std::string(kClosestPointProperty), glm::vec3(0.0f));
        (void)tree.GetOrAddNodeProperty<glm::vec3>(std::string(kNormalProperty), glm::vec3(0.0f, 1.0f, 0.0f));
        (void)tree.GetOrAddNodeProperty<float>(std::string(kSignedDistanceProperty), 0.0f);
        (void)tree.GetOrAddNodeProperty<float>(std::string(kMaxPlaneErrorProperty), 0.0f);
        (void)tree.GetOrAddNodeProperty<float>(std::string(kSupportRadiusProperty), 0.0f);
        (void)tree.GetOrAddNodeProperty<std::uint32_t>(std::string(kFlagsProperty), 0u);
        (void)tree.GetOrAddNodeProperty<std::uint32_t>(std::string(kSourcePrimitiveProperty), std::numeric_limits<std::uint32_t>::max());

        BuildContext ctx(mesh, params, result.Stats, result.Field);
        if (ctx.Oracle.Empty())
            return std::nullopt;

        AABB bounds = ComputeMeshBounds(mesh);
        const float diagonal = NodeDiagonal(bounds);
        if (!std::isfinite(diagonal) || diagonal <= kEpsilon)
            return std::nullopt;

        const glm::vec3 padding(params.BoundingBoxPadding * diagonal);
        bounds.Min -= padding;
        bounds.Max += padding;

        // Expand to cube for stable octree subdivision.
        const glm::vec3 extent = bounds.Max - bounds.Min;
        const float maxExtent = std::max({extent.x, extent.y, extent.z});
        const glm::vec3 center = bounds.GetCenter();
        const glm::vec3 half(0.5f * maxExtent);
        bounds.Min = center - half;
        bounds.Max = center + half;

        result.Field.SetBlendNormalCompatibilityDot(CosDegrees(params.BlendNormalCompatibilityDegrees));
        CreateNode(tree, bounds);
        BuildNodeRecursive(ctx, result.Field, 0u, 0u);

        result.Stats.NodeCount = tree.m_Nodes.size();
        if (result.Stats.MaxReachedDepth >= params.MaxDepth)
            result.Status = BuildStatus::MaxDepthReached;
        else
            result.Status = BuildStatus::Success;

        return result;
    }

    std::optional<Grid::DenseGrid> SampleToDenseGrid(
        const PlaneField& field,
        const Grid::GridDimensions& dims,
        const std::string_view scalarPropertyName)
    {
        if (!dims.IsValid() || field.Hierarchy().m_Nodes.empty())
            return std::nullopt;

        Grid::DenseGrid grid(dims);
        auto scalar = grid.AddProperty<float>(std::string(scalarPropertyName), 0.0f);
        if (!scalar)
            return std::nullopt;

        for (std::size_t z = 0; z <= dims.NZ; ++z)
        {
            for (std::size_t y = 0; y <= dims.NY; ++y)
            {
                for (std::size_t x = 0; x <= dims.NX; ++x)
                {
                    const glm::vec3 worldPos = dims.WorldPosition(x, y, z);
                    const auto value = field.Evaluate(worldPos);
                    grid.Set(scalar, x, y, z, value.value_or(1.0f));
                }
            }
        }

        return grid;
    }

    std::optional<Halfedge::Mesh> ExtractMesh(
        const PlaneField& field,
        const Grid::GridDimensions& dims,
        const MarchingCubes::MarchingCubesParams& mcParams,
        const std::string_view scalarPropertyName)
    {
        auto grid = SampleToDenseGrid(field, dims, scalarPropertyName);
        if (!grid.has_value())
            return std::nullopt;

        auto mc = MarchingCubes::Extract(*grid, mcParams, scalarPropertyName);
        if (!mc.has_value())
            return std::nullopt;

        return MarchingCubes::ToMesh(*mc);
    }
}
