// Implements canonical runtime mesh face/corner traversal, shading seams and
// the generated-atlas extent record.
module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Extrinsic.Runtime.MeshSurfaceTopology;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.MeshSoup;
import Geometry.Properties;
import Geometry.UvAtlas;
import Extrinsic.Core.Hash;

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

        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        [[nodiscard]] glm::vec3 ResolveNormal(
            const glm::vec3 candidate,
            const glm::vec3 fallback) noexcept
        {
            constexpr float kDegenerateLengthEpsilon = 1.0e-6f;
            if (IsFinite(candidate))
            {
                const float length = glm::length(candidate);
                if (std::isfinite(length) && length > kDegenerateLengthEpsilon)
                    return candidate / length;
            }
            if (IsFinite(fallback))
            {
                const float length = glm::length(fallback);
                if (std::isfinite(length) && length > kDegenerateLengthEpsilon)
                    return fallback / length;
            }
            return glm::vec3{0.0f, 0.0f, 1.0f};
        }

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

        struct FaceRingInputs
        {
            const std::vector<std::uint32_t>* ToVertex{nullptr};
            const std::vector<std::uint32_t>* NextHalfedge{nullptr};
            const std::vector<std::uint32_t>* HalfedgeFace{nullptr};
            const std::vector<std::uint32_t>* FaceHalfedge{nullptr};
            std::uint32_t VertexCount{0u};
        };

        // Validates and borrows the canonical face-ring topology rows.
        [[nodiscard]] MeshSurfaceTopologyStatus ResolveFaceRingInputs(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            FaceRingInputs& out)
        {
            using namespace ECS::Components::GeometrySources;
            if (BuildSourceAvailability(view).ProvenanceDomain != Domain::Mesh)
                return MeshSurfaceTopologyStatus::WrongDomain;
            if (view.VertexSource == nullptr)
                return MeshSurfaceTopologyStatus::MissingVertexSource;
            // Connectivity indexes vertex slots, independent of whichever
            // position-valued property a caller binds for geometric work.
            out.VertexCount =
                static_cast<std::uint32_t>(view.VertexSource->Properties.Size());
            if (out.VertexCount == 0u)
                return MeshSurfaceTopologyStatus::EmptyMesh;

            if (view.HalfedgeSource == nullptr)
                return MeshSurfaceTopologyStatus::MissingHalfedgeTopology;
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
                return MeshSurfaceTopologyStatus::MissingHalfedgeTopology;
            const std::size_t halfedgeCount = toVertex.Vector().size();
            if (halfedgeCount == 0u)
                return MeshSurfaceTopologyStatus::EmptyMesh;
            if (nextHalfedge.Vector().size() != halfedgeCount
                || halfedgeFace.Vector().size() != halfedgeCount)
            {
                return MeshSurfaceTopologyStatus::InvalidTopology;
            }

            if (view.FaceSource == nullptr)
                return MeshSurfaceTopologyStatus::MissingFaceTopology;
            const auto faceHalfedge =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kFaceHalfedge);
            if (!faceHalfedge)
                return MeshSurfaceTopologyStatus::MissingFaceTopology;
            if (faceHalfedge.Vector().empty())
                return MeshSurfaceTopologyStatus::EmptyMesh;

            out.ToVertex = &toVertex.Vector();
            out.NextHalfedge = &nextHalfedge.Vector();
            out.HalfedgeFace = &halfedgeFace.Vector();
            out.FaceHalfedge = &faceHalfedge.Vector();
            return MeshSurfaceTopologyStatus::Success;
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

            FaceRingInputs inputs{};
            if (const MeshSurfaceTopologyStatus status = ResolveFaceRingInputs(view, inputs);
                status != MeshSurfaceTopologyStatus::Success)
            {
                return fail(status);
            }
            const std::vector<std::uint32_t>& faceHalfedgeRows = *inputs.FaceHalfedge;
            const std::vector<std::uint32_t>& halfedgeFaceRows = *inputs.HalfedgeFace;
            const std::vector<std::uint32_t>& nextHalfedgeRows = *inputs.NextHalfedge;
            const std::vector<std::uint32_t>& toVertexRows = *inputs.ToVertex;
            const std::uint32_t vertexCount = inputs.VertexCount;
            const std::size_t faceCount = faceHalfedgeRows.size();

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
                    faceHalfedgeRows,
                    halfedgeFaceRows,
                    nextHalfedgeRows,
                    toVertexRows,
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

        template <typename T>
        [[nodiscard]] bool PublishMeshCornerProperty(
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
            const std::size_t sourceVertexCount,
            const std::span<const T> cornerValues,
            const std::string_view propertyName,
            const T defaultValue)
        {
            if (mesh.FacesSize() != sourceFaces.size() ||
                mesh.VerticesSize() != sourceVertexCount ||
                cornerValues.size() != sourceFaces.size() * 3u)
            {
                return false;
            }

            std::vector<T> values(mesh.HalfedgesSize(), defaultValue);
            std::vector<std::uint8_t> written(mesh.HalfedgesSize(), 0u);
            std::vector<T> valueForVertex(mesh.VerticesSize(), defaultValue);
            std::vector<std::uint8_t> vertexHasValue(mesh.VerticesSize(), 0u);

            for (std::size_t faceIndex = 0u; faceIndex < mesh.FacesSize(); ++faceIndex)
            {
                const Geometry::FaceHandle face{
                    static_cast<Geometry::PropertyIndex>(faceIndex)};
                if (mesh.IsDeleted(face))
                    continue;

                const std::vector<std::uint32_t>& indices =
                    sourceFaces[faceIndex].Indices;
                for (const Geometry::HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundFace(face))
                {
                    const Geometry::VertexHandle vertex = mesh.ToVertex(halfedge);
                    std::size_t slot = indices.size();
                    for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k)
                    {
                        if (indices[k] == vertex.Index)
                        {
                            slot = k;
                            break;
                        }
                    }
                    if (slot >= 3u)
                        return false;

                    const T value = cornerValues[faceIndex * 3u + slot];
                    values[halfedge.Index] = value;
                    written[halfedge.Index] = 1u;
                    valueForVertex[vertex.Index] = value;
                    vertexHasValue[vertex.Index] = 1u;
                }
            }

            for (std::size_t index = 0u; index < values.size(); ++index)
            {
                if (written[index] != 0u)
                    continue;
                const Geometry::HalfedgeHandle halfedge{
                    static_cast<Geometry::PropertyIndex>(index)};
                if (mesh.IsDeleted(halfedge))
                    continue;
                const Geometry::VertexHandle vertex = mesh.ToVertex(halfedge);
                if (mesh.IsValid(vertex) && vertexHasValue[vertex.Index] != 0u)
                    values[index] = valueForVertex[vertex.Index];
            }

            auto property = mesh.HalfedgeProperties().GetOrAdd<T>(
                std::string{propertyName}, defaultValue);
            if (property.Vector().size() != values.size())
                return false;
            property.Vector() = std::move(values);
            return true;
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
        case MeshSurfaceTopologyStatus::MissingVertexSource:
            return "MeshTopology.MissingVertexSource";
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

    MeshSurfaceTopologyStatus BuildMeshFaceCenters(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const std::span<const glm::vec3> vertexPositions,
        std::vector<glm::vec3>& outCenters,
        std::vector<std::uint32_t>& outLiveFaces)
    {
        outCenters.clear();
        outLiveFaces.clear();
        FaceRingInputs inputs{};
        if (const MeshSurfaceTopologyStatus status = ResolveFaceRingInputs(view, inputs);
            status != MeshSurfaceTopologyStatus::Success)
        {
            return status;
        }
        if (vertexPositions.size() != inputs.VertexCount)
            return MeshSurfaceTopologyStatus::MissingVertexSource;

        const std::size_t faceCount = inputs.FaceHalfedge->size();
        std::optional<std::vector<bool>> deleted{};
        if (const auto deletedProperty =
                view.FaceSource->Properties.Get<bool>("f:deleted");
            deletedProperty)
        {
            deleted = deletedProperty.Vector();
        }

        outCenters.assign(faceCount, glm::vec3{0.0f});
        outLiveFaces.reserve(faceCount);
        std::vector<std::uint32_t> ring;
        ring.reserve(8u);
        for (std::size_t faceIndex = 0u; faceIndex < faceCount; ++faceIndex)
        {
            if (deleted.has_value() && faceIndex < deleted->size() && (*deleted)[faceIndex])
                continue;
            const FaceRingOutcome outcome = ProduceFaceRing(
                *inputs.FaceHalfedge,
                *inputs.HalfedgeFace,
                *inputs.NextHalfedge,
                *inputs.ToVertex,
                static_cast<std::uint32_t>(faceCount),
                inputs.VertexCount,
                faceIndex,
                ring);
            if (outcome == FaceRingOutcome::Invalid)
            {
                outCenters.clear();
                outLiveFaces.clear();
                return MeshSurfaceTopologyStatus::InvalidTopology;
            }
            if (outcome == FaceRingOutcome::Skip)
                continue;

            glm::dvec3 sum{0.0};
            bool finite = true;
            for (const std::uint32_t vertex : ring)
            {
                const glm::vec3 position = vertexPositions[vertex];
                finite = finite && IsFinite(position);
                sum += glm::dvec3{position};
            }
            if (!finite)
                continue;
            outCenters[faceIndex] =
                glm::vec3{sum / static_cast<double>(ring.size())};
            outLiveFaces.push_back(static_cast<std::uint32_t>(faceIndex));
        }
        return MeshSurfaceTopologyStatus::Success;
    }

    bool BuildMeshCornerTexcoordSplit(
        const std::span<const glm::vec2> cornerTexcoords,
        const std::span<const std::uint32_t> cornerHalfedges,
        const std::span<const glm::vec2> fallbackVertexTexcoords,
        const std::size_t vertexCount,
        std::vector<std::uint32_t>& surfaceIndices,
        MeshCornerTexcoordSplit& outSplit)
    {
        outSplit.SourceVertexForSlot.clear();
        outSplit.TexcoordForSlot.clear();

        MeshCornerAttributeSplit attributes{};
        if (!BuildMeshCornerAttributeSplit(
                cornerTexcoords,
                {},
                cornerHalfedges,
                fallbackVertexTexcoords,
                {},
                vertexCount,
                surfaceIndices,
                attributes))
        {
            return false;
        }
        outSplit.SourceVertexForSlot = std::move(attributes.SourceVertexForSlot);
        outSplit.TexcoordForSlot = std::move(attributes.TexcoordForSlot);
        return true;
    }

    bool BuildMeshCornerAttributeSplit(
        const std::span<const glm::vec2> cornerTexcoords,
        const std::span<const glm::vec3> cornerNormals,
        const std::span<const std::uint32_t> cornerHalfedges,
        const std::span<const glm::vec2> fallbackVertexTexcoords,
        const std::span<const glm::vec3> fallbackVertexNormals,
        const std::size_t vertexCount,
        std::vector<std::uint32_t>& surfaceIndices,
        MeshCornerAttributeSplit& outSplit)
    {
        outSplit.SourceVertexForSlot.clear();
        outSplit.TexcoordForSlot.clear();
        outSplit.NormalForSlot.clear();

        if (cornerHalfedges.size() != surfaceIndices.size() ||
            surfaceIndices.empty() ||
            (cornerTexcoords.empty() && cornerNormals.empty()))
        {
            return false;
        }

        for (const std::uint32_t vertex : surfaceIndices)
        {
            if (vertex >= vertexCount)
            {
                return false;
            }
        }

        struct EmittedAttributes
        {
            glm::vec2 Texcoord{0.0f};
            glm::vec3 Normal{0.0f, 0.0f, 1.0f};
            std::uint32_t Slot{kInvalidIndex};
        };
        std::unordered_map<std::uint32_t, std::vector<EmittedAttributes>> emitted;
        emitted.reserve(vertexCount);

        std::vector<std::uint32_t> remappedIndices;
        remappedIndices.reserve(surfaceIndices.size());
        outSplit.SourceVertexForSlot.reserve(vertexCount);
        outSplit.TexcoordForSlot.reserve(vertexCount);
        outSplit.NormalForSlot.reserve(vertexCount);

        for (std::size_t corner = 0u; corner < surfaceIndices.size(); ++corner)
        {
            const std::uint32_t sourceVertex = surfaceIndices[corner];
            const std::uint32_t halfedge = cornerHalfedges[corner];

            glm::vec2 uv{0.0f, 0.0f};
            if (halfedge < cornerTexcoords.size())
            {
                uv = cornerTexcoords[halfedge];
            }
            else if (sourceVertex < fallbackVertexTexcoords.size())
            {
                uv = fallbackVertexTexcoords[sourceVertex];
            }
            if (!IsFinite(uv))
            {
                uv = glm::vec2{0.0f, 0.0f};
            }

            const glm::vec3 fallbackNormal =
                sourceVertex < fallbackVertexNormals.size()
                    ? fallbackVertexNormals[sourceVertex]
                    : glm::vec3{0.0f, 0.0f, 1.0f};
            const glm::vec3 normal = ResolveNormal(
                halfedge < cornerNormals.size()
                    ? cornerNormals[halfedge]
                    : fallbackNormal,
                fallbackNormal);

            std::vector<EmittedAttributes>& seen = emitted[sourceVertex];
            std::uint32_t slot = kInvalidIndex;
            for (const EmittedAttributes& existing : seen)
            {
                if (existing.Texcoord.x == uv.x && existing.Texcoord.y == uv.y &&
                    existing.Normal.x == normal.x &&
                    existing.Normal.y == normal.y &&
                    existing.Normal.z == normal.z)
                {
                    slot = existing.Slot;
                    break;
                }
            }

            if (slot == kInvalidIndex)
            {
                slot = static_cast<std::uint32_t>(outSplit.SourceVertexForSlot.size());
                outSplit.SourceVertexForSlot.push_back(sourceVertex);
                outSplit.TexcoordForSlot.push_back(uv);
                outSplit.NormalForSlot.push_back(normal);
                seen.push_back(EmittedAttributes{uv, normal, slot});
            }

            remappedIndices.push_back(slot);
        }

        surfaceIndices = std::move(remappedIndices);
        return true;
    }

    bool PublishMeshCornerTexcoords(
        Geometry::HalfedgeMesh::Mesh& mesh,
        const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        const std::size_t sourceVertexCount,
        const std::span<const glm::vec2> cornerUvs)
    {
        return PublishMeshCornerProperty(
            mesh,
            sourceFaces,
            sourceVertexCount,
            cornerUvs,
            Geometry::MeshUtils::kHalfedgeTexcoordPropertyName,
            glm::vec2{0.0f});
    }

    bool PublishMeshCornerNormals(
        Geometry::HalfedgeMesh::Mesh& mesh,
        const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        const std::size_t sourceVertexCount,
        const std::span<const glm::vec3> cornerNormals)
    {
        return PublishMeshCornerProperty(
            mesh,
            sourceFaces,
            sourceVertexCount,
            cornerNormals,
            "h:normal",
            glm::vec3{0.0f, 0.0f, 1.0f});
    }

    bool FinalizeMeshCornerTexcoords(
        MeshCornerTexcoords& corners,
        const std::span<const std::uint8_t> cornerAssigned,
        const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        const std::size_t sourceVertexCount)
    {
        if (corners.CornerUvs.size() != sourceFaces.size() * 3u ||
            cornerAssigned.size() != corners.CornerUvs.size())
        {
            return false;
        }

        corners.VertexUvs.assign(sourceVertexCount, glm::vec2{0.0f});
        std::vector<std::uint8_t> vertexAssigned(sourceVertexCount, 0u);

        for (std::size_t face = 0u; face < sourceFaces.size(); ++face)
        {
            const std::vector<std::uint32_t>& indices =
                sourceFaces[face].Indices;
            for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k)
            {
                const std::size_t corner = face * 3u + k;
                if (cornerAssigned[corner] == 0u)
                    continue;
                const std::uint32_t vertex = indices[k];
                if (vertex >= corners.VertexUvs.size())
                    return false;
                if (vertexAssigned[vertex] == 0u)
                {
                    corners.VertexUvs[vertex] = corners.CornerUvs[corner];
                    vertexAssigned[vertex] = 1u;
                }
                else if (corners.VertexUvs[vertex] != corners.CornerUvs[corner])
                {
                    corners.HasSeam = true;
                }
            }
        }

        for (std::size_t face = 0u; face < sourceFaces.size(); ++face)
        {
            const std::vector<std::uint32_t>& indices =
                sourceFaces[face].Indices;
            for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k)
            {
                const std::size_t corner = face * 3u + k;
                if (cornerAssigned[corner] != 0u)
                    continue;
                ++corners.UnmappedCornerCount;
                const std::uint32_t vertex = indices[k];
                if (vertex < corners.VertexUvs.size() &&
                    vertexAssigned[vertex] != 0u)
                {
                    corners.CornerUvs[corner] = corners.VertexUvs[vertex];
                }
            }
        }
        return true;
    }

    bool GatherSplitMeshCornerTexcoords(
        const Geometry::MeshSoup::IndexedMesh& outputMesh,
        const std::span<const glm::vec2> outputVertexUvs,
        const std::span<const std::uint32_t> sourceFaceForOutputFace,
        const std::span<const std::uint32_t> sourceVertexForOutputVertex,
        const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        const std::size_t sourceVertexCount,
        MeshCornerTexcoords& out)
    {
        out = MeshCornerTexcoords{};

        if (outputVertexUvs.size() != outputMesh.VertexCount())
            return false;

        const std::span<const Geometry::MeshSoup::PolygonFace> outputFaces =
            outputMesh.Faces();
        if (sourceFaceForOutputFace.size() != outputFaces.size())
            return false;

        out.CornerUvs.assign(sourceFaces.size() * 3u, glm::vec2{0.0f});
        std::vector<std::uint8_t> cornerAssigned(sourceFaces.size() * 3u, 0u);

        for (std::size_t outputFace = 0u; outputFace < outputFaces.size();
             ++outputFace)
        {
            const std::uint32_t sourceFace =
                sourceFaceForOutputFace[outputFace];
            if (sourceFace >= sourceFaces.size())
                return false;

            const std::vector<std::uint32_t>& outputIndices =
                outputFaces[outputFace].Indices;
            const std::vector<std::uint32_t>& sourceIndices =
                sourceFaces[sourceFace].Indices;
            if (outputIndices.size() != 3u || sourceIndices.size() != 3u)
                return false;

            for (std::size_t k = 0u; k < 3u; ++k)
            {
                const std::uint32_t outputVertex = outputIndices[k];
                if (outputVertex >= sourceVertexForOutputVertex.size())
                    return false;
                const std::uint32_t sourceVertex =
                    sourceVertexForOutputVertex[outputVertex];

                // Chart splitting preserves winding, so corner k normally maps
                // to corner k; the search only covers a backend that rotated
                // the triangle.
                std::size_t slot = 3u;
                if (sourceIndices[k] == sourceVertex)
                {
                    slot = k;
                }
                else
                {
                    for (std::size_t candidate = 0u; candidate < 3u; ++candidate)
                    {
                        if (sourceIndices[candidate] == sourceVertex)
                        {
                            slot = candidate;
                            break;
                        }
                    }
                }
                if (slot >= 3u)
                    return false;

                const std::size_t corner = sourceFace * 3u + slot;
                out.CornerUvs[corner] = outputVertexUvs[outputVertex];
                cornerAssigned[corner] = 1u;
            }
        }

        return FinalizeMeshCornerTexcoords(
            out, cornerAssigned, sourceFaces, sourceVertexCount);
    }

    namespace
    {
        struct CanonicalTexcoordBinding
        {
            std::optional<std::uint64_t> Corner{};
            std::optional<std::uint64_t> Vertex{};
            const Geometry::PropertySet* Corners{nullptr};
            const Geometry::PropertySet* Vertices{nullptr};

            [[nodiscard]] bool MatchesStamps(const MeshUvAtlasExtent& extent) const noexcept
            {
                return Corner == extent.CornerTexcoordRevision &&
                       Vertex == extent.VertexTexcoordRevision;
            }

            // Presence and exact bytes of the canonical corner-over-vertex UVs.
            [[nodiscard]] std::uint64_t Fingerprint() const
            {
                const auto hash = [](const Geometry::PropertySet& set, const char* name)
                {
                    const auto uv = Geometry::ConstPropertySet{set}.Get<glm::vec2>(name);
                    if (!uv)
                        return std::uint64_t{0u};
                    const std::span<const glm::vec2> values{uv.Vector()};
                    return Core::Hash::HashString64(std::string_view{
                               reinterpret_cast<const char*>(values.data()), values.size_bytes()}) |
                           1u;
                };
                const std::uint64_t corner = Corner.has_value()
                    ? hash(*Corners, Geometry::MeshUtils::kHalfedgeTexcoordPropertyName) : 0u;
                const std::uint64_t vertex = Vertex.has_value()
                    ? hash(*Vertices, Geometry::MeshUtils::kVertexTexcoordPropertyName) : 0u;
                const std::uint64_t combined = (corner * 1099511628211ull) ^ vertex;
                return combined == 0u ? 1u : combined;
            }
        };

        // Revision of the selected canonical UV property, or nullopt when the
        // entity is not a mesh carrying a complete `h:texcoord` or
        // `v:texcoord` (the bake's canonical corner-over-vertex atlas).
        [[nodiscard]] std::optional<CanonicalTexcoordBinding> BindCanonicalTexcoords(
            const entt::registry& registry,
            const entt::entity entity)
        {
            if (!registry.valid(entity))
                return std::nullopt;
            const ECS::Components::GeometrySources::ConstSourceView view =
                ECS::Components::GeometrySources::BuildConstView(registry, entity);
            if (view.ActiveDomain != ECS::Components::GeometrySources::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr)
            {
                return std::nullopt;
            }
            const auto complete = [](const Geometry::PropertySet& set, const char* name)
            {
                const auto uv = Geometry::ConstPropertySet{set}.Get<glm::vec2>(name);
                return uv && uv.Vector().size() == set.Size();
            };
            const Geometry::PropertySet& corners = view.HalfedgeSource->Properties;
            const Geometry::PropertySet& vertices = view.VertexSource->Properties;
            const bool useCorners = complete(corners, Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
            if (!useCorners &&
                !complete(vertices, Geometry::MeshUtils::kVertexTexcoordPropertyName))
            {
                return std::nullopt;
            }
            return CanonicalTexcoordBinding{
                .Corner = useCorners ? corners.FindPropertyRevision(
                    Geometry::MeshUtils::kHalfedgeTexcoordPropertyName) : std::nullopt,
                .Vertex = useCorners ? std::nullopt : vertices.FindPropertyRevision(
                    Geometry::MeshUtils::kVertexTexcoordPropertyName),
                .Corners = &corners,
                .Vertices = &vertices,
            };
        }
    }

    bool IsValidMeshUvAtlasExtent(const std::uint32_t width, const std::uint32_t height)
    {
        // Resolution 0 means "default" to the generator, so it is excluded
        // here; the upper bound is the generator's own option preflight.
        const auto valid = [](const std::uint32_t side)
        {
            return side != 0u &&
                   Geometry::UvAtlas::ValidateUvAtlasOptions(
                       Geometry::UvAtlas::UvAtlasOptions{.Resolution = side, .Padding = 0u})
                       .Valid;
        };
        return valid(width) && valid(height);
    }

    bool PublishMeshUvAtlasExtent(
        entt::registry& registry,
        const entt::entity entity,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (!registry.valid(entity))
            return false;
        const std::optional<CanonicalTexcoordBinding> binding =
            IsValidMeshUvAtlasExtent(width, height)
                ? BindCanonicalTexcoords(registry, entity)
                : std::nullopt;
        if (!binding.has_value())
        {
            registry.remove<MeshUvAtlasExtent>(entity);
            return false;
        }
        registry.emplace_or_replace<MeshUvAtlasExtent>(
            entity,
            MeshUvAtlasExtent{
                .Width = width,
                .Height = height,
                .CornerTexcoordRevision = binding->Corner,
                .VertexTexcoordRevision = binding->Vertex,
                .TexcoordFingerprint = binding->Fingerprint(),
            });
        return true;
    }

    void RestoreMeshUvAtlasExtent(
        entt::registry& registry,
        const entt::entity entity,
        MeshUvAtlasExtent extent)
    {
        if (!registry.valid(entity))
            return;
        extent.CornerTexcoordRevision.reset();
        extent.VertexTexcoordRevision.reset();
        extent.StampsMatchContent = false;
        registry.emplace_or_replace<MeshUvAtlasExtent>(entity, extent);
    }

    std::optional<MeshUvAtlasExtent> FindCurrentMeshUvAtlasExtent(
        const entt::registry& registry,
        const entt::entity entity)
    {
        const auto* extent = registry.valid(entity)
            ? registry.try_get<MeshUvAtlasExtent>(entity)
            : nullptr;
        if (extent == nullptr)
            return std::nullopt;
        const std::optional<CanonicalTexcoordBinding> binding =
            BindCanonicalTexcoords(registry, entity);
        if (!binding.has_value() ||
            (binding->MatchesStamps(*extent) ? !extent->StampsMatchContent :
             binding->Fingerprint() != extent->TexcoordFingerprint))
        {
            return std::nullopt;
        }
        return *extent;
    }

    std::optional<MeshUvAtlasExtent> RefreshMeshUvAtlasExtent(
        entt::registry& registry,
        const entt::entity entity)
    {
        auto* extent = registry.valid(entity)
            ? registry.try_get<MeshUvAtlasExtent>(entity)
            : nullptr;
        if (extent == nullptr)
            return std::nullopt;
        const std::optional<CanonicalTexcoordBinding> binding =
            BindCanonicalTexcoords(registry, entity);
        if (!binding.has_value())
            return std::nullopt;
        if (binding->MatchesStamps(*extent))
            return extent->StampsMatchContent ? std::optional{*extent} : std::nullopt;
        extent->StampsMatchContent = binding->Fingerprint() == extent->TexcoordFingerprint;
        extent->CornerTexcoordRevision = binding->Corner;
        extent->VertexTexcoordRevision = binding->Vertex;
        return extent->StampsMatchContent ? std::optional{*extent} : std::nullopt;
    }
}
