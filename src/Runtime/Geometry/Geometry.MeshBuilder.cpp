module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Geometry.MeshBuilder;

import Geometry.HalfedgeMesh;
import Geometry.Subdivision;

namespace Geometry::Halfedge
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;
        constexpr std::size_t kRevolveSegments = 24;
        constexpr std::size_t kHemisphereBands = 6;

        [[nodiscard]] bool IsFiniteFloat(const float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3& value) noexcept
        {
            return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
        }

        [[nodiscard]] bool IsFiniteQuat(const glm::quat& value) noexcept
        {
            return IsFiniteFloat(value.w) && IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
        }

        [[nodiscard]] glm::quat NormalizeQuatSafe(const glm::quat& rotation) noexcept
        {
            const float lenSq = glm::dot(rotation, rotation);
            if (lenSq <= kEpsilon || !IsFiniteFloat(lenSq))
            {
                return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            }
            return rotation * glm::inversesqrt(lenSq);
        }

        [[nodiscard]] bool AddFaceChecked(Mesh& mesh, std::span<const VertexHandle> vertices) noexcept
        {
            return mesh.AddFace(vertices).has_value();
        }

        [[nodiscard]] bool AddQuadChecked(Mesh& mesh,
                                          const VertexHandle v0,
                                          const VertexHandle v1,
                                          const VertexHandle v2,
                                          const VertexHandle v3) noexcept
        {
            return mesh.AddQuad(v0, v1, v2, v3).has_value();
        }

        [[nodiscard]] bool AddTriangleChecked(Mesh& mesh,
                                              const VertexHandle v0,
                                              const VertexHandle v1,
                                              const VertexHandle v2) noexcept
        {
            return mesh.AddTriangle(v0, v1, v2).has_value();
        }

        [[nodiscard]] std::optional<Mesh> BuildBoxFromCanonicalCorners(const std::array<glm::vec3, 8>& corners) noexcept
        {
            for (const glm::vec3& corner : corners)
            {
                if (!IsFiniteVec3(corner))
                {
                    return std::nullopt;
                }
            }

            Mesh mesh;
            std::array<VertexHandle, 8> vertices{};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                vertices[i] = mesh.AddVertex(corners[i]);
            }

            if (!AddQuadChecked(mesh, vertices[3], vertices[2], vertices[1], vertices[0]) ||
                !AddQuadChecked(mesh, vertices[4], vertices[5], vertices[6], vertices[7]) ||
                !AddQuadChecked(mesh, vertices[0], vertices[1], vertices[5], vertices[4]) ||
                !AddQuadChecked(mesh, vertices[2], vertices[3], vertices[7], vertices[6]) ||
                !AddQuadChecked(mesh, vertices[0], vertices[4], vertices[7], vertices[3]) ||
                !AddQuadChecked(mesh, vertices[1], vertices[2], vertices[6], vertices[5]))
            {
                return std::nullopt;
            }

            return mesh;
        }

        void ProjectVerticesToUnitSphere(Mesh& mesh) noexcept
        {
            for (glm::vec3& position : mesh.Positions())
            {
                const float lenSq = glm::dot(position, position);
                if (lenSq <= kEpsilon)
                {
                    position = glm::vec3{1.0f, 0.0f, 0.0f};
                    continue;
                }
                position *= glm::inversesqrt(lenSq);
            }
        }

        [[nodiscard]] std::optional<Mesh> MakeUnitSphereMesh(const uint8_t subdivLevel) noexcept
        {
            Mesh current = MakeMeshIcosahedron();
            ProjectVerticesToUnitSphere(current);

            for (uint8_t iteration = 0; iteration < subdivLevel; ++iteration)
            {
                Mesh refined;
                Geometry::Subdivision::SubdivisionParams params;
                params.Iterations = 1;
                if (!Geometry::Subdivision::Subdivide(current, refined, params).has_value())
                {
                    return std::nullopt;
                }
                current = std::move(refined);
                ProjectVerticesToUnitSphere(current);
            }

            return current;
        }

        struct RevolutionFrame
        {
            glm::vec3 AxisDirection{};
            glm::vec3 Tangent{};
            glm::vec3 Bitangent{};
        };

        [[nodiscard]] std::optional<RevolutionFrame> MakeRevolutionFrame(const glm::vec3& pointA,
                                                                          const glm::vec3& pointB) noexcept
        {
            if (!IsFiniteVec3(pointA) || !IsFiniteVec3(pointB))
            {
                return std::nullopt;
            }

            const glm::vec3 axis = pointB - pointA;
            const float axisLenSq = glm::dot(axis, axis);
            if (axisLenSq <= kEpsilon)
            {
                return std::nullopt;
            }

            const glm::vec3 axisDirection = axis * glm::inversesqrt(axisLenSq);
            const glm::vec3 seed = (std::abs(axisDirection.y) < 0.999f)
                ? glm::vec3{0.0f, 1.0f, 0.0f}
                : glm::vec3{1.0f, 0.0f, 0.0f};
            glm::vec3 tangent = glm::cross(axisDirection, seed);
            const float tangentLenSq = glm::dot(tangent, tangent);
            if (tangentLenSq <= kEpsilon)
            {
                return std::nullopt;
            }
            tangent *= glm::inversesqrt(tangentLenSq);
            glm::vec3 bitangent = glm::cross(axisDirection, tangent);
            const float bitangentLenSq = glm::dot(bitangent, bitangent);
            if (bitangentLenSq <= kEpsilon)
            {
                return std::nullopt;
            }
            bitangent *= glm::inversesqrt(bitangentLenSq);

            return RevolutionFrame{
                .AxisDirection = axisDirection,
                .Tangent = tangent,
                .Bitangent = bitangent,
            };
        }

        [[nodiscard]] std::vector<VertexHandle> AddRing(Mesh& mesh,
                                                        const glm::vec3& center,
                                                        const glm::vec3& tangent,
                                                        const glm::vec3& bitangent,
                                                        const float radius,
                                                        const std::size_t segments) noexcept
        {
            std::vector<VertexHandle> ring;
            ring.reserve(segments);

            for (std::size_t i = 0; i < segments; ++i)
            {
                const float angle = (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) /
                                    static_cast<float>(segments);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const glm::vec3 radial = tangent * c + bitangent * s;
                ring.push_back(mesh.AddVertex(center + radius * radial));
            }

            return ring;
        }

        [[nodiscard]] glm::vec3 ComputePolygonAverage(const Mesh& mesh,
                                                      const std::vector<VertexHandle>& polygon) noexcept
        {
            glm::vec3 average{0.0f};
            for (const VertexHandle vertex : polygon)
            {
                average += mesh.Position(vertex);
            }
            return average / static_cast<float>(polygon.size());
        }

        [[nodiscard]] glm::vec3 ComputePolygonNormal(const Mesh& mesh,
                                                     const std::vector<VertexHandle>& polygon) noexcept
        {
            glm::vec3 normal{0.0f};
            for (std::size_t i = 0; i < polygon.size(); ++i)
            {
                const glm::vec3& current = mesh.Position(polygon[i]);
                const glm::vec3& next = mesh.Position(polygon[(i + 1) % polygon.size()]);
                normal.x += (current.y - next.y) * (current.z + next.z);
                normal.y += (current.z - next.z) * (current.x + next.x);
                normal.z += (current.x - next.x) * (current.y + next.y);
            }
            return normal;
        }
    }

    std::optional<Mesh> MakeMesh(const AABB& aabb) noexcept
    {
        if (!aabb.IsValid() || !IsFiniteVec3(aabb.Min) || !IsFiniteVec3(aabb.Max))
        {
            return std::nullopt;
        }

        const glm::vec3 size = aabb.GetSize();
        if (size.x <= kEpsilon || size.y <= kEpsilon || size.z <= kEpsilon)
        {
            return std::nullopt;
        }

        return BuildBoxFromCanonicalCorners(aabb.GetCorners());
    }

    std::optional<Mesh> MakeMesh(const OBB& obb) noexcept
    {
        if (!obb.IsValid() || !IsFiniteVec3(obb.Center) || !IsFiniteVec3(obb.Extents) || !IsFiniteQuat(obb.Rotation))
        {
            return std::nullopt;
        }
        if (obb.Extents.x <= kEpsilon || obb.Extents.y <= kEpsilon || obb.Extents.z <= kEpsilon)
        {
            return std::nullopt;
        }

        const glm::quat rotation = NormalizeQuatSafe(obb.Rotation);
        const glm::vec3 e = obb.Extents;
        const std::array<glm::vec3, 8> corners{
            obb.Center + rotation * glm::vec3{-e.x, -e.y, -e.z},
            obb.Center + rotation * glm::vec3{ e.x, -e.y, -e.z},
            obb.Center + rotation * glm::vec3{ e.x,  e.y, -e.z},
            obb.Center + rotation * glm::vec3{-e.x,  e.y, -e.z},
            obb.Center + rotation * glm::vec3{-e.x, -e.y,  e.z},
            obb.Center + rotation * glm::vec3{ e.x, -e.y,  e.z},
            obb.Center + rotation * glm::vec3{ e.x,  e.y,  e.z},
            obb.Center + rotation * glm::vec3{-e.x,  e.y,  e.z},
        };

        return BuildBoxFromCanonicalCorners(corners);
    }

    std::optional<Mesh> MakeMesh(const Sphere& sphere, const uint8_t subdiv_level) noexcept
    {
        if (!IsFiniteVec3(sphere.Center) || !IsFiniteFloat(sphere.Radius) || sphere.Radius <= kEpsilon)
        {
            return std::nullopt;
        }

        auto mesh = MakeUnitSphereMesh(subdiv_level);
        if (!mesh.has_value())
        {
            return std::nullopt;
        }

        for (glm::vec3& position : mesh->Positions())
        {
            position = sphere.Center + sphere.Radius * position;
        }

        return mesh;
    }

    std::optional<Mesh> MakeMesh(const Capsule& capsule) noexcept
    {
        if (!IsFiniteVec3(capsule.PointA) || !IsFiniteVec3(capsule.PointB) ||
            !IsFiniteFloat(capsule.Radius) || capsule.Radius <= kEpsilon)
        {
            return std::nullopt;
        }

        const glm::vec3 axis = capsule.PointB - capsule.PointA;
        if (glm::dot(axis, axis) <= kEpsilon)
        {
            return MakeMesh(Sphere{capsule.PointA, capsule.Radius});
        }

        const auto frame = MakeRevolutionFrame(capsule.PointA, capsule.PointB);
        if (!frame.has_value())
        {
            return std::nullopt;
        }

        Mesh mesh;
        const VertexHandle southPole = mesh.AddVertex(capsule.PointA - frame->AxisDirection * capsule.Radius);
        std::vector<std::vector<VertexHandle>> rings;
        rings.reserve(2u * kHemisphereBands);

        for (std::size_t band = kHemisphereBands; band-- > 1;)
        {
            const float gamma = (static_cast<float>(band) * std::numbers::pi_v<float>) /
                                (2.0f * static_cast<float>(kHemisphereBands));
            const float radial = capsule.Radius * std::cos(gamma);
            const float axialOffset = -capsule.Radius * std::sin(gamma);
            rings.push_back(AddRing(mesh,
                                    capsule.PointA + frame->AxisDirection * axialOffset,
                                    frame->Tangent,
                                    frame->Bitangent,
                                    radial,
                                    kRevolveSegments));
        }

        rings.push_back(AddRing(mesh,
                                capsule.PointA,
                                frame->Tangent,
                                frame->Bitangent,
                                capsule.Radius,
                                kRevolveSegments));
        rings.push_back(AddRing(mesh,
                                capsule.PointB,
                                frame->Tangent,
                                frame->Bitangent,
                                capsule.Radius,
                                kRevolveSegments));

        for (std::size_t band = 1; band < kHemisphereBands; ++band)
        {
            const float gamma = (static_cast<float>(band) * std::numbers::pi_v<float>) /
                                (2.0f * static_cast<float>(kHemisphereBands));
            const float radial = capsule.Radius * std::cos(gamma);
            const float axialOffset = capsule.Radius * std::sin(gamma);
            rings.push_back(AddRing(mesh,
                                    capsule.PointB + frame->AxisDirection * axialOffset,
                                    frame->Tangent,
                                    frame->Bitangent,
                                    radial,
                                    kRevolveSegments));
        }

        const VertexHandle northPole = mesh.AddVertex(capsule.PointB + frame->AxisDirection * capsule.Radius);
        if (rings.empty())
        {
            return std::nullopt;
        }

        const auto& southRing = rings.front();
        for (std::size_t i = 0; i < kRevolveSegments; ++i)
        {
            const std::size_t next = (i + 1u) % kRevolveSegments;
            if (!AddTriangleChecked(mesh, southPole, southRing[next], southRing[i]))
            {
                return std::nullopt;
            }
        }

        for (std::size_t ringIndex = 0; ringIndex + 1u < rings.size(); ++ringIndex)
        {
            const auto& lower = rings[ringIndex];
            const auto& upper = rings[ringIndex + 1u];
            for (std::size_t i = 0; i < kRevolveSegments; ++i)
            {
                const std::size_t next = (i + 1u) % kRevolveSegments;
                if (!AddQuadChecked(mesh, lower[i], lower[next], upper[next], upper[i]))
                {
                    return std::nullopt;
                }
            }
        }

        const auto& northRing = rings.back();
        for (std::size_t i = 0; i < kRevolveSegments; ++i)
        {
            const std::size_t next = (i + 1u) % kRevolveSegments;
            if (!AddTriangleChecked(mesh, northPole, northRing[i], northRing[next]))
            {
                return std::nullopt;
            }
        }

        return mesh;
    }

    std::optional<Mesh> MakeMesh(const Cylinder& cylinder) noexcept
    {
        if (!IsFiniteVec3(cylinder.PointA) || !IsFiniteVec3(cylinder.PointB) ||
            !IsFiniteFloat(cylinder.Radius) || cylinder.Radius <= kEpsilon)
        {
            return std::nullopt;
        }

        const auto frame = MakeRevolutionFrame(cylinder.PointA, cylinder.PointB);
        if (!frame.has_value())
        {
            return std::nullopt;
        }

        Mesh mesh;
        const std::vector<VertexHandle> bottom = AddRing(mesh,
                                                         cylinder.PointA,
                                                         frame->Tangent,
                                                         frame->Bitangent,
                                                         cylinder.Radius,
                                                         kRevolveSegments);
        const std::vector<VertexHandle> top = AddRing(mesh,
                                                      cylinder.PointB,
                                                      frame->Tangent,
                                                      frame->Bitangent,
                                                      cylinder.Radius,
                                                      kRevolveSegments);

        for (std::size_t i = 0; i < kRevolveSegments; ++i)
        {
            const std::size_t next = (i + 1u) % kRevolveSegments;
            if (!AddQuadChecked(mesh, bottom[i], bottom[next], top[next], top[i]))
            {
                return std::nullopt;
            }
        }

        std::vector<VertexHandle> bottomCap = bottom;
        std::reverse(bottomCap.begin(), bottomCap.end());
        if (!AddFaceChecked(mesh, bottomCap) || !AddFaceChecked(mesh, top))
        {
            return std::nullopt;
        }

        return mesh;
    }

    std::optional<Mesh> MakeMesh(const Ellipsoid& ellipsoid) noexcept
    {
        if (!IsFiniteVec3(ellipsoid.Center) || !IsFiniteVec3(ellipsoid.Radii) || !IsFiniteQuat(ellipsoid.Rotation))
        {
            return std::nullopt;
        }
        if (ellipsoid.Radii.x <= kEpsilon || ellipsoid.Radii.y <= kEpsilon || ellipsoid.Radii.z <= kEpsilon)
        {
            return std::nullopt;
        }

        auto mesh = MakeUnitSphereMesh(3);
        if (!mesh.has_value())
        {
            return std::nullopt;
        }

        const glm::quat rotation = NormalizeQuatSafe(ellipsoid.Rotation);
        for (glm::vec3& position : mesh->Positions())
        {
            position = ellipsoid.Center + rotation * (position * ellipsoid.Radii);
        }

        return mesh;
    }

    Mesh MakeMeshTetrahedron() noexcept
    {
        Mesh mesh;
        const VertexHandle v0 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
        const VertexHandle v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
        const VertexHandle v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
        const VertexHandle v3 = mesh.AddVertex({-1.0f, -1.0f, 1.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        (void)mesh.AddTriangle(v0, v2, v3);
        (void)mesh.AddTriangle(v0, v3, v1);
        (void)mesh.AddTriangle(v1, v3, v2);
        return mesh;
    }

    Mesh MakeMeshIcosahedron() noexcept
    {
        Mesh mesh;
        const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
        const float scale = 1.0f / std::sqrt(1.0f + phi * phi);

        const VertexHandle v0 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, phi} * scale);
        const VertexHandle v1 = mesh.AddVertex(glm::vec3{0.0f, -1.0f, phi} * scale);
        const VertexHandle v2 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, -phi} * scale);
        const VertexHandle v3 = mesh.AddVertex(glm::vec3{0.0f, -1.0f, -phi} * scale);
        const VertexHandle v4 = mesh.AddVertex(glm::vec3{1.0f, phi, 0.0f} * scale);
        const VertexHandle v5 = mesh.AddVertex(glm::vec3{-1.0f, phi, 0.0f} * scale);
        const VertexHandle v6 = mesh.AddVertex(glm::vec3{1.0f, -phi, 0.0f} * scale);
        const VertexHandle v7 = mesh.AddVertex(glm::vec3{-1.0f, -phi, 0.0f} * scale);
        const VertexHandle v8 = mesh.AddVertex(glm::vec3{phi, 0.0f, 1.0f} * scale);
        const VertexHandle v9 = mesh.AddVertex(glm::vec3{-phi, 0.0f, 1.0f} * scale);
        const VertexHandle v10 = mesh.AddVertex(glm::vec3{phi, 0.0f, -1.0f} * scale);
        const VertexHandle v11 = mesh.AddVertex(glm::vec3{-phi, 0.0f, -1.0f} * scale);

        (void)mesh.AddTriangle(v0, v1, v8);
        (void)mesh.AddTriangle(v0, v8, v4);
        (void)mesh.AddTriangle(v0, v4, v5);
        (void)mesh.AddTriangle(v0, v5, v9);
        (void)mesh.AddTriangle(v0, v9, v1);
        (void)mesh.AddTriangle(v1, v6, v8);
        (void)mesh.AddTriangle(v1, v7, v6);
        (void)mesh.AddTriangle(v1, v9, v7);
        (void)mesh.AddTriangle(v2, v3, v11);
        (void)mesh.AddTriangle(v2, v10, v3);
        (void)mesh.AddTriangle(v2, v4, v10);
        (void)mesh.AddTriangle(v2, v5, v4);
        (void)mesh.AddTriangle(v2, v11, v5);
        (void)mesh.AddTriangle(v3, v6, v7);
        (void)mesh.AddTriangle(v3, v10, v6);
        (void)mesh.AddTriangle(v3, v7, v11);
        (void)mesh.AddTriangle(v4, v8, v10);
        (void)mesh.AddTriangle(v5, v11, v9);
        (void)mesh.AddTriangle(v6, v10, v8);
        (void)mesh.AddTriangle(v7, v9, v11);
        return mesh;
    }

    Mesh MakeMeshDodecahedron() noexcept
    {
        const Mesh icosahedron = MakeMeshIcosahedron();
        Mesh dodecahedron;
        std::vector<VertexHandle> faceCenterVertices(icosahedron.FacesSize());

        for (std::size_t fi = 0; fi < icosahedron.FacesSize(); ++fi)
        {
            const FaceHandle face{static_cast<PropertyIndex>(fi)};
            if (icosahedron.IsDeleted(face))
            {
                continue;
            }

            glm::vec3 centroid{0.0f};
            std::size_t count = 0;
            for (const VertexHandle vertex : icosahedron.VerticesAroundFace(face))
            {
                centroid += icosahedron.Position(vertex);
                ++count;
            }
            if (count == 0)
            {
                continue;
            }
            centroid /= static_cast<float>(count);
            const float lenSq = glm::dot(centroid, centroid);
            if (lenSq > kEpsilon)
            {
                centroid *= glm::inversesqrt(lenSq);
            }
            faceCenterVertices[fi] = dodecahedron.AddVertex(centroid);
        }

        for (std::size_t vi = 0; vi < icosahedron.VerticesSize(); ++vi)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(vi)};
            if (icosahedron.IsDeleted(vertex) || icosahedron.IsIsolated(vertex))
            {
                continue;
            }

            std::vector<VertexHandle> polygon;
            polygon.reserve(icosahedron.Valence(vertex));
            for (const HalfedgeHandle halfedge : icosahedron.HalfedgesAroundVertex(vertex))
            {
                const FaceHandle face = icosahedron.Face(halfedge);
                if (!face.IsValid() || icosahedron.IsDeleted(face))
                {
                    continue;
                }
                polygon.push_back(faceCenterVertices[face.Index]);
            }

            if (polygon.size() < 3u)
            {
                continue;
            }

            if (glm::dot(ComputePolygonNormal(dodecahedron, polygon), ComputePolygonAverage(dodecahedron, polygon)) < 0.0f)
            {
                std::reverse(polygon.begin(), polygon.end());
            }
            (void)dodecahedron.AddFace(polygon);
        }

        return dodecahedron;
    }

    Mesh MakeMeshOctahedron() noexcept
    {
        Mesh mesh;
        const VertexHandle px = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const VertexHandle nx = mesh.AddVertex({-1.0f, 0.0f, 0.0f});
        const VertexHandle py = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        const VertexHandle ny = mesh.AddVertex({0.0f, -1.0f, 0.0f});
        const VertexHandle pz = mesh.AddVertex({0.0f, 0.0f, 1.0f});
        const VertexHandle nz = mesh.AddVertex({0.0f, 0.0f, -1.0f});

        (void)mesh.AddTriangle(px, py, pz);
        (void)mesh.AddTriangle(py, nx, pz);
        (void)mesh.AddTriangle(nx, ny, pz);
        (void)mesh.AddTriangle(ny, px, pz);
        (void)mesh.AddTriangle(py, px, nz);
        (void)mesh.AddTriangle(nx, py, nz);
        (void)mesh.AddTriangle(ny, nx, nz);
        (void)mesh.AddTriangle(px, ny, nz);
        return mesh;
    }
}


