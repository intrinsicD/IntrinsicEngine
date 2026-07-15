module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.VertexChannelStreams;
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

        [[nodiscard]] MeshPackStatus BuildSurfaceTriangleTopologyImpl(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            std::vector<std::uint32_t>* outSurfaceIndices,
            std::vector<std::uint32_t>* outTriangleToFace)
        {
            using namespace ECS::Components::GeometrySources;

            if (outSurfaceIndices != nullptr)
                outSurfaceIndices->clear();
            if (outTriangleToFace != nullptr)
                outTriangleToFace->clear();

            const auto fail = [&](const MeshPackStatus status)
            {
                if (outSurfaceIndices != nullptr)
                    outSurfaceIndices->clear();
                if (outTriangleToFace != nullptr)
                    outTriangleToFace->clear();
                return status;
            };

            const SourceAvailability availability = BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != Domain::Mesh)
                return fail(MeshPackStatus::WrongDomain);
            if (view.VertexSource == nullptr)
                return fail(MeshPackStatus::MissingPositions);
            const auto positionProperty =
                view.VertexSource->Properties.Get<glm::vec3>(
                    PropertyNames::kPosition);
            if (!positionProperty)
                return fail(MeshPackStatus::MissingPositions);
            const std::uint32_t vertexCount = static_cast<std::uint32_t>(
                positionProperty.Vector().size());
            if (vertexCount == 0u)
                return fail(MeshPackStatus::EmptyMesh);

            if (view.HalfedgeSource == nullptr)
                return fail(MeshPackStatus::MissingHalfedgeTopology);
            const auto toVertexProperty =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeToVertex);
            const auto nextProperty =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeNext);
            const auto faceProperty =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kHalfedgeFace);
            if (!toVertexProperty || !nextProperty || !faceProperty)
                return fail(MeshPackStatus::MissingHalfedgeTopology);
            const auto& toVertex = toVertexProperty.Vector();
            const auto& nextHalfedge = nextProperty.Vector();
            const auto& halfedgeFace = faceProperty.Vector();
            const std::size_t halfedgeCount = toVertex.size();
            if (halfedgeCount == 0u)
                return fail(MeshPackStatus::EmptyMesh);
            if (nextHalfedge.size() != halfedgeCount ||
                halfedgeFace.size() != halfedgeCount)
            {
                return fail(MeshPackStatus::InvalidTopology);
            }

            if (view.FaceSource == nullptr)
                return fail(MeshPackStatus::MissingFaceTopology);
            const auto faceHalfedgeProperty =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    PropertyNames::kFaceHalfedge);
            if (!faceHalfedgeProperty)
                return fail(MeshPackStatus::MissingFaceTopology);
            const auto& faceHalfedge = faceHalfedgeProperty.Vector();
            const std::size_t faceCount = faceHalfedge.size();
            if (faceCount == 0u)
                return fail(MeshPackStatus::EmptyMesh);

            const std::uint32_t faceCountU32 =
                static_cast<std::uint32_t>(faceCount);
            std::vector<std::uint32_t> ringScratch;
            ringScratch.reserve(8u);
            std::size_t emittedTriangleCount = 0u;
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceCount;
                 ++faceIndex)
            {
                const FaceRingOutcome outcome = ProduceFaceRing(
                    faceHalfedge,
                    halfedgeFace,
                    nextHalfedge,
                    toVertex,
                    faceCountU32,
                    vertexCount,
                    faceIndex,
                    ringScratch);
                if (outcome == FaceRingOutcome::Invalid)
                    return fail(MeshPackStatus::InvalidTopology);
                if (outcome == FaceRingOutcome::Skip)
                    continue;

                for (std::size_t ringIndex = 1u;
                     ringIndex + 1u < ringScratch.size();
                     ++ringIndex)
                {
                    if (outSurfaceIndices != nullptr)
                    {
                        outSurfaceIndices->insert(
                            outSurfaceIndices->end(),
                            {ringScratch[0u],
                             ringScratch[ringIndex],
                             ringScratch[ringIndex + 1u]});
                    }
                    if (outTriangleToFace != nullptr)
                    {
                        outTriangleToFace->push_back(
                            static_cast<std::uint32_t>(faceIndex));
                    }
                    ++emittedTriangleCount;
                }
            }

            if (emittedTriangleCount == 0u)
                return fail(MeshPackStatus::DegenerateAllFaces);
            return MeshPackStatus::Success;
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
        Channels = {};
        PackedColors.clear();
        SurfaceIndices.clear();
    }

    MeshPackResult PackMesh(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPackBuffer& outBuffer)
    {
        return PackMesh(view, nullptr, outBuffer);
    }

    MeshPackResult PackMesh(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
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

        // Resolve the normal and texcoord vertex channels through the reusable
        // attribute-binding resolver (RUNTIME-120). Behavior matches the prior
        // inline logic: missing / count-mismatched normals fall back to +Z and
        // are renormalized per element; missing / count-mismatched texcoords
        // fall back to zero, and non-finite texcoords are repaired per element.
        // Position keeps its hard-fail `NonFinitePosition` validation inline.
        std::vector<glm::vec3> normals(vertexCount);
        std::vector<glm::vec2> texcoords(vertexCount);

        const VertexChannelSourceBinding* normalOverride =
            (channelBindings != nullptr && IsVertexChannelBindingEnabled(channelBindings->Normal))
                ? &channelBindings->Normal
                : nullptr;
        const VertexAttributeBinding normalBinding{
            .Channel = VertexChannel::Normal,
            .SourceType = normalOverride != nullptr ? normalOverride->SourceType : AttributeSourceType::Vec3,
            .SourceProperty = normalOverride != nullptr
                ? std::string_view{normalOverride->SourceProperty}
                : std::string_view{PropertyNames::kNormal},
            .AllowFallback = true,
            .Normalize = true,
            .Fallback = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f},
        };
        const VertexAttributeBinding texcoordBinding{
            .Channel = VertexChannel::Texcoord,
            .SourceType = AttributeSourceType::Vec2,
            .SourceProperty = std::string_view{"v:texcoord"},
            .AllowFallback = true,
            .Normalize = false,
            .Fallback = glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        };
        (void)ResolveVec3Channel(
            view.VertexSource->Properties, normalBinding, vertexCountU32, normals);
        (void)ResolveVec2Channel(
            view.VertexSource->Properties, texcoordBinding, vertexCountU32, texcoords);

        const auto resolveColors = [&]() {
            if (channelBindings != nullptr && IsVertexChannelBindingEnabled(channelBindings->Color))
            {
                outBuffer.PackedColors.resize(vertexCount);
                const VertexAttributeBinding colorBinding{
                    .Channel = VertexChannel::Color,
                    .SourceType = channelBindings->Color.SourceType,
                    .SourceProperty = std::string_view{channelBindings->Color.SourceProperty},
                    .AllowFallback = false,
                    .Normalize = false,
                    .Fallback = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                };
                const AttributeBindResult colorResult =
                    ResolveColorChannelPackedUnorm8(
                        view.VertexSource->Properties,
                        colorBinding,
                        vertexCountU32,
                        outBuffer.PackedColors);
                if (!colorResult.Ok())
                {
                    outBuffer.PackedColors.clear();
                }
                return;
            }

            const std::string_view colorName{"v:color"};
            AttributeSourceType colorSourceType = AttributeSourceType::Vec4;
            if (view.VertexSource->Properties.Get<glm::vec4>(colorName))
            {
                colorSourceType = AttributeSourceType::Vec4;
            }
            else if (view.VertexSource->Properties.Get<glm::vec3>(colorName))
            {
                colorSourceType = AttributeSourceType::Vec3;
            }
            else
            {
                return;
            }

            outBuffer.PackedColors.resize(vertexCount);
            const VertexAttributeBinding colorBinding{
                .Channel = VertexChannel::Color,
                .SourceType = colorSourceType,
                .SourceProperty = colorName,
                .AllowFallback = false,
                .Normalize = false,
                .Fallback = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            };
            const AttributeBindResult colorResult =
                ResolveColorChannelPackedUnorm8(
                    view.VertexSource->Properties,
                    colorBinding,
                    vertexCountU32,
                    outBuffer.PackedColors);
            if (!colorResult.Ok())
            {
                outBuffer.PackedColors.clear();
            }
        };
        resolveColors();

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
            const glm::vec2 uv = texcoords[i];
            const glm::vec3 n = normals[i];
            vData[i] = MeshVertex{p.x, p.y, p.z, uv.x, uv.y, n.x, n.y, n.z};
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        outBuffer.Channels.SetVertexCount(vertexCountU32);
        SetChannelVec3(
            outBuffer.Channels,
            VertexChannel::Position,
            std::span<const glm::vec3>{positions.data(), positions.size()});
        SetChannelVec2(
            outBuffer.Channels,
            VertexChannel::Texcoord,
            std::span<const glm::vec2>{texcoords.data(), texcoords.size()});
        SetChannelVec3(
            outBuffer.Channels,
            VertexChannel::Normal,
            std::span<const glm::vec3>{normals.data(), normals.size()});
        if (!outBuffer.PackedColors.empty())
        {
            SetChannelPackedUnorm8(
                outBuffer.Channels,
                VertexChannel::Color,
                std::span<const std::uint32_t>{outBuffer.PackedColors});
        }

        const auto channelBytes = [&outBuffer](const VertexChannel channel) -> std::span<const std::byte> {
            const VertexChannelStreams::Stream* stream = outBuffer.Channels.Find(channel);
            return stream != nullptr ? std::span<const std::byte>{stream->Bytes}
                                     : std::span<const std::byte>{};
        };

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.PositionBytes = channelBytes(VertexChannel::Position);
        desc.TexcoordBytes = channelBytes(VertexChannel::Texcoord);
        desc.NormalBytes = channelBytes(VertexChannel::Normal);
        desc.PackedVertexColors = std::span<const std::uint32_t>{outBuffer.PackedColors};
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
        return BuildSurfaceTriangleTopologyImpl(
            view,
            nullptr,
            &outTriangleToFace);
    }

    MeshPackStatus BuildSurfaceTriangleTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace)
    {
        return BuildSurfaceTriangleTopologyImpl(
            view,
            &outSurfaceIndices,
            &outTriangleToFace);
    }
}
