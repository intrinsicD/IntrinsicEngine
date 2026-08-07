module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshSurfaceTopology;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex =
            std::numeric_limits<std::uint32_t>::max();

        enum class FaceRingOutcome : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] FaceRingOutcome ProduceFaceRing(
            const std::vector<std::uint32_t>& faceHalfedge,
            const std::vector<std::uint32_t>& halfedgeFace,
            const std::vector<std::uint32_t>& nextHalfedge,
            const std::vector<std::uint32_t>& toVertex,
            const std::uint32_t faceCount,
            const std::uint32_t vertexCount,
            const std::size_t faceIndex,
            std::vector<std::uint32_t>& outRing,
            std::vector<std::uint32_t>* outRingHalfedges = nullptr)
        {
            outRing.clear();
            if (outRingHalfedges != nullptr)
                outRingHalfedges->clear();
            const std::size_t halfedgeCount = toVertex.size();
            const std::uint32_t first = faceHalfedge[faceIndex];
            if (first == kInvalidIndex)
                return FaceRingOutcome::Skip;
            if (first >= halfedgeCount)
                return FaceRingOutcome::Invalid;

            const std::uint32_t firstOwner = halfedgeFace[first];
            if (firstOwner == kInvalidIndex || firstOwner >= faceCount
                || firstOwner != static_cast<std::uint32_t>(faceIndex))
            {
                return FaceRingOutcome::Skip;
            }

            std::uint32_t halfedge = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (halfedge >= halfedgeCount
                    || halfedgeFace[halfedge]
                        != static_cast<std::uint32_t>(faceIndex))
                {
                    return FaceRingOutcome::Invalid;
                }
                const std::uint32_t target = toVertex[halfedge];
                if (target >= vertexCount)
                    return FaceRingOutcome::Invalid;
                outRing.push_back(target);
                if (outRingHalfedges != nullptr)
                    outRingHalfedges->push_back(halfedge);

                const std::uint32_t next = nextHalfedge[halfedge];
                if (next == first)
                    break;
                if (next == kInvalidIndex || step == halfedgeCount)
                    return FaceRingOutcome::Invalid;
                halfedge = next;
            }

            return outRing.size() < 3u
                ? FaceRingOutcome::Skip
                : FaceRingOutcome::Triangulate;
        }

        [[nodiscard]] MeshSurfaceTopologyStatus BuildTopology(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            std::vector<std::uint32_t>* outSurfaceIndices,
            std::vector<std::uint32_t>* outTriangleToFace,
            std::vector<std::uint32_t>* outCornerHalfedges = nullptr)
        {
            using namespace ECS::Components::GeometrySources;
            if (outSurfaceIndices != nullptr)
                outSurfaceIndices->clear();
            if (outTriangleToFace != nullptr)
                outTriangleToFace->clear();
            if (outCornerHalfedges != nullptr)
                outCornerHalfedges->clear();

            const auto fail = [&](const MeshSurfaceTopologyStatus status)
            {
                if (outSurfaceIndices != nullptr)
                    outSurfaceIndices->clear();
                if (outTriangleToFace != nullptr)
                    outTriangleToFace->clear();
                if (outCornerHalfedges != nullptr)
                    outCornerHalfedges->clear();
                return status;
            };

            if (BuildSourceAvailability(view).ProvenanceDomain != Domain::Mesh)
                return fail(MeshSurfaceTopologyStatus::WrongDomain);
            if (view.VertexSource == nullptr)
                return fail(MeshSurfaceTopologyStatus::MissingPositions);
            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(
                PropertyNames::kPosition);
            if (!positions)
                return fail(MeshSurfaceTopologyStatus::MissingPositions);
            const std::uint32_t vertexCount =
                static_cast<std::uint32_t>(positions.Vector().size());
            if (vertexCount == 0u)
                return fail(MeshSurfaceTopologyStatus::EmptyMesh);

            if (view.HalfedgeSource == nullptr)
                return fail(MeshSurfaceTopologyStatus::MissingHalfedgeTopology);
            const auto toVertex =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedge =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeNext);
            const auto halfedgeFace =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeFace);
            if (!toVertex || !nextHalfedge || !halfedgeFace)
                return fail(MeshSurfaceTopologyStatus::MissingHalfedgeTopology);
            const std::size_t halfedgeCount = toVertex.Vector().size();
            if (halfedgeCount == 0u)
                return fail(MeshSurfaceTopologyStatus::EmptyMesh);
            if (nextHalfedge.Vector().size() != halfedgeCount
                || halfedgeFace.Vector().size() != halfedgeCount)
            {
                return fail(MeshSurfaceTopologyStatus::InvalidTopology);
            }

            if (view.FaceSource == nullptr)
                return fail(MeshSurfaceTopologyStatus::MissingFaceTopology);
            const auto faceHalfedge =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kFaceHalfedge);
            if (!faceHalfedge)
                return fail(MeshSurfaceTopologyStatus::MissingFaceTopology);
            const std::size_t faceCount = faceHalfedge.Vector().size();
            if (faceCount == 0u)
                return fail(MeshSurfaceTopologyStatus::EmptyMesh);

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            std::vector<std::uint32_t> ringHalfedges;
            ringHalfedges.reserve(8u);
            std::size_t triangleCount = 0u;
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceCount;
                 ++faceIndex)
            {
                const FaceRingOutcome outcome = ProduceFaceRing(
                    faceHalfedge.Vector(),
                    halfedgeFace.Vector(),
                    nextHalfedge.Vector(),
                    toVertex.Vector(),
                    static_cast<std::uint32_t>(faceCount),
                    vertexCount,
                    faceIndex,
                    ring,
                    outCornerHalfedges != nullptr ? &ringHalfedges : nullptr);
                if (outcome == FaceRingOutcome::Invalid)
                    return fail(MeshSurfaceTopologyStatus::InvalidTopology);
                if (outcome == FaceRingOutcome::Skip)
                    continue;

                for (std::size_t ringIndex = 1u;
                     ringIndex + 1u < ring.size();
                     ++ringIndex)
                {
                    if (outSurfaceIndices != nullptr)
                    {
                        outSurfaceIndices->insert(
                            outSurfaceIndices->end(),
                            {ring[0u], ring[ringIndex], ring[ringIndex + 1u]});
                    }
                    if (outCornerHalfedges != nullptr)
                    {
                        // Parallel to the fan emitted above, so corner `i` of
                        // the triangle list resolves to the halfedge whose
                        // target is that corner's vertex.
                        outCornerHalfedges->insert(
                            outCornerHalfedges->end(),
                            {ringHalfedges[0u],
                             ringHalfedges[ringIndex],
                             ringHalfedges[ringIndex + 1u]});
                    }
                    if (outTriangleToFace != nullptr)
                    {
                        outTriangleToFace->push_back(
                            static_cast<std::uint32_t>(faceIndex));
                    }
                    ++triangleCount;
                }
            }

            if (triangleCount == 0u)
                return fail(MeshSurfaceTopologyStatus::DegenerateAllFaces);
            return MeshSurfaceTopologyStatus::Success;
        }
    }

    const char* DebugNameForMeshSurfaceTopologyStatus(
        const MeshSurfaceTopologyStatus status) noexcept
    {
        switch (status)
        {
        case MeshSurfaceTopologyStatus::Success:
            return "MeshTopology.Success";
        case MeshSurfaceTopologyStatus::WrongDomain:
            return "MeshTopology.WrongDomain";
        case MeshSurfaceTopologyStatus::MissingPositions:
            return "MeshTopology.MissingPositions";
        case MeshSurfaceTopologyStatus::MissingHalfedgeTopology:
            return "MeshTopology.MissingHalfedgeTopology";
        case MeshSurfaceTopologyStatus::MissingFaceTopology:
            return "MeshTopology.MissingFaceTopology";
        case MeshSurfaceTopologyStatus::EmptyMesh:
            return "MeshTopology.EmptyMesh";
        case MeshSurfaceTopologyStatus::InvalidTopology:
            return "MeshTopology.InvalidTopology";
        case MeshSurfaceTopologyStatus::DegenerateAllFaces:
            return "MeshTopology.DegenerateAllFaces";
        }
        return "MeshTopology.Unknown";
    }

    MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleFaceMap(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outTriangleToFace)
    {
        return BuildTopology(view, nullptr, &outTriangleToFace);
    }

    MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace)
    {
        return BuildTopology(view, &outSurfaceIndices, &outTriangleToFace);
    }

    MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleCornerTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace,
        std::vector<std::uint32_t>& outCornerHalfedges)
    {
        return BuildTopology(
            view, &outSurfaceIndices, &outTriangleToFace, &outCornerHalfedges);
    }
}
