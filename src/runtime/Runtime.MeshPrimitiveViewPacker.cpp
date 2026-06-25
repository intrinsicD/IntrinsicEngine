module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshPrimitiveViewPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelStreams;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kEdgeViewDebugName = "Runtime.MeshEdgeView";
        constexpr const char* kVertexViewDebugName = "Runtime.MeshVertexView";
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] MeshPrimitiveViewResult Failure(MeshPrimitiveViewStatus status,
                                                      MeshPrimitiveViewBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return MeshPrimitiveViewResult{status, std::nullopt};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        enum class FaceRingOutcome : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] FaceRingOutcome ProduceFaceRing(
            const std::vector<std::uint32_t>& faceHe,
            const std::vector<std::uint32_t>& halfedgeFace,
            const std::vector<std::uint32_t>& nextHe,
            const std::vector<std::uint32_t>& toVertex,
            const std::uint32_t faceCountU32,
            const std::uint32_t vertexCount,
            const std::size_t f,
            std::vector<std::uint32_t>& outRing)
        {
            outRing.clear();

            const std::size_t halfedgeCount = toVertex.size();
            const std::uint32_t first = faceHe[f];
            if (first == kInvalidIndex)
            {
                return FaceRingOutcome::Skip;
            }
            if (first >= halfedgeCount)
            {
                return FaceRingOutcome::Invalid;
            }

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
                if (halfedgeFace[h] != static_cast<std::uint32_t>(f))
                {
                    return FaceRingOutcome::Invalid;
                }
                const std::uint32_t targetV = toVertex[h];
                if (targetV >= vertexCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                outRing.push_back(targetV);

                const std::uint32_t next = nextHe[h];
                if (next == first)
                {
                    break;
                }
                if (next == kInvalidIndex || step == halfedgeCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                h = next;
            }

            return outRing.size() < 3u
                ? FaceRingOutcome::Skip
                : FaceRingOutcome::Triangulate;
        }

        // Resolve and validate the mesh vertex positions shared by both views.
        // On success, returns the positions span; on failure, fills `status`
        // (the caller turns it into a `Failure`).
        [[nodiscard]] const std::vector<glm::vec3>* ResolvePositions(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            MeshPrimitiveViewStatus& status) noexcept
        {
            using namespace ECS::Components::GeometrySources;

            const SourceAvailability availability = BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != Domain::Mesh)
            {
                status = MeshPrimitiveViewStatus::WrongDomain;
                return nullptr;
            }
            if (view.VertexSource == nullptr)
            {
                status = MeshPrimitiveViewStatus::MissingPositions;
                return nullptr;
            }
            const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
            if (!posProp)
            {
                status = MeshPrimitiveViewStatus::MissingPositions;
                return nullptr;
            }
            const auto& positions = posProp.Vector();
            if (positions.empty())
            {
                status = MeshPrimitiveViewStatus::EmptyMesh;
                return nullptr;
            }
            status = MeshPrimitiveViewStatus::Success;
            return &positions;
        }

        // Write the shared vertex buffer from `positions`, validating finiteness
        // and accumulating the local AABB. Returns false (and fills `status`)
        // on a non-finite position.
        [[nodiscard]] bool WriteVertexBuffer(
            const std::vector<glm::vec3>& positions,
            MeshPrimitiveViewBuffer& outBuffer,
            glm::vec3& minP,
            glm::vec3& maxP,
            MeshPrimitiveViewStatus& status) noexcept
        {
            const std::size_t vertexCount = positions.size();
            outBuffer.VertexBytes.resize(sizeof(MeshPrimitiveVertex) * vertexCount);
            auto* vData = reinterpret_cast<MeshPrimitiveVertex*>(outBuffer.VertexBytes.data());

            constexpr float kInf = std::numeric_limits<float>::infinity();
            minP = glm::vec3{+kInf, +kInf, +kInf};
            maxP = glm::vec3{-kInf, -kInf, -kInf};

            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                const glm::vec3 p = positions[i];
                if (!IsFinite(p))
                {
                    status = MeshPrimitiveViewStatus::NonFinitePosition;
                    return false;
                }
                vData[i] = MeshPrimitiveVertex{p.x, p.y, p.z, 0.0f, 0.0f};
                minP = glm::min(minP, p);
                maxP = glm::max(maxP, p);
            }
            status = MeshPrimitiveViewStatus::Success;
            return true;
        }

        void PopulateChannelStreams(const std::vector<glm::vec3>& positions,
                                    MeshPrimitiveViewBuffer& outBuffer)
        {
            const auto vertexCountU32 = static_cast<std::uint32_t>(positions.size());
            std::vector<glm::vec2> texcoords(positions.size(), glm::vec2{0.0f, 0.0f});
            outBuffer.Channels.SetVertexCount(vertexCountU32);
            SetChannelVec3(
                outBuffer.Channels,
                VertexChannel::Position,
                std::span<const glm::vec3>{positions.data(), positions.size()});
            SetChannelVec2(
                outBuffer.Channels,
                VertexChannel::Texcoord,
                std::span<const glm::vec2>{texcoords.data(), texcoords.size()});
        }

        [[nodiscard]] std::span<const std::byte> ChannelBytes(
            const MeshPrimitiveViewBuffer& outBuffer,
            const VertexChannel channel) noexcept
        {
            const VertexChannelStreams::Stream* stream = outBuffer.Channels.Find(channel);
            return stream != nullptr ? std::span<const std::byte>{stream->Bytes}
                                     : std::span<const std::byte>{};
        }

        enum class SurfaceWireEdgeOutcome : std::uint8_t
        {
            Emitted,
            NoTopology,
            Empty,
            Invalid,
        };

        [[nodiscard]] SurfaceWireEdgeOutcome AppendSurfaceWireEdges(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const std::uint32_t vertexCountU32,
            MeshPrimitiveViewBuffer& outBuffer)
        {
            using namespace ECS::Components::GeometrySources;

            if (view.HalfedgeSource == nullptr || view.FaceSource == nullptr)
            {
                return SurfaceWireEdgeOutcome::NoTopology;
            }
            const auto toVertexProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeToVertex);
            const auto nextProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeNext);
            const auto faceProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeFace);
            const auto faceHeProp = view.FaceSource->Properties.Get<std::uint32_t>(
                PropertyNames::kFaceHalfedge);
            if (!toVertexProp || !nextProp || !faceProp || !faceHeProp)
            {
                return SurfaceWireEdgeOutcome::NoTopology;
            }

            const auto& toVertex = toVertexProp.Vector();
            const auto& nextHe = nextProp.Vector();
            const auto& halfedgeFace = faceProp.Vector();
            const auto& faceHe = faceHeProp.Vector();
            const std::size_t halfedgeCount = toVertex.size();
            const std::size_t faceCount = faceHe.size();
            if (halfedgeCount == 0u || faceCount == 0u)
            {
                return SurfaceWireEdgeOutcome::Empty;
            }
            if (nextHe.size() != halfedgeCount ||
                halfedgeFace.size() != halfedgeCount ||
                faceCount > std::numeric_limits<std::uint32_t>::max())
            {
                return SurfaceWireEdgeOutcome::Invalid;
            }

            std::unordered_set<std::uint64_t> seenEdges;
            seenEdges.reserve(faceCount * 4u);
            std::vector<std::uint32_t> ringScratch;
            ringScratch.reserve(8u);

            const auto faceCountU32 = static_cast<std::uint32_t>(faceCount);
            for (std::size_t f = 0; f < faceCount; ++f)
            {
                const FaceRingOutcome outcome =
                    ProduceFaceRing(faceHe,
                                    halfedgeFace,
                                    nextHe,
                                    toVertex,
                                    faceCountU32,
                                    vertexCountU32,
                                    f,
                                    ringScratch);
                if (outcome == FaceRingOutcome::Skip)
                {
                    continue;
                }
                if (outcome == FaceRingOutcome::Invalid)
                {
                    return SurfaceWireEdgeOutcome::Invalid;
                }

                for (std::size_t i = 0; i < ringScratch.size(); ++i)
                {
                    const std::uint32_t a = ringScratch[i];
                    const std::uint32_t b = ringScratch[(i + 1u) % ringScratch.size()];
                    if (a == b)
                    {
                        continue;
                    }
                    const std::uint32_t lo = a < b ? a : b;
                    const std::uint32_t hi = a < b ? b : a;
                    const std::uint64_t key =
                        (static_cast<std::uint64_t>(lo) << 32u) |
                        static_cast<std::uint64_t>(hi);
                    if (seenEdges.insert(key).second)
                    {
                        outBuffer.LineIndices.push_back(a);
                        outBuffer.LineIndices.push_back(b);
                    }
                }
            }

            return outBuffer.LineIndices.empty()
                ? SurfaceWireEdgeOutcome::Empty
                : SurfaceWireEdgeOutcome::Emitted;
        }

        void FillLocalSphere(Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& desc,
                             const glm::vec3& minP,
                             const glm::vec3& maxP) noexcept
        {
            const glm::vec3 center = 0.5f * (minP + maxP);
            const float radius = 0.5f * glm::length(maxP - minP);
            desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        }
    }

    const char* DebugNameForMeshPrimitiveViewStatus(MeshPrimitiveViewStatus status) noexcept
    {
        switch (status)
        {
            case MeshPrimitiveViewStatus::Success:             return "MeshPrimitiveView.Success";
            case MeshPrimitiveViewStatus::WrongDomain:         return "MeshPrimitiveView.WrongDomain";
            case MeshPrimitiveViewStatus::MissingPositions:    return "MeshPrimitiveView.MissingPositions";
            case MeshPrimitiveViewStatus::EmptyMesh:           return "MeshPrimitiveView.EmptyMesh";
            case MeshPrimitiveViewStatus::MissingEdgeTopology: return "MeshPrimitiveView.MissingEdgeTopology";
            case MeshPrimitiveViewStatus::InvalidEdge:         return "MeshPrimitiveView.InvalidEdge";
            case MeshPrimitiveViewStatus::NonFinitePosition:   return "MeshPrimitiveView.NonFinitePosition";
        }
        return "MeshPrimitiveView.Unknown";
    }

    void MeshPrimitiveViewBuffer::Clear() noexcept
    {
        VertexBytes.clear();
        Channels = {};
        LineIndices.clear();
    }

    MeshPrimitiveViewResult PackMeshEdgeView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        MeshPrimitiveViewStatus status = MeshPrimitiveViewStatus::Success;
        const std::vector<glm::vec3>* positions = ResolvePositions(view, status);
        if (positions == nullptr)
        {
            return Failure(status, outBuffer);
        }
        const std::size_t vertexCount = positions->size();

        const auto vertexCountU32 = static_cast<std::uint32_t>(vertexCount);
        bool hasExplicitEdgeTopology = false;
        bool usedExplicitEdges = false;
        if (view.EdgeSource != nullptr)
        {
            const auto v0Prop = view.EdgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kEdgeV0);
            const auto v1Prop = view.EdgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kEdgeV1);
            if (v0Prop || v1Prop)
            {
                if (!v0Prop || !v1Prop)
                {
                    return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
                }
                const auto& v0 = v0Prop.Vector();
                const auto& v1 = v1Prop.Vector();
                if (v0.size() != v1.size())
                {
                    return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
                }

                hasExplicitEdgeTopology = true;
                if (!v0.empty())
                {
                    outBuffer.LineIndices.reserve(v0.size() * 2u);
                    for (std::size_t e = 0; e < v0.size(); ++e)
                    {
                        if (v0[e] >= vertexCountU32 || v1[e] >= vertexCountU32)
                        {
                            return Failure(MeshPrimitiveViewStatus::InvalidEdge, outBuffer);
                        }
                        outBuffer.LineIndices.push_back(v0[e]);
                        outBuffer.LineIndices.push_back(v1[e]);
                    }
                    usedExplicitEdges = true;
                }
            }
        }

        if (!usedExplicitEdges)
        {
            const std::size_t priorLineCount = outBuffer.LineIndices.size();
            const SurfaceWireEdgeOutcome derived =
                AppendSurfaceWireEdges(view, vertexCountU32, outBuffer);
            if (derived == SurfaceWireEdgeOutcome::Invalid)
            {
                outBuffer.LineIndices.resize(priorLineCount);
                if (!hasExplicitEdgeTopology)
                {
                    return Failure(MeshPrimitiveViewStatus::InvalidEdge, outBuffer);
                }
            }
            else if (derived != SurfaceWireEdgeOutcome::Emitted &&
                     !hasExplicitEdgeTopology)
            {
                return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
            }
        }

        glm::vec3 minP{};
        glm::vec3 maxP{};
        if (!WriteVertexBuffer(*positions, outBuffer, minP, maxP, status))
        {
            return Failure(status, outBuffer);
        }
        PopulateChannelStreams(*positions, outBuffer);

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.PositionBytes = ChannelBytes(outBuffer, VertexChannel::Position);
        desc.TexcoordBytes = ChannelBytes(outBuffer, VertexChannel::Texcoord);
        desc.SurfaceIndices = {};
        desc.LineIndices = std::span<const std::uint32_t>{outBuffer.LineIndices};
        desc.VertexCount = vertexCountU32;
        FillLocalSphere(desc, minP, maxP);
        desc.DebugName = kEdgeViewDebugName;

        return MeshPrimitiveViewResult{MeshPrimitiveViewStatus::Success, desc};
    }

    MeshPrimitiveViewResult PackMeshVertexView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer)
    {
        outBuffer.Clear();

        MeshPrimitiveViewStatus status = MeshPrimitiveViewStatus::Success;
        const std::vector<glm::vec3>* positions = ResolvePositions(view, status);
        if (positions == nullptr)
        {
            return Failure(status, outBuffer);
        }

        glm::vec3 minP{};
        glm::vec3 maxP{};
        if (!WriteVertexBuffer(*positions, outBuffer, minP, maxP, status))
        {
            return Failure(status, outBuffer);
        }
        PopulateChannelStreams(*positions, outBuffer);

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.PositionBytes = ChannelBytes(outBuffer, VertexChannel::Position);
        desc.TexcoordBytes = ChannelBytes(outBuffer, VertexChannel::Texcoord);
        desc.SurfaceIndices = {};
        desc.LineIndices = {};
        desc.VertexCount = static_cast<std::uint32_t>(positions->size());
        FillLocalSphere(desc, minP, maxP);
        desc.DebugName = kVertexViewDebugName;

        return MeshPrimitiveViewResult{MeshPrimitiveViewStatus::Success, desc};
    }
}
