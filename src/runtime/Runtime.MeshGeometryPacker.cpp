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
            case MeshPackStatus::DegenerateAllFaces:      return "Mesh.DegenerateAllFaces";
        }
        return "Mesh.Unknown";
    }

    MeshPackResult PackMesh(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        if (view.ActiveDomain != Domain::Mesh)
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
        if (!toVertexProp || !nextProp)
        {
            return Failure(MeshPackStatus::MissingHalfedgeTopology, outBuffer);
        }
        const auto& toVertex = toVertexProp.Vector();
        const auto& nextHe = nextProp.Vector();
        const std::size_t halfedgeCount = toVertex.size();
        if (halfedgeCount == 0)
        {
            return Failure(MeshPackStatus::EmptyMesh, outBuffer);
        }
        if (nextHe.size() != halfedgeCount)
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

        for (std::size_t f = 0; f < faceCount; ++f)
        {
            const std::uint32_t first = faceHe[f];
            if (first == kInvalidIndex)
            {
                continue; // boundary / deleted face slot
            }
            if (first >= halfedgeCount)
            {
                return Failure(MeshPackStatus::InvalidTopology, outBuffer);
            }

            ringScratch.clear();
            std::uint32_t h = first;
            for (std::size_t step = 0; step <= halfedgeCount; ++step)
            {
                if (h >= halfedgeCount)
                {
                    return Failure(MeshPackStatus::InvalidTopology, outBuffer);
                }
                const std::uint32_t targetV = toVertex[h];
                if (targetV >= vertexCount)
                {
                    return Failure(MeshPackStatus::InvalidTopology, outBuffer);
                }
                ringScratch.push_back(targetV);

                const std::uint32_t nh = nextHe[h];
                if (nh == first)
                {
                    h = nh;
                    break;
                }
                if (nh == kInvalidIndex)
                {
                    return Failure(MeshPackStatus::InvalidTopology, outBuffer);
                }
                if (step == halfedgeCount)
                {
                    // ring did not close within the halfedge count: a malformed
                    // `h:next` cycle would loop forever here.
                    return Failure(MeshPackStatus::InvalidTopology, outBuffer);
                }
                h = nh;
            }

            if (ringScratch.size() < 3)
            {
                continue; // degenerate / sub-triangular face
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
            vData[i] = MeshVertex{p.x, p.y, p.z, 0.0f, 0.0f};
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
}
