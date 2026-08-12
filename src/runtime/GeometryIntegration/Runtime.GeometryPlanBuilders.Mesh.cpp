module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.GeometryPlanBuilders;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.VertexChannelStreams;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kMeshDebugName = "Runtime.Mesh";

        [[nodiscard]] MeshPlanBuildResult Failure(
            MeshPackStatus status,
            MeshPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return MeshPlanBuildResult{status, std::nullopt};
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

    MeshPlanBuildResult BuildMeshGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        MeshPackBuffer& outBuffer)
    {
        return BuildMeshGeometryPlan(view, nullptr, request, outBuffer);
    }

    MeshPlanBuildResult BuildMeshGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
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

        std::vector<std::uint32_t> triangleToFace;
        std::vector<std::uint32_t> cornerHalfedges;
        const MeshSurfaceTopologyStatus topology =
            BuildMeshSurfaceTriangleCornerTopology(
                view,
                outBuffer.SurfaceIndices,
                triangleToFace,
                cornerHalfedges);
        if (topology != MeshSurfaceTopologyStatus::Success)
        {
            switch (topology)
            {
            case MeshSurfaceTopologyStatus::WrongDomain:
                return Failure(MeshPackStatus::WrongDomain, outBuffer);
            case MeshSurfaceTopologyStatus::MissingPositions:
                return Failure(MeshPackStatus::MissingPositions, outBuffer);
            case MeshSurfaceTopologyStatus::MissingHalfedgeTopology:
                return Failure(
                    MeshPackStatus::MissingHalfedgeTopology, outBuffer);
            case MeshSurfaceTopologyStatus::MissingFaceTopology:
                return Failure(
                    MeshPackStatus::MissingFaceTopology, outBuffer);
            case MeshSurfaceTopologyStatus::EmptyMesh:
                return Failure(MeshPackStatus::EmptyMesh, outBuffer);
            case MeshSurfaceTopologyStatus::InvalidTopology:
                return Failure(MeshPackStatus::InvalidTopology, outBuffer);
            case MeshSurfaceTopologyStatus::DegenerateAllFaces:
                return Failure(
                    MeshPackStatus::DegenerateAllFaces, outBuffer);
            case MeshSurfaceTopologyStatus::Success:
                break;
            }
        }
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
        const std::optional<AttributeSourceType> normalOverrideType =
            normalOverride != nullptr &&
                    normalOverride->Property.Domain ==
                        GeometryElementDomain::MeshVertex
                ? ToAttributeSourceType(normalOverride->Property.ValueKind)
                : std::nullopt;
        const VertexAttributeBinding normalBinding{
            .Channel = VertexChannel::Normal,
            .SourceType = normalOverrideType.value_or(
                AttributeSourceType::Vec3),
            .SourceProperty = normalOverride != nullptr &&
                                      normalOverrideType ==
                                          AttributeSourceType::Vec3
                ? std::string_view{normalOverride->Property.Name}
                : normalOverride != nullptr
                    ? std::string_view{}
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
                const std::optional<AttributeSourceType> sourceType =
                    channelBindings->Color.Property.Domain ==
                            GeometryElementDomain::MeshVertex
                        ? ToAttributeSourceType(
                              channelBindings->Color.Property.ValueKind)
                        : std::nullopt;
                if (sourceType != AttributeSourceType::Vec3 &&
                    sourceType != AttributeSourceType::Vec4)
                {
                    return;
                }
                outBuffer.PackedColors.resize(vertexCount);
                const VertexAttributeBinding colorBinding{
                    .Channel = VertexChannel::Color,
                    .SourceType = *sourceType,
                    .SourceProperty = std::string_view{
                        channelBindings->Color.Property.Name},
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

        // De-index corner-domain shading attributes for indexed GPU upload.
        // The GPU path has one UV and normal per vertex, while authored seams
        // may carry several values at one mesh vertex. Duplication happens only
        // here, keyed by `(source vertex, UV, normal)`, and never mutates the
        // authoritative mesh. An explicit normal-channel binding remains an
        // explicit override and suppresses canonical `h:normal` resolution.
        std::vector<glm::vec3> splitPositions;
        std::size_t gpuVertexCount = vertexCount;
        std::span<const glm::vec3> positionSpan{positions.data(), positions.size()};

        const auto cornerUvProperty =
            view.HalfedgeSource != nullptr
                ? view.HalfedgeSource->Properties.Get<glm::vec2>("h:texcoord")
                : Geometry::ConstProperty<glm::vec2>{};
        const bool cornerUvsUsable =
            static_cast<bool>(cornerUvProperty) &&
            view.HalfedgeSource != nullptr &&
            cornerUvProperty.Vector().size() ==
                view.HalfedgeSource->Properties.Size();
        const auto cornerNormalProperty =
            view.HalfedgeSource != nullptr && normalOverride == nullptr
                ? view.HalfedgeSource->Properties.Get<glm::vec3>("h:normal")
                : Geometry::ConstProperty<glm::vec3>{};
        const bool cornerNormalsUsable =
            static_cast<bool>(cornerNormalProperty) &&
            view.HalfedgeSource != nullptr &&
            cornerNormalProperty.Vector().size() ==
                view.HalfedgeSource->Properties.Size();
        const bool cornerAttributesUsable =
            (cornerUvsUsable || cornerNormalsUsable) &&
            !cornerHalfedges.empty() &&
            cornerHalfedges.size() == outBuffer.SurfaceIndices.size();

        if (cornerAttributesUsable)
        {
            // The split table itself lives in Runtime.MeshSurfaceTopology so
            // that property-texture bake produces an identical one; the two
            // cross-check through GPU residency.
            MeshCornerAttributeSplit split{};
            if (!BuildMeshCornerAttributeSplit(
                    cornerUvsUsable
                        ? std::span<const glm::vec2>{cornerUvProperty.Vector()}
                        : std::span<const glm::vec2>{},
                    cornerNormalsUsable
                        ? std::span<const glm::vec3>{cornerNormalProperty.Vector()}
                        : std::span<const glm::vec3>{},
                    cornerHalfedges,
                    texcoords,
                    normals,
                    vertexCount,
                    outBuffer.SurfaceIndices,
                    split))
            {
                return Failure(MeshPackStatus::InvalidTopology, outBuffer);
            }

            const bool hasColors = !outBuffer.PackedColors.empty();
            std::vector<std::uint32_t> newColors;
            splitPositions.reserve(split.SourceVertexForSlot.size());
            if (hasColors)
                newColors.reserve(split.SourceVertexForSlot.size());

            for (const std::uint32_t sourceVertex : split.SourceVertexForSlot)
            {
                splitPositions.push_back(positions[sourceVertex]);
                if (hasColors)
                    newColors.push_back(outBuffer.PackedColors[sourceVertex]);
            }

            normals = std::move(split.NormalForSlot);
            texcoords = std::move(split.TexcoordForSlot);
            if (hasColors)
                outBuffer.PackedColors = std::move(newColors);
            positionSpan = std::span<const glm::vec3>{splitPositions};
            gpuVertexCount = splitPositions.size();
        }

        outBuffer.VertexBytes.resize(sizeof(MeshVertex) * gpuVertexCount);
        auto* vData = reinterpret_cast<MeshVertex*>(outBuffer.VertexBytes.data());

        constexpr float kInf = std::numeric_limits<float>::infinity();
        glm::vec3 minP{+kInf, +kInf, +kInf};
        glm::vec3 maxP{-kInf, -kInf, -kInf};

        for (std::size_t i = 0; i < gpuVertexCount; ++i)
        {
            const glm::vec3 p = positionSpan[i];
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

        outBuffer.Channels.SetVertexCount(static_cast<std::uint32_t>(gpuVertexCount));
        SetChannelVec3(
            outBuffer.Channels,
            VertexChannel::Position,
            positionSpan);
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
        desc.VertexCount = static_cast<std::uint32_t>(gpuVertexCount);

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kMeshDebugName;

        return MeshPlanBuildResult{
            MeshPackStatus::Success,
            Graphics::MakeGeometryUploadPlan(
                request.Key,
                request.Generation,
                desc,
                request.UpdateClass,
                request.UpdateChannels),
        };
    }

}
