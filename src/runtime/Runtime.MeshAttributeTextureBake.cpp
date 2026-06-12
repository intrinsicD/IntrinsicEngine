module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshAttributeTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
        constexpr const char* kDefaultNormalProperty = "v:normal";
        constexpr const char* kDefaultColorProperty = "v:color";

        enum class BakeKind : std::uint8_t
        {
            Normal,
            Color,
        };

        [[nodiscard]] MeshAttributeTextureBakeResult Failure(
            const MeshAttributeTextureBakeStatus status)
        {
            return MeshAttributeTextureBakeResult{.Status = status, .Payload = {}};
        }

        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] std::byte ToByte(const float value) noexcept
        {
            const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
            return static_cast<std::byte>(static_cast<std::uint32_t>(std::round(scaled)));
        }

        [[nodiscard]] std::array<std::byte, 4> EncodeNormal(glm::vec3 normal) noexcept
        {
            const float len = glm::length(normal);
            if (std::isfinite(len) && len > 1.0e-6f)
            {
                normal /= len;
            }
            else
            {
                normal = glm::vec3{0.0f, 0.0f, 1.0f};
            }

            return {
                ToByte(normal.x * 0.5f + 0.5f),
                ToByte(normal.y * 0.5f + 0.5f),
                ToByte(normal.z * 0.5f + 0.5f),
                std::byte{0xFF},
            };
        }

        [[nodiscard]] std::array<std::byte, 4> EncodeColor(const glm::vec4 color) noexcept
        {
            return {
                ToByte(color.r),
                ToByte(color.g),
                ToByte(color.b),
                ToByte(color.a),
            };
        }

        [[nodiscard]] float EdgeFunction(
            const glm::vec2 a,
            const glm::vec2 b,
            const glm::vec2 p) noexcept
        {
            return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        }

        struct AttributeSample
        {
            glm::vec4 Value{0.0f};
        };

        struct BakeInput
        {
            std::vector<glm::vec2> Texcoords{};
            std::vector<AttributeSample> Values{};
            std::vector<std::uint32_t> SurfaceIndices{};
            std::string SourcePropertyName{};
        };

        void WritePixel(
            Assets::AssetTexture2DPayload& payload,
            const std::uint32_t width,
            const std::uint32_t x,
            const std::uint32_t y,
            const std::array<std::byte, 4>& rgba)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4u;
            payload.PixelBytes[offset + 0u] = rgba[0];
            payload.PixelBytes[offset + 1u] = rgba[1];
            payload.PixelBytes[offset + 2u] = rgba[2];
            payload.PixelBytes[offset + 3u] = rgba[3];
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus Rasterize(
            const BakeKind kind,
            const BakeInput& input,
            const MeshAttributeTextureBakeOptions& options,
            Assets::AssetTexture2DPayload& payload)
        {
            const std::uint32_t width = options.Width;
            const std::uint32_t height = options.Height;
            bool wroteAny = false;

            for (std::size_t tri = 0u; tri + 2u < input.SurfaceIndices.size(); tri += 3u)
            {
                const std::uint32_t ia = input.SurfaceIndices[tri + 0u];
                const std::uint32_t ib = input.SurfaceIndices[tri + 1u];
                const std::uint32_t ic = input.SurfaceIndices[tri + 2u];
                if (ia >= input.Texcoords.size() ||
                    ib >= input.Texcoords.size() ||
                    ic >= input.Texcoords.size() ||
                    ia >= input.Values.size() ||
                    ib >= input.Values.size() ||
                    ic >= input.Values.size())
                {
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }

                const glm::vec2 uvA = input.Texcoords[ia];
                const glm::vec2 uvB = input.Texcoords[ib];
                const glm::vec2 uvC = input.Texcoords[ic];
                if (!IsFinite(uvA) || !IsFinite(uvB) || !IsFinite(uvC))
                {
                    return MeshAttributeTextureBakeStatus::NonFiniteTexcoord;
                }

                const glm::vec2 pA = glm::vec2{uvA.x * static_cast<float>(width - 1u),
                                               uvA.y * static_cast<float>(height - 1u)};
                const glm::vec2 pB = glm::vec2{uvB.x * static_cast<float>(width - 1u),
                                               uvB.y * static_cast<float>(height - 1u)};
                const glm::vec2 pC = glm::vec2{uvC.x * static_cast<float>(width - 1u),
                                               uvC.y * static_cast<float>(height - 1u)};

                const float area = EdgeFunction(pA, pB, pC);
                if (std::abs(area) <= 1.0e-6f || !std::isfinite(area))
                {
                    continue;
                }

                const float minXf = std::floor(std::min({pA.x, pB.x, pC.x}));
                const float maxXf = std::ceil(std::max({pA.x, pB.x, pC.x}));
                const float minYf = std::floor(std::min({pA.y, pB.y, pC.y}));
                const float maxYf = std::ceil(std::max({pA.y, pB.y, pC.y}));

                const std::uint32_t minX = static_cast<std::uint32_t>(
                    std::clamp(minXf, 0.0f, static_cast<float>(width - 1u)));
                const std::uint32_t maxX = static_cast<std::uint32_t>(
                    std::clamp(maxXf, 0.0f, static_cast<float>(width - 1u)));
                const std::uint32_t minY = static_cast<std::uint32_t>(
                    std::clamp(minYf, 0.0f, static_cast<float>(height - 1u)));
                const std::uint32_t maxY = static_cast<std::uint32_t>(
                    std::clamp(maxYf, 0.0f, static_cast<float>(height - 1u)));

                for (std::uint32_t y = minY; y <= maxY; ++y)
                {
                    for (std::uint32_t x = minX; x <= maxX; ++x)
                    {
                        const glm::vec2 p{
                            static_cast<float>(x),
                            static_cast<float>(y),
                        };
                        const float w0 = EdgeFunction(pB, pC, p) / area;
                        const float w1 = EdgeFunction(pC, pA, p) / area;
                        const float w2 = EdgeFunction(pA, pB, p) / area;
                        constexpr float kEpsilon = -1.0e-5f;
                        if (w0 < kEpsilon || w1 < kEpsilon || w2 < kEpsilon)
                        {
                            continue;
                        }

                        const glm::vec4 value =
                            input.Values[ia].Value * w0 +
                            input.Values[ib].Value * w1 +
                            input.Values[ic].Value * w2;
                        if (!IsFinite(value))
                        {
                            return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                        }

                        WritePixel(
                            payload,
                            width,
                            x,
                            y,
                            kind == BakeKind::Normal
                                ? EncodeNormal(glm::vec3{value})
                                : EncodeColor(value));
                        wroteAny = true;
                    }
                }
            }

            return wroteAny
                ? MeshAttributeTextureBakeStatus::Success
                : MeshAttributeTextureBakeStatus::DegenerateAllTriangles;
        }

        [[nodiscard]] Assets::AssetTexture2DPayload MakePayloadShell(
            const BakeKind kind,
            const MeshAttributeTextureBakeOptions& options,
            const std::string& propertyName)
        {
            Assets::AssetTexture2DPayload payload{};
            payload.Metadata.Width = options.Width;
            payload.Metadata.Height = options.Height;
            payload.Metadata.Components = 4u;
            payload.Metadata.PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm;
            payload.Metadata.ColorSpace = kind == BakeKind::Color
                ? Assets::AssetTextureColorSpace::SRGB
                : Assets::AssetTextureColorSpace::Linear;
            payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::Generated;
            payload.Metadata.SourceFormat = Assets::AssetFileFormat::Unknown;
            payload.Metadata.SourcePath = propertyName;
            payload.Metadata.DebugName = options.DebugName.empty()
                ? propertyName
                : options.DebugName;
            payload.PixelBytes.resize(
                static_cast<std::size_t>(options.Width) * options.Height * 4u);

            const std::array<std::byte, 4> initial = kind == BakeKind::Normal
                ? EncodeNormal(glm::vec3{0.0f, 0.0f, 1.0f})
                : EncodeColor(glm::vec4{1.0f});
            for (std::uint32_t y = 0u; y < options.Height; ++y)
            {
                for (std::uint32_t x = 0u; x < options.Width; ++x)
                {
                    WritePixel(payload, options.Width, x, y, initial);
                }
            }

            return payload;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus ValidateBakeOptions(
            const MeshAttributeTextureBakeOptions& options) noexcept
        {
            if (options.Width == 0u || options.Height == 0u)
            {
                return MeshAttributeTextureBakeStatus::InvalidResolution;
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        // Outcome of walking one face slot's halfedge ring.
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
            const std::size_t f,
            const std::uint32_t faceCount,
            const std::uint32_t vertexCount,
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
            if (firstOwner == kInvalidIndex || firstOwner >= faceCount)
            {
                return FaceRingOutcome::Skip;
            }
            if (firstOwner != static_cast<std::uint32_t>(f))
            {
                return FaceRingOutcome::Skip;
            }

            std::uint32_t h = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (h >= halfedgeCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                if (halfedgeFace[h] != static_cast<std::uint32_t>(f))
                {
                    return FaceRingOutcome::Invalid;
                }
                const std::uint32_t v = toVertex[h];
                if (v >= vertexCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                outRing.push_back(v);

                const std::uint32_t next = nextHe[h];
                if (next == first)
                {
                    break;
                }
                if (next == kInvalidIndex)
                {
                    return FaceRingOutcome::Invalid;
                }
                if (step == halfedgeCount)
                {
                    return FaceRingOutcome::Invalid;
                }
                h = next;
            }

            return outRing.size() >= 3u
                ? FaceRingOutcome::Triangulate
                : FaceRingOutcome::Skip;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus BuildSurfaceIndices(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outIndices)
        {
            using namespace ECS::Components::GeometrySources;

            if (view.HalfedgeSource == nullptr)
            {
                return MeshAttributeTextureBakeStatus::MissingHalfedgeTopology;
            }
            const auto toVertexProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeToVertex);
            const auto nextProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeNext);
            const auto faceProp = view.HalfedgeSource->Properties.Get<std::uint32_t>(
                PropertyNames::kHalfedgeFace);
            if (!toVertexProp || !nextProp || !faceProp)
            {
                return MeshAttributeTextureBakeStatus::MissingHalfedgeTopology;
            }

            const auto& toVertex = toVertexProp.Vector();
            const auto& nextHe = nextProp.Vector();
            const auto& halfedgeFace = faceProp.Vector();
            if (toVertex.empty())
            {
                return MeshAttributeTextureBakeStatus::EmptyMesh;
            }
            if (nextHe.size() != toVertex.size() || halfedgeFace.size() != toVertex.size())
            {
                return MeshAttributeTextureBakeStatus::InvalidTopology;
            }

            if (view.FaceSource == nullptr)
            {
                return MeshAttributeTextureBakeStatus::MissingFaceTopology;
            }
            const auto faceHeProp = view.FaceSource->Properties.Get<std::uint32_t>(
                PropertyNames::kFaceHalfedge);
            if (!faceHeProp)
            {
                return MeshAttributeTextureBakeStatus::MissingFaceTopology;
            }
            const auto& faceHe = faceHeProp.Vector();
            if (faceHe.empty())
            {
                return MeshAttributeTextureBakeStatus::EmptyMesh;
            }
            if (faceHe.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return MeshAttributeTextureBakeStatus::InvalidTopology;
            }

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            outIndices.clear();
            for (std::size_t f = 0u; f < faceHe.size(); ++f)
            {
                const FaceRingOutcome outcome = ProduceFaceRing(
                    faceHe,
                    halfedgeFace,
                    nextHe,
                    toVertex,
                    f,
                    static_cast<std::uint32_t>(faceHe.size()),
                    vertexCount,
                    ring);
                if (outcome == FaceRingOutcome::Invalid)
                {
                    outIndices.clear();
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }
                if (outcome == FaceRingOutcome::Skip)
                {
                    continue;
                }
                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    outIndices.push_back(ring[0u]);
                    outIndices.push_back(ring[i]);
                    outIndices.push_back(ring[i + 1u]);
                }
            }

            return outIndices.empty()
                ? MeshAttributeTextureBakeStatus::DegenerateAllTriangles
                : MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus BuildSurfaceIndices(
            const Geometry::HalfedgeMesh::Mesh& mesh,
            std::vector<std::uint32_t>& outIndices)
        {
            outIndices.clear();
            if (mesh.VerticesSize() == 0u || mesh.HalfedgesSize() == 0u || mesh.FacesSize() == 0u)
            {
                return MeshAttributeTextureBakeStatus::EmptyMesh;
            }

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u; faceIndex < mesh.FacesSize(); ++faceIndex)
            {
                const Geometry::FaceHandle face{
                    static_cast<Geometry::PropertyIndex>(faceIndex)};
                if (mesh.IsDeleted(face))
                {
                    continue;
                }

                ring.clear();
                const Geometry::HalfedgeHandle first = mesh.Halfedge(face);
                if (!first.IsValid() || !mesh.IsValid(first))
                {
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }

                Geometry::HalfedgeHandle h = first;
                for (std::size_t step = 0u; step <= mesh.HalfedgesSize(); ++step)
                {
                    if (!h.IsValid() || !mesh.IsValid(h))
                    {
                        return MeshAttributeTextureBakeStatus::InvalidTopology;
                    }
                    if (mesh.Face(h) != face)
                    {
                        return MeshAttributeTextureBakeStatus::InvalidTopology;
                    }
                    const Geometry::VertexHandle vertex = mesh.ToVertex(h);
                    if (!vertex.IsValid() || !mesh.IsValid(vertex))
                    {
                        return MeshAttributeTextureBakeStatus::InvalidTopology;
                    }
                    ring.push_back(vertex.Index);

                    const Geometry::HalfedgeHandle next = mesh.NextHalfedge(h);
                    if (next == first)
                    {
                        break;
                    }
                    if (!next.IsValid())
                    {
                        return MeshAttributeTextureBakeStatus::InvalidTopology;
                    }
                    if (step == mesh.HalfedgesSize())
                    {
                        return MeshAttributeTextureBakeStatus::InvalidTopology;
                    }
                    h = next;
                }

                if (ring.size() < 3u)
                {
                    continue;
                }

                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    outIndices.push_back(ring[0u]);
                    outIndices.push_back(ring[i]);
                    outIndices.push_back(ring[i + 1u]);
                }
            }

            return outIndices.empty()
                ? MeshAttributeTextureBakeStatus::DegenerateAllTriangles
                : MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillNormalValues(
            const Geometry::ConstPropertySet& properties,
            const std::string& propertyName,
            const std::size_t vertexCount,
            std::vector<AttributeSample>& outValues)
        {
            const auto normals = properties.Get<glm::vec3>(propertyName);
            if (!normals)
            {
                return properties.Exists(propertyName)
                    ? MeshAttributeTextureBakeStatus::UnsupportedPropertyType
                    : MeshAttributeTextureBakeStatus::MissingProperty;
            }
            if (normals.Vector().size() != vertexCount)
            {
                return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
            }

            outValues.resize(vertexCount);
            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                if (!IsFinite(normals.Vector()[i]))
                {
                    return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                }
                outValues[i].Value = glm::vec4{normals.Vector()[i], 0.0f};
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillColorValues(
            const Geometry::ConstPropertySet& properties,
            const std::string& propertyName,
            const std::size_t vertexCount,
            std::vector<AttributeSample>& outValues)
        {
            const auto colors4 = properties.Get<glm::vec4>(propertyName);
            if (colors4)
            {
                if (colors4.Vector().size() != vertexCount)
                {
                    return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
                }
                outValues.resize(vertexCount);
                for (std::size_t i = 0u; i < vertexCount; ++i)
                {
                    if (!IsFinite(colors4.Vector()[i]))
                    {
                        return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                    }
                    outValues[i].Value = colors4.Vector()[i];
                }
                return MeshAttributeTextureBakeStatus::Success;
            }

            const auto colors3 = properties.Get<glm::vec3>(propertyName);
            if (colors3)
            {
                if (colors3.Vector().size() != vertexCount)
                {
                    return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
                }
                outValues.resize(vertexCount);
                for (std::size_t i = 0u; i < vertexCount; ++i)
                {
                    if (!IsFinite(colors3.Vector()[i]))
                    {
                        return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                    }
                    outValues[i].Value = glm::vec4{colors3.Vector()[i], 1.0f};
                }
                return MeshAttributeTextureBakeStatus::Success;
            }

            return properties.Exists(propertyName)
                ? MeshAttributeTextureBakeStatus::UnsupportedPropertyType
                : MeshAttributeTextureBakeStatus::MissingProperty;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillTexcoords(
            const Geometry::ConstPropertySet& properties,
            const std::string& propertyName,
            const std::size_t vertexCount,
            std::vector<glm::vec2>& outTexcoords)
        {
            const auto texcoords = properties.Get<glm::vec2>(propertyName);
            if (!texcoords)
            {
                return MeshAttributeTextureBakeStatus::MissingTexcoords;
            }
            if (texcoords.Vector().size() != vertexCount)
            {
                return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
            }
            outTexcoords = texcoords.Vector();
            for (const glm::vec2 texcoord : outTexcoords)
            {
                if (!IsFinite(texcoord))
                {
                    return MeshAttributeTextureBakeStatus::NonFiniteTexcoord;
                }
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromProperties(
            const BakeKind kind,
            const Geometry::ConstPropertySet& vertexProperties,
            const std::size_t vertexCount,
            std::vector<std::uint32_t> surfaceIndices,
            MeshAttributeTextureBakeOptions options)
        {
            if (const MeshAttributeTextureBakeStatus optionStatus =
                    ValidateBakeOptions(options);
                optionStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(optionStatus);
            }

            if (vertexCount == 0u)
            {
                return Failure(MeshAttributeTextureBakeStatus::EmptyMesh);
            }

            if (options.SourcePropertyName.empty())
            {
                options.SourcePropertyName =
                    kind == BakeKind::Normal ? kDefaultNormalProperty : kDefaultColorProperty;
            }

            BakeInput input{};
            input.SurfaceIndices = std::move(surfaceIndices);
            input.SourcePropertyName = options.SourcePropertyName;

            if (const MeshAttributeTextureBakeStatus texStatus =
                    FillTexcoords(
                        vertexProperties,
                        options.TexcoordPropertyName,
                        vertexCount,
                        input.Texcoords);
                texStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(texStatus);
            }

            const MeshAttributeTextureBakeStatus valueStatus = kind == BakeKind::Normal
                ? FillNormalValues(
                    vertexProperties,
                    options.SourcePropertyName,
                    vertexCount,
                    input.Values)
                : FillColorValues(
                    vertexProperties,
                    options.SourcePropertyName,
                    vertexCount,
                    input.Values);
            if (valueStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(valueStatus);
            }

            Assets::AssetTexture2DPayload payload =
                MakePayloadShell(kind, options, options.SourcePropertyName);
            const MeshAttributeTextureBakeStatus rasterStatus =
                Rasterize(kind, input, options, payload);
            if (rasterStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(rasterStatus);
            }

            return MeshAttributeTextureBakeResult{
                .Status = MeshAttributeTextureBakeStatus::Success,
                .Payload = std::move(payload),
            };
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromView(
            const BakeKind kind,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const MeshAttributeTextureBakeOptions& options)
        {
            using namespace ECS::Components::GeometrySources;

            if (view.ActiveDomain != Domain::Mesh)
            {
                return Failure(MeshAttributeTextureBakeStatus::WrongDomain);
            }
            if (view.VertexSource == nullptr)
            {
                return Failure(MeshAttributeTextureBakeStatus::MissingVertexSource);
            }
            const std::size_t vertexCount = view.VertexSource->Properties.Size();
            if (vertexCount == 0u)
            {
                return Failure(MeshAttributeTextureBakeStatus::EmptyMesh);
            }
            if (vertexCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return Failure(MeshAttributeTextureBakeStatus::InvalidTopology);
            }

            std::vector<std::uint32_t> surfaceIndices;
            const MeshAttributeTextureBakeStatus indexStatus = BuildSurfaceIndices(
                view,
                static_cast<std::uint32_t>(vertexCount),
                surfaceIndices);
            if (indexStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(indexStatus);
            }

            return BakeFromProperties(
                kind,
                Geometry::ConstPropertySet{view.VertexSource->Properties},
                vertexCount,
                std::move(surfaceIndices),
                options);
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromMesh(
            const BakeKind kind,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const MeshAttributeTextureBakeOptions& options)
        {
            if (mesh.VerticesSize() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return Failure(MeshAttributeTextureBakeStatus::InvalidTopology);
            }

            std::vector<std::uint32_t> surfaceIndices;
            const MeshAttributeTextureBakeStatus indexStatus =
                BuildSurfaceIndices(mesh, surfaceIndices);
            if (indexStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(indexStatus);
            }

            return BakeFromProperties(
                kind,
                mesh.VertexProperties(),
                mesh.VerticesSize(),
                std::move(surfaceIndices),
                options);
        }
    }

    const char* DebugNameForMeshAttributeTextureBakeStatus(
        const MeshAttributeTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case MeshAttributeTextureBakeStatus::Success: return "MeshAttributeTextureBake.Success";
        case MeshAttributeTextureBakeStatus::WrongDomain: return "MeshAttributeTextureBake.WrongDomain";
        case MeshAttributeTextureBakeStatus::MissingVertexSource: return "MeshAttributeTextureBake.MissingVertexSource";
        case MeshAttributeTextureBakeStatus::MissingHalfedgeTopology: return "MeshAttributeTextureBake.MissingHalfedgeTopology";
        case MeshAttributeTextureBakeStatus::MissingFaceTopology: return "MeshAttributeTextureBake.MissingFaceTopology";
        case MeshAttributeTextureBakeStatus::EmptyMesh: return "MeshAttributeTextureBake.EmptyMesh";
        case MeshAttributeTextureBakeStatus::InvalidTopology: return "MeshAttributeTextureBake.InvalidTopology";
        case MeshAttributeTextureBakeStatus::MissingTexcoords: return "MeshAttributeTextureBake.MissingTexcoords";
        case MeshAttributeTextureBakeStatus::MissingProperty: return "MeshAttributeTextureBake.MissingProperty";
        case MeshAttributeTextureBakeStatus::UnsupportedPropertyType: return "MeshAttributeTextureBake.UnsupportedPropertyType";
        case MeshAttributeTextureBakeStatus::MismatchedPropertyCount: return "MeshAttributeTextureBake.MismatchedPropertyCount";
        case MeshAttributeTextureBakeStatus::InvalidResolution: return "MeshAttributeTextureBake.InvalidResolution";
        case MeshAttributeTextureBakeStatus::NonFiniteTexcoord: return "MeshAttributeTextureBake.NonFiniteTexcoord";
        case MeshAttributeTextureBakeStatus::NonFinitePropertyValue: return "MeshAttributeTextureBake.NonFinitePropertyValue";
        case MeshAttributeTextureBakeStatus::DegenerateAllTriangles: return "MeshAttributeTextureBake.DegenerateAllTriangles";
        }
        return "MeshAttributeTextureBake.Unknown";
    }

    MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromView(BakeKind::Normal, view, options);
    }

    MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromMesh(BakeKind::Normal, mesh, options);
    }

    MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromView(BakeKind::Color, view, options);
    }

    MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromMesh(BakeKind::Color, mesh, options);
    }
}
