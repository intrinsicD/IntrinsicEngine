module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
        constexpr const char* kMeshDebugName = "Runtime.Mesh";

        [[nodiscard]] MeshPackResult Failure(MeshPackStatus status, MeshPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return MeshPackResult{status, std::nullopt};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec2& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y);
        }

        [[nodiscard]] glm::vec3 NormalizeOrDefaultNormal(const glm::vec3 normal) noexcept
        {
            if (!IsFinite(normal))
            {
                return glm::vec3{0.0f, 0.0f, 1.0f};
            }
            const float len = glm::length(normal);
            return len > 1.0e-6f ? normal / len : glm::vec3{0.0f, 0.0f, 1.0f};
        }

        // Outcome of walking one face slot's halfedge ring.
        enum class FaceRingOutcome : std::uint8_t
        {
            Triangulate, // `outRing` holds >= 3 fan-order ring vertices.
            Skip,        // deleted / boundary / sub-triangular slot — emits no triangles.
            Invalid,     // corrupt topology — caller fails closed.
        };

        // Single source of truth for which face slots are live and the ring
        // vertices they fan-triangulate. `PackMesh` (surface triangulation) and
        // `BuildSurfaceTriangleFaceMap` (the gl_PrimitiveID -> face inverse) both
        // call this so the emitted surface triangle order and the triangle->face
        // map can never drift. Mirrors the deleted-face guard and the
        // step-capped ring walk documented on `PackMesh`.
        [[nodiscard]] FaceRingOutcome ProduceFaceRing(
            const std::vector<std::uint32_t>& faceHe,
            const std::vector<std::uint32_t>& halfedgeFace,
            const std::vector<std::uint32_t>& nextHe,
            const std::vector<std::uint32_t>& toVertex,
            std::uint32_t faceCountU32,
            std::uint32_t vertexCount,
            std::size_t f,
            std::vector<std::uint32_t>& outRing)
        {
            outRing.clear();

            const std::size_t halfedgeCount = toVertex.size();
            const std::uint32_t first = faceHe[f];
            if (first == kInvalidIndex)
            {
                return FaceRingOutcome::Skip; // boundary / unpopulated face slot
            }
            if (first >= halfedgeCount)
            {
                return FaceRingOutcome::Invalid;
            }

            // Deleted-face guard via `h:face` ownership of the first ring halfedge.
            const std::uint32_t firstOwner = halfedgeFace[first];
            if (firstOwner == kInvalidIndex || firstOwner >= faceCountU32)
            {
                return FaceRingOutcome::Skip;
            }
            if (firstOwner != static_cast<std::uint32_t>(f))
            {
                return FaceRingOutcome::Skip;
            }

            std::uint32_t h = first;
            for (std::size_t step = 0; step <= halfedgeCount; ++step)
            {
                if (h >= halfedgeCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                const std::uint32_t owner = halfedgeFace[h];
                if (owner != static_cast<std::uint32_t>(f))
                {
                    return FaceRingOutcome::Invalid; // mixed-owner ring
                }
                const std::uint32_t targetV = toVertex[h];
                if (targetV >= vertexCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                outRing.push_back(targetV);

                const std::uint32_t nh = nextHe[h];
                if (nh == first)
                {
                    break;
                }
                if (nh == kInvalidIndex)
                {
                    return FaceRingOutcome::Invalid;
                }
                if (step == halfedgeCount)
                {
                    return FaceRingOutcome::Invalid; // ring did not close
                }
                h = nh;
            }

            if (outRing.size() < 3)
            {
                return FaceRingOutcome::Skip; // degenerate / sub-triangular face
            }
            return FaceRingOutcome::Triangulate;
        }
    }

    const char* DebugNameForMeshPackStatus(MeshPackStatus status) noexcept
    {
        switch (status)
        {
            case MeshPackStatus::Success:                 return "Mesh.Success";
            case MeshPackStatus::WrongDomain:             return "Mesh.WrongDomain";
            case MeshPackStatus::MissingPositions:        return "Mesh.MissingPositions";
            case MeshPackStatus::MissingHalfedgeTopology: return "Mesh.MissingHalfedgeTopology";
            case MeshPackStatus::MissingFaceTopology:     return "Mesh.MissingFaceTopology";
            case MeshPackStatus::EmptyMesh:               return "Mesh.EmptyMesh";
            case MeshPackStatus::InvalidTopology:         return "Mesh.InvalidTopology";
            case MeshPackStatus::NonFinitePosition:       return "Mesh.NonFinitePosition";
            case MeshPackStatus::MissingTexcoords:        return "Mesh.MissingTexcoords";
            case MeshPackStatus::NonFiniteTexcoord:       return "Mesh.NonFiniteTexcoord";
            case MeshPackStatus::DegenerateAllFaces:      return "Mesh.DegenerateAllFaces";
        }
        return "Mesh.Unknown";
    }

    void MeshPackBuffer::Clear() noexcept
    {
        VertexBytes.clear();
        SurfaceIndices.clear();
    }

    MeshPackResult PackMesh(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        const SourceAvailability availability = BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != Domain::Mesh)
        {
            return Failure(MeshPackStatus::WrongDomain, outBuffer);
        }
        if (view.VertexSource == nullptr)
        {
            return Failure(MeshPackStatus::MissingPositions, outBuffer);
        }

        const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
        if (!posProp)
        {
            return Failure(MeshPackStatus::MissingPositions, outBuffer);
        }
        const auto& positions = posProp.Vector();
        const std::size_t vertexCount = positions.size();
        if (vertexCount == 0)
        {
            return Failure(MeshPackStatus::EmptyMesh, outBuffer);
        }

        if (view.HalfedgeSource == nullptr)
        {
            return Failure(MeshPackStatus::MissingHalfedgeTopology, outBuffer);
        }
        const auto toVertexProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeToVertex);
        const auto nextProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeNext);
        const auto faceProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeFace);
        if (!toVertexProp || !nextProp || !faceProp)
        {
            return Failure(MeshPackStatus::MissingHalfedgeTopology, outBuffer);
        }
        const auto& toVertex = toVertexProp.Vector();
        const auto& nextHe = nextProp.Vector();
        const auto& halfedgeFace = faceProp.Vector();
        const std::size_t halfedgeCount = toVertex.size();
        if (halfedgeCount == 0)
        {
            return Failure(MeshPackStatus::EmptyMesh, outBuffer);
        }
        if (nextHe.size() != halfedgeCount || halfedgeFace.size() != halfedgeCount)
        {
            return Failure(MeshPackStatus::InvalidTopology, outBuffer);
        }

        if (view.FaceSource == nullptr)
        {
            return Failure(MeshPackStatus::MissingFaceTopology, outBuffer);
        }
        const auto faceHeProp = view.FaceSource->Properties.Get<std::uint32_t>(
            PropertyNames::kFaceHalfedge);
        if (!faceHeProp)
        {
            return Failure(MeshPackStatus::MissingFaceTopology, outBuffer);
        }
        const auto& faceHe = faceHeProp.Vector();
        const std::size_t faceCount = faceHe.size();
        if (faceCount == 0)
        {
            return Failure(MeshPackStatus::EmptyMesh, outBuffer);
        }

        outBuffer.SurfaceIndices.reserve(faceCount * 3u);

        std::vector<std::uint32_t> ringScratch;
        ringScratch.reserve(8);

        const std::uint32_t faceCountU32 = static_cast<std::uint32_t>(faceCount);

        const std::uint32_t vertexCountU32 = static_cast<std::uint32_t>(vertexCount);

        const auto texcoordProp =
            view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
        const bool hasCountMatchedTexcoords =
            texcoordProp && texcoordProp.Vector().size() == vertexCount;
        const auto normalProp =
            view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kNormal);
        const bool hasCountMatchedNormals =
            normalProp && normalProp.Vector().size() == vertexCount;

        for (std::size_t f = 0; f < faceCount; ++f)
        {
            const FaceRingOutcome outcome = ProduceFaceRing(
                faceHe, halfedgeFace, nextHe, toVertex, faceCountU32, vertexCountU32, f, ringScratch);
            if (outcome == FaceRingOutcome::Invalid)
            {
                return Failure(MeshPackStatus::InvalidTopology, outBuffer);
            }
            if (outcome == FaceRingOutcome::Skip)
            {
                continue;
            }

            for (std::size_t i = 1; i + 1 < ringScratch.size(); ++i)
            {
                outBuffer.SurfaceIndices.push_back(ringScratch[0]);
                outBuffer.SurfaceIndices.push_back(ringScratch[i]);
                outBuffer.SurfaceIndices.push_back(ringScratch[i + 1]);
            }
        }

        if (outBuffer.SurfaceIndices.empty())
        {
            return Failure(MeshPackStatus::DegenerateAllFaces, outBuffer);
        }

        outBuffer.VertexBytes.resize(sizeof(MeshVertex) * vertexCount);
        auto* vData = reinterpret_cast<MeshVertex*>(outBuffer.VertexBytes.data());

        constexpr float kInf = std::numeric_limits<float>::infinity();
        glm::vec3 minP{+kInf, +kInf, +kInf};
        glm::vec3 maxP{-kInf, -kInf, -kInf};

        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const glm::vec3 p = positions[i];
            if (!IsFinite(p))
            {
                return Failure(MeshPackStatus::NonFinitePosition, outBuffer);
            }
            glm::vec2 uv{0.0f, 0.0f};
            if (hasCountMatchedTexcoords)
            {
                const glm::vec2 sourceUv = texcoordProp.Vector()[i];
                if (IsFinite(sourceUv))
                {
                    uv = sourceUv;
                }
            }
            const glm::vec3 n = hasCountMatchedNormals
                ? NormalizeOrDefaultNormal(normalProp.Vector()[i])
                : glm::vec3{0.0f, 0.0f, 1.0f};
            vData[i] = MeshVertex{p.x, p.y, p.z, uv.x, uv.y, n.x, n.y, n.z};
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.SurfaceIndices = std::span<const std::uint32_t>{outBuffer.SurfaceIndices};
        desc.LineIndices = {};
        desc.VertexCount = static_cast<std::uint32_t>(vertexCount);

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kMeshDebugName;

        return MeshPackResult{MeshPackStatus::Success, desc};
    }

    MeshPackStatus BuildSurfaceTriangleFaceMap(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outTriangleToFace)
    {
        outTriangleToFace.clear();

        using namespace ECS::Components::GeometrySources;

        // Validation mirrors `PackMesh` so the two stay in lockstep; only the
        // topology needed to reproduce the surface triangle order is read
        // (positions are needed for the `targetV < vertexCount` ring check).
        const SourceAvailability availability = BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != Domain::Mesh)
        {
            return MeshPackStatus::WrongDomain;
        }
        if (view.VertexSource == nullptr)
        {
            return MeshPackStatus::MissingPositions;
        }
        const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
        if (!posProp)
        {
            return MeshPackStatus::MissingPositions;
        }
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(posProp.Vector().size());
        if (vertexCount == 0)
        {
            return MeshPackStatus::EmptyMesh;
        }
        if (view.HalfedgeSource == nullptr)
        {
            return MeshPackStatus::MissingHalfedgeTopology;
        }
        const auto toVertexProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeToVertex);
        const auto nextProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeNext);
        const auto faceProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
            PropertyNames::kHalfedgeFace);
        if (!toVertexProp || !nextProp || !faceProp)
        {
            return MeshPackStatus::MissingHalfedgeTopology;
        }
        const auto& toVertex = toVertexProp.Vector();
        const auto& nextHe = nextProp.Vector();
        const auto& halfedgeFace = faceProp.Vector();
        const std::size_t halfedgeCount = toVertex.size();
        if (halfedgeCount == 0)
        {
            return MeshPackStatus::EmptyMesh;
        }
        if (nextHe.size() != halfedgeCount || halfedgeFace.size() != halfedgeCount)
        {
            return MeshPackStatus::InvalidTopology;
        }

        if (view.FaceSource == nullptr)
        {
            return MeshPackStatus::MissingFaceTopology;
        }
        const auto faceHeProp = view.FaceSource->Properties.Get<std::uint32_t>(
            PropertyNames::kFaceHalfedge);
        if (!faceHeProp)
        {
            return MeshPackStatus::MissingFaceTopology;
        }
        const auto& faceHe = faceHeProp.Vector();
        const std::size_t faceCount = faceHe.size();
        if (faceCount == 0)
        {
            return MeshPackStatus::EmptyMesh;
        }

        const std::uint32_t faceCountU32 = static_cast<std::uint32_t>(faceCount);

        std::vector<std::uint32_t> ringScratch;
        ringScratch.reserve(8);

        for (std::size_t f = 0; f < faceCount; ++f)
        {
            const FaceRingOutcome outcome = ProduceFaceRing(
                faceHe, halfedgeFace, nextHe, toVertex, faceCountU32, vertexCount, f, ringScratch);
            if (outcome == FaceRingOutcome::Invalid)
            {
                outTriangleToFace.clear();
                return MeshPackStatus::InvalidTopology;
            }
            if (outcome == FaceRingOutcome::Skip)
            {
                continue;
            }
            // Fan triangulation emits (ringSize - 2) triangles for this face,
            // all owned by face row `f` — matching PackMesh's emission count.
            const std::size_t triangles = ringScratch.size() - 2u;
            for (std::size_t t = 0; t < triangles; ++t)
            {
                outTriangleToFace.push_back(static_cast<std::uint32_t>(f));
            }
        }

        if (outTriangleToFace.empty())
        {
            return MeshPackStatus::DegenerateAllFaces;
        }
        return MeshPackStatus::Success;
    }
}
