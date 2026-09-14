#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    namespace GS = ECS::Components::GeometrySources;
        [[nodiscard]] MeshFaceRingStatus BuildMeshFaceRing(
            const std::vector<std::uint32_t>& faceHalfedges,
            const std::vector<std::uint32_t>& halfedgeFaces,
            const std::vector<std::uint32_t>& nextHalfedges,
            const std::vector<std::uint32_t>& toVertices,
            const std::size_t faceIndex,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outRing)
        {
            constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();
            outRing.clear();
            if (faceIndex >= faceHalfedges.size())
                return MeshFaceRingStatus::Invalid;

            const std::size_t halfedgeCount = toVertices.size();
            const std::uint32_t first = faceHalfedges[faceIndex];
            if (first == invalid)
                return MeshFaceRingStatus::Skip;
            if (first >= halfedgeCount)
                return MeshFaceRingStatus::Invalid;

            const std::uint32_t owner = halfedgeFaces[first];
            if (owner == invalid || owner >= faceHalfedges.size())
                return MeshFaceRingStatus::Skip;
            if (owner != static_cast<std::uint32_t>(faceIndex))
                return MeshFaceRingStatus::Skip;

            std::uint32_t halfedge = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (halfedge >= halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                if (halfedgeFaces[halfedge] != static_cast<std::uint32_t>(faceIndex))
                    return MeshFaceRingStatus::Invalid;

                const std::uint32_t vertex = toVertices[halfedge];
                if (vertex >= vertexCount)
                    return MeshFaceRingStatus::Invalid;
                outRing.push_back(vertex);

                const std::uint32_t next = nextHalfedges[halfedge];
                if (next == first)
                    break;
                if (next == invalid || step == halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                halfedge = next;
            }

            return outRing.size() >= 3u
                ? MeshFaceRingStatus::Triangulate
                : MeshFaceRingStatus::Skip;
        }
        [[nodiscard]] MeshForVertexNormalsResult
        BuildHalfedgeMeshForVertexNormalRecompute(const GS::ConstSourceView& view, std::string_view positionProperty)
        {
            MeshForVertexNormalsResult result{};
            if (view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh vertex normals require selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    positionProperty);
            if (!positions || positions.Vector().empty() ||
                positions.Vector().size() != view.VertexSource->Properties.Size())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh requires count-matched v:position for normal recompute";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(
                    std::numeric_limits<Geometry::PropertyIndex>::max()))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has too many vertices for normal recompute";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces ||
                !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().size() !=
                    view.FaceSource->Properties.Size())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has invalid halfedge/face topology for normal recompute";
                return result;
            }

            result.Mesh.Reserve(positions.Vector().size(),
                                view.EdgeSource != nullptr
                                    ? view.EdgeSource->Properties.Size()
                                    : 0u,
                                faceHalfedges.Vector().size());
            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            const auto deletedFaces = view.FaceSource->Properties.Get<bool>("f:deleted");
            const auto deletedEdges = view.EdgeSource
                ? view.EdgeSource->Properties.Get<bool>("e:deleted")
                : decltype(view.FaceSource->Properties.Get<bool>("f:deleted")){};
            std::vector<std::uint32_t> ring{};
            ring.reserve(8u);
            std::vector<Geometry::VertexHandle> faceVertices{};
            faceVertices.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                if (deletedFaces && faceIndex < deletedFaces.Size() && deletedFaces[faceIndex])
                    continue;
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid || status == MeshFaceRingStatus::Skip)
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh topology is not valid for normal recompute";
                    return result;
                }

                if (deletedEdges)
                {
                    auto h = faceHalfedges[faceIndex];
                    bool touchesDeletedEdge = false;
                    for (std::size_t corner = 0; corner < ring.size(); ++corner)
                    {
                        if (h / 2 < deletedEdges.Size() && deletedEdges[h / 2]) touchesDeletedEdge = true;
                        h = nextHalfedges[h];
                    }
                    if (touchesDeletedEdge) continue;
                }
                faceVertices.clear();
                for (const std::uint32_t vertex : ring)
                {
                    faceVertices.push_back(
                        Geometry::VertexHandle{
                            static_cast<Geometry::PropertyIndex>(vertex)});
                }
                if (!result.Mesh.AddFace(faceVertices).has_value())
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic = "selected mesh face ring could not be reconstructed "
                                        "for normal recompute";
                    return result;
                }
                result.SourceFaceForMeshFace.push_back(static_cast<std::uint32_t>(faceIndex));
            }

            result.Status = EditorCommandStatus::Applied;
            return result;
        }
}
