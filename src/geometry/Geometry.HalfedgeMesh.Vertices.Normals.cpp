module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.HalfedgeMesh.Vertices.Normals;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

namespace Geometry::HalfedgeMesh::VertexNormals
{
    namespace
    {
        constexpr glm::vec3 kDefaultFallbackNormal{0.0f, 1.0f, 0.0f};

        struct FaceCorner
        {
            VertexHandle Vertex{};
            glm::dvec3 Position{0.0};
        };

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] double DotLengthSq(const glm::dvec3 value) noexcept
        {
            return glm::dot(value, value);
        }

        void NormalizeFallback(const Params& params,
                               Result& result,
                               glm::vec3& out) noexcept
        {
            if (IsFinite(params.FallbackNormal))
            {
                const double lenSq = static_cast<double>(glm::dot(params.FallbackNormal, params.FallbackNormal));
                if (std::isfinite(lenSq)
                    && lenSq > params.DegenerateNormalLengthEpsilon * params.DegenerateNormalLengthEpsilon)
                {
                    out = params.FallbackNormal * static_cast<float>(1.0 / std::sqrt(lenSq));
                    return;
                }
            }

            out = kDefaultFallbackNormal;
            result.FallbackNormalWasRepaired = true;
        }

        [[nodiscard]] bool GatherFaceCorners(const Mesh& mesh,
                                             FaceHandle face,
                                             const Params& params,
                                             Result& result,
                                             std::vector<FaceCorner>& corners)
        {
            corners.clear();

            if (!face.IsValid() || !mesh.IsValid(face))
            {
                ++result.InvalidTopologyFaceCount;
                return false;
            }

            HalfedgeHandle current = mesh.Halfedge(face);
            if (!current.IsValid() || !mesh.IsValid(current))
            {
                ++result.InvalidTopologyFaceCount;
                return false;
            }

            const HalfedgeHandle first = current;
            const std::size_t maxSteps = mesh.HalfedgesSize() + 1u;
            std::size_t steps = 0;

            do
            {
                if (!current.IsValid() || !mesh.IsValid(current))
                {
                    ++result.InvalidTopologyFaceCount;
                    return false;
                }

                if (params.SkipDeleted && mesh.IsDeleted(current))
                {
                    ++result.InvalidTopologyFaceCount;
                    return false;
                }

                if (mesh.Face(current) != face)
                {
                    ++result.InvalidTopologyFaceCount;
                    return false;
                }

                const VertexHandle vertex = mesh.ToVertex(current);
                if (!vertex.IsValid() || !mesh.IsValid(vertex))
                {
                    ++result.InvalidTopologyFaceCount;
                    return false;
                }

                if (params.SkipDeleted && mesh.IsDeleted(vertex))
                {
                    ++result.InvalidTopologyFaceCount;
                    return false;
                }

                const glm::vec3 position = mesh.Position(vertex);
                if (!IsFinite(position))
                {
                    ++result.NonFiniteFaceCount;
                    return false;
                }

                corners.push_back(FaceCorner{
                    .Vertex = vertex,
                    .Position = glm::dvec3(position),
                });

                current = mesh.NextHalfedge(current);
                ++steps;
            } while (current != first && steps <= maxSteps);

            if (current != first || steps > maxSteps)
            {
                ++result.InvalidTopologyFaceCount;
                return false;
            }

            if (corners.size() < 3u)
            {
                ++result.DegenerateFaceCount;
                return false;
            }

            return true;
        }

        [[nodiscard]] glm::dvec3 ComputeAreaVector(const std::vector<FaceCorner>& corners) noexcept
        {
            glm::dvec3 areaVector{0.0};
            const glm::dvec3 root = corners.front().Position;
            for (std::size_t i = 1u; i + 1u < corners.size(); ++i)
            {
                areaVector += glm::cross(corners[i].Position - root,
                                         corners[i + 1u].Position - root);
            }
            return areaVector;
        }

        [[nodiscard]] double CornerAngle(const std::vector<FaceCorner>& corners,
                                         const std::size_t index,
                                         const double epsilon,
                                         Result& result) noexcept
        {
            const std::size_t prevIndex = (index + corners.size() - 1u) % corners.size();
            const std::size_t nextIndex = (index + 1u) % corners.size();

            const glm::dvec3 toPrev = corners[prevIndex].Position - corners[index].Position;
            const glm::dvec3 toNext = corners[nextIndex].Position - corners[index].Position;
            const double prevLenSq = DotLengthSq(toPrev);
            const double nextLenSq = DotLengthSq(toNext);
            const double epsilonSq = epsilon * epsilon;

            if (!std::isfinite(prevLenSq) || !std::isfinite(nextLenSq)
                || prevLenSq <= epsilonSq || nextLenSq <= epsilonSq)
            {
                ++result.DegenerateCornerCount;
                return 0.0;
            }

            const double cosine = std::clamp(glm::dot(toPrev, toNext) / std::sqrt(prevLenSq * nextLenSq),
                                             -1.0,
                                             1.0);
            const double angle = std::acos(cosine);
            if (!std::isfinite(angle) || angle <= epsilon)
            {
                ++result.DegenerateCornerCount;
                return 0.0;
            }

            return angle;
        }

        [[nodiscard]] double MaxCornerWeight(const std::vector<FaceCorner>& corners,
                                             const std::size_t index,
                                             const double epsilon,
                                             Result& result) noexcept
        {
            const std::size_t prevIndex = (index + corners.size() - 1u) % corners.size();
            const std::size_t nextIndex = (index + 1u) % corners.size();

            const glm::dvec3 toPrev = corners[prevIndex].Position - corners[index].Position;
            const glm::dvec3 toNext = corners[nextIndex].Position - corners[index].Position;
            const double prevLenSq = DotLengthSq(toPrev);
            const double nextLenSq = DotLengthSq(toNext);
            const double epsilonSq = epsilon * epsilon;

            if (!std::isfinite(prevLenSq) || !std::isfinite(nextLenSq)
                || prevLenSq <= epsilonSq || nextLenSq <= epsilonSq)
            {
                ++result.DegenerateCornerCount;
                return 0.0;
            }

            const double crossLen = glm::length(glm::cross(toPrev, toNext));
            const double denominator = prevLenSq * nextLenSq;
            const double weight = crossLen / denominator;
            if (!std::isfinite(weight) || weight <= epsilon)
            {
                ++result.DegenerateCornerCount;
                return 0.0;
            }

            return weight;
        }

        [[nodiscard]] glm::dvec3 CornerContribution(const AveragingMode mode,
                                                    const std::vector<FaceCorner>& corners,
                                                    const std::size_t index,
                                                    const glm::dvec3 faceUnitNormal,
                                                    const glm::dvec3 areaVector,
                                                    const double epsilon,
                                                    Result& result) noexcept
        {
            switch (mode)
            {
            case AveragingMode::UniformFace:
                return faceUnitNormal;
            case AveragingMode::AreaWeighted:
                return areaVector;
            case AveragingMode::AngleWeighted:
                return faceUnitNormal * CornerAngle(corners, index, epsilon, result);
            case AveragingMode::MaxWeighted:
                return faceUnitNormal * MaxCornerWeight(corners, index, epsilon, result);
            }

            return areaVector;
        }
    } // namespace

    std::string_view DebugName(const AveragingMode mode) noexcept
    {
        switch (mode)
        {
        case AveragingMode::UniformFace:
            return "UniformFace";
        case AveragingMode::AreaWeighted:
            return "AreaWeighted";
        case AveragingMode::AngleWeighted:
            return "AngleWeighted";
        case AveragingMode::MaxWeighted:
            return "MaxWeighted";
        }

        return "Unknown";
    }

    std::string_view DebugName(const RecomputeStatus status) noexcept
    {
        switch (status)
        {
        case RecomputeStatus::Success:
            return "Success";
        case RecomputeStatus::EmptyMesh:
            return "EmptyMesh";
        case RecomputeStatus::InvalidOutputProperty:
            return "InvalidOutputProperty";
        case RecomputeStatus::PropertyTypeConflict:
            return "PropertyTypeConflict";
        }

        return "Unknown";
    }

    Result Recompute(Mesh& mesh, const Params& params)
    {
        Result result{};
        result.Weighting = params.Weighting;
        result.VertexSlotCount = mesh.VerticesSize();

        if (params.OutputProperty.empty())
        {
            result.Status = RecomputeStatus::InvalidOutputProperty;
            return result;
        }

        const double epsilon = params.DegenerateNormalLengthEpsilon > 0.0
            ? params.DegenerateNormalLengthEpsilon
            : Params{}.DegenerateNormalLengthEpsilon;

        glm::vec3 fallbackNormal{0.0f};
        NormalizeFallback(params, result, fallbackNormal);

        auto normals = mesh.VertexProperties().GetOrAdd<glm::vec3>(
            std::string(params.OutputProperty),
            fallbackNormal);
        if (!normals.IsValid())
        {
            result.Status = RecomputeStatus::PropertyTypeConflict;
            return result;
        }

        result.Normals = VertexProperty<glm::vec3>(normals);

        if (mesh.VerticesSize() == 0u)
        {
            result.Status = RecomputeStatus::EmptyMesh;
            return result;
        }

        std::vector<glm::dvec3> accumulators(mesh.VerticesSize(), glm::dvec3{0.0});
        std::vector<FaceCorner> corners;
        corners.reserve(8u);

        for (std::size_t faceIndex = 0; faceIndex < mesh.FacesSize(); ++faceIndex)
        {
            const FaceHandle face{static_cast<PropertyIndex>(faceIndex)};
            if (params.SkipDeleted && mesh.IsDeleted(face))
            {
                ++result.SkippedDeletedFaceCount;
                continue;
            }

            if (!GatherFaceCorners(mesh, face, params, result, corners))
            {
                continue;
            }

            const glm::dvec3 areaVector = ComputeAreaVector(corners);
            const double areaVectorLength = glm::length(areaVector);
            if (!IsFinite(areaVector) || !std::isfinite(areaVectorLength))
            {
                ++result.NonFiniteFaceCount;
                continue;
            }
            if (areaVectorLength <= epsilon)
            {
                ++result.DegenerateFaceCount;
                continue;
            }

            const glm::dvec3 faceUnitNormal = areaVector / areaVectorLength;
            ++result.ProcessedFaceCount;

            for (std::size_t cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex)
            {
                const VertexHandle vertex = corners[cornerIndex].Vertex;
                if (vertex.Index >= accumulators.size())
                {
                    ++result.InvalidTopologyFaceCount;
                    continue;
                }

                const glm::dvec3 contribution = CornerContribution(params.Weighting,
                                                                   corners,
                                                                   cornerIndex,
                                                                   faceUnitNormal,
                                                                   areaVector,
                                                                   epsilon,
                                                                   result);
                if (!IsFinite(contribution) || DotLengthSq(contribution) <= epsilon * epsilon)
                {
                    continue;
                }

                accumulators[vertex.Index] += contribution;
            }
        }

        for (std::size_t vertexIndex = 0; vertexIndex < mesh.VerticesSize(); ++vertexIndex)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(vertexIndex)};
            if (params.SkipDeleted && mesh.IsDeleted(vertex))
            {
                ++result.SkippedDeletedVertexCount;
                continue;
            }

            const glm::dvec3 accumulated = accumulators[vertexIndex];
            const double length = glm::length(accumulated);
            if (IsFinite(accumulated) && std::isfinite(length) && length > epsilon)
            {
                result.Normals[vertex] = glm::vec3(accumulated / length);
                ++result.ValidNormalVertexCount;
            }
            else
            {
                result.Normals[vertex] = fallbackNormal;
                ++result.FallbackVertexCount;
            }
            ++result.WrittenCount;
        }

        return result;
    }
} // namespace Geometry::HalfedgeMesh::VertexNormals
