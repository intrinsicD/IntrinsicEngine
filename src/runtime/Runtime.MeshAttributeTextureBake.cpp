module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
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

        struct SurfaceTriangle
        {
            std::array<std::uint32_t, 3u> Vertices{};
            std::uint32_t FaceIndex{kInvalidIndex};
        };

        struct AttributeSample
        {
            glm::vec4 Value{0.0f};
            std::uint32_t Label{0u};
        };

        struct BakeInput
        {
            std::vector<glm::vec2> Texcoords{};
            std::vector<AttributeSample> Values{};
            std::vector<SurfaceTriangle> Triangles{};
            MeshAttributeTextureBakeSourceDomain SourceDomain{
                MeshAttributeTextureBakeSourceDomain::Vertex};
            MeshAttributeTextureBakeValueKind ValueKind{
                MeshAttributeTextureBakeValueKind::Auto};
            MeshAttributeTextureBakeEncoder Encoder{
                MeshAttributeTextureBakeEncoder::Auto};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
        };

        [[nodiscard]] MeshAttributeTextureBakeDiagnostics MakeDiagnostics(
            const MeshAttributeTextureBakeRequest& request)
        {
            return MeshAttributeTextureBakeDiagnostics{
                .SourceDomain = request.SourceDomain,
                .ValueKind = request.ValueKind,
                .Encoder = request.Encoder,
                .DirtyStamp = request.DirtyStamp,
            };
        }

        [[nodiscard]] MeshAttributeTextureBakeResult Failure(
            const MeshAttributeTextureBakeStatus status,
            MeshAttributeTextureBakeDiagnostics diagnostics = {})
        {
            return MeshAttributeTextureBakeResult{
                .Status = status,
                .Diagnostics = diagnostics,
                .Payload = {},
            };
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

        [[nodiscard]] float NormalizeRange(
            const float value,
            const float minValue,
            const float maxValue) noexcept
        {
            return std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
        }

        [[nodiscard]] std::array<std::byte, 4u> EncodeNormal(glm::vec3 normal) noexcept
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

        [[nodiscard]] std::array<std::byte, 4u> EncodeColor(const glm::vec4 color) noexcept
        {
            return {
                ToByte(color.r),
                ToByte(color.g),
                ToByte(color.b),
                ToByte(color.a),
            };
        }

        [[nodiscard]] std::array<std::byte, 4u> EncodeScalarColormap(const float t) noexcept
        {
            const float u = std::clamp(t, 0.0f, 1.0f);
            const float r = std::clamp(1.5f * u - 0.25f, 0.0f, 1.0f);
            const float g = 1.0f - std::abs(2.0f * u - 1.0f);
            const float b = std::clamp(1.25f - 1.5f * u, 0.0f, 1.0f);
            return {ToByte(r), ToByte(g), ToByte(b), std::byte{0xFF}};
        }

        [[nodiscard]] std::array<std::byte, 4u> EncodeLabelPalette(
            const std::uint32_t label) noexcept
        {
            std::uint32_t x = label * 747796405u + 2891336453u;
            x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
            x = (x >> 22u) ^ x;
            return {
                static_cast<std::byte>((x >> 0u) & 0xFFu),
                static_cast<std::byte>((x >> 8u) & 0xFFu),
                static_cast<std::byte>((x >> 16u) & 0xFFu),
                std::byte{0xFF},
            };
        }

        [[nodiscard]] float EdgeFunction(
            const glm::vec2 a,
            const glm::vec2 b,
            const glm::vec2 p) noexcept
        {
            return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        }

        [[nodiscard]] std::uint32_t ComponentsFor(
            const Assets::AssetTexturePixelFormat format) noexcept
        {
            switch (format)
            {
            case Assets::AssetTexturePixelFormat::R8Unorm: return 1u;
            case Assets::AssetTexturePixelFormat::Rg8Unorm: return 2u;
            case Assets::AssetTexturePixelFormat::Rgb8Unorm:
            case Assets::AssetTexturePixelFormat::Rgb32Float: return 3u;
            case Assets::AssetTexturePixelFormat::Rgba8Unorm:
            case Assets::AssetTexturePixelFormat::Rgba32Float: return 4u;
            case Assets::AssetTexturePixelFormat::Unknown: break;
            }
            return 0u;
        }

        [[nodiscard]] std::uint32_t BytesPerPixelFor(
            const Assets::AssetTexturePixelFormat format) noexcept
        {
            switch (format)
            {
            case Assets::AssetTexturePixelFormat::R8Unorm: return 1u;
            case Assets::AssetTexturePixelFormat::Rg8Unorm: return 2u;
            case Assets::AssetTexturePixelFormat::Rgb8Unorm: return 3u;
            case Assets::AssetTexturePixelFormat::Rgba8Unorm: return 4u;
            case Assets::AssetTexturePixelFormat::Rgb32Float: return 12u;
            case Assets::AssetTexturePixelFormat::Rgba32Float: return 16u;
            case Assets::AssetTexturePixelFormat::Unknown: break;
            }
            return 0u;
        }

        [[nodiscard]] Assets::AssetTexturePixelFormat DefaultFormatFor(
            const MeshAttributeTextureBakeEncoder encoder) noexcept
        {
            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::LinearScalar:
                return Assets::AssetTexturePixelFormat::R8Unorm;
            case MeshAttributeTextureBakeEncoder::Vector2:
                return Assets::AssetTexturePixelFormat::Rg8Unorm;
            case MeshAttributeTextureBakeEncoder::Vector3:
                return Assets::AssetTexturePixelFormat::Rgb8Unorm;
            case MeshAttributeTextureBakeEncoder::ScalarColormap:
            case MeshAttributeTextureBakeEncoder::LabelPalette:
            case MeshAttributeTextureBakeEncoder::Normal:
            case MeshAttributeTextureBakeEncoder::RgbaColor:
            case MeshAttributeTextureBakeEncoder::Auto:
                return Assets::AssetTexturePixelFormat::Rgba8Unorm;
            }
            return Assets::AssetTexturePixelFormat::Rgba8Unorm;
        }

        [[nodiscard]] Assets::AssetTextureColorSpace DefaultColorSpaceFor(
            const MeshAttributeTextureBakeEncoder encoder) noexcept
        {
            return encoder == MeshAttributeTextureBakeEncoder::RgbaColor
                ? Assets::AssetTextureColorSpace::SRGB
                : Assets::AssetTextureColorSpace::Linear;
        }

        [[nodiscard]] bool EncoderSupportsFormat(
            const MeshAttributeTextureBakeEncoder encoder,
            const Assets::AssetTexturePixelFormat format) noexcept
        {
            return DefaultFormatFor(encoder) == format;
        }

        [[nodiscard]] bool EncoderSupportsValueKind(
            const MeshAttributeTextureBakeEncoder encoder,
            const MeshAttributeTextureBakeValueKind valueKind) noexcept
        {
            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::LinearScalar:
            case MeshAttributeTextureBakeEncoder::ScalarColormap:
                return valueKind == MeshAttributeTextureBakeValueKind::Scalar;
            case MeshAttributeTextureBakeEncoder::LabelPalette:
                return valueKind == MeshAttributeTextureBakeValueKind::Label;
            case MeshAttributeTextureBakeEncoder::Vector2:
                return valueKind == MeshAttributeTextureBakeValueKind::Vector2;
            case MeshAttributeTextureBakeEncoder::Vector3:
            case MeshAttributeTextureBakeEncoder::Normal:
                return valueKind == MeshAttributeTextureBakeValueKind::Vector3;
            case MeshAttributeTextureBakeEncoder::RgbaColor:
                return valueKind == MeshAttributeTextureBakeValueKind::Vector3 ||
                       valueKind == MeshAttributeTextureBakeValueKind::Vector4;
            case MeshAttributeTextureBakeEncoder::Auto:
                break;
            }
            return false;
        }

        void WritePixelBytes(
            Assets::AssetTexture2DPayload& payload,
            const std::uint32_t bytesPerPixel,
            const std::uint32_t x,
            const std::uint32_t y,
            const std::array<std::byte, 16u>& bytes)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * payload.Metadata.Width + x) * bytesPerPixel;
            for (std::uint32_t i = 0u; i < bytesPerPixel; ++i)
            {
                payload.PixelBytes[offset + i] = bytes[i];
            }
        }

        [[nodiscard]] std::array<std::byte, 16u> EncodePixel(
            const MeshAttributeTextureBakeEncoder encoder,
            const AttributeSample& sample,
            const float rangeMin,
            const float rangeMax)
        {
            std::array<std::byte, 16u> bytes{};
            const glm::vec4 value = sample.Value;
            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::LinearScalar:
                bytes[0u] = ToByte(NormalizeRange(value.x, rangeMin, rangeMax));
                break;
            case MeshAttributeTextureBakeEncoder::ScalarColormap:
            {
                const std::array<std::byte, 4u> rgba =
                    EncodeScalarColormap(NormalizeRange(value.x, rangeMin, rangeMax));
                std::copy(rgba.begin(), rgba.end(), bytes.begin());
                break;
            }
            case MeshAttributeTextureBakeEncoder::LabelPalette:
            {
                const std::array<std::byte, 4u> rgba = EncodeLabelPalette(sample.Label);
                std::copy(rgba.begin(), rgba.end(), bytes.begin());
                break;
            }
            case MeshAttributeTextureBakeEncoder::Vector2:
                bytes[0u] = ToByte(value.x);
                bytes[1u] = ToByte(value.y);
                break;
            case MeshAttributeTextureBakeEncoder::Vector3:
                bytes[0u] = ToByte(value.x);
                bytes[1u] = ToByte(value.y);
                bytes[2u] = ToByte(value.z);
                break;
            case MeshAttributeTextureBakeEncoder::Normal:
            {
                const std::array<std::byte, 4u> rgba = EncodeNormal(glm::vec3{value});
                std::copy(rgba.begin(), rgba.end(), bytes.begin());
                break;
            }
            case MeshAttributeTextureBakeEncoder::RgbaColor:
            case MeshAttributeTextureBakeEncoder::Auto:
            {
                const std::array<std::byte, 4u> rgba = EncodeColor(value);
                std::copy(rgba.begin(), rgba.end(), bytes.begin());
                break;
            }
            }
            return bytes;
        }

        [[nodiscard]] MeshAttributeTextureBakeEncoder ResolveEncoder(
            const MeshAttributeTextureBakeRequest& request,
            const MeshAttributeTextureBakeValueKind valueKind) noexcept
        {
            if (request.Encoder != MeshAttributeTextureBakeEncoder::Auto)
            {
                return request.Encoder;
            }
            switch (valueKind)
            {
            case MeshAttributeTextureBakeValueKind::Scalar:
                return MeshAttributeTextureBakeEncoder::ScalarColormap;
            case MeshAttributeTextureBakeValueKind::Label:
                return MeshAttributeTextureBakeEncoder::LabelPalette;
            case MeshAttributeTextureBakeValueKind::Vector2:
                return MeshAttributeTextureBakeEncoder::Vector2;
            case MeshAttributeTextureBakeValueKind::Vector3:
                return MeshAttributeTextureBakeEncoder::Vector3;
            case MeshAttributeTextureBakeValueKind::Vector4:
                return MeshAttributeTextureBakeEncoder::RgbaColor;
            case MeshAttributeTextureBakeValueKind::Auto:
                break;
            }
            return MeshAttributeTextureBakeEncoder::RgbaColor;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus ValidateResolvedRequest(
            const MeshAttributeTextureBakeRequest& request,
            const MeshAttributeTextureBakeEncoder encoder,
            const Assets::AssetTexturePixelFormat format,
            const float rangeMin,
            const float rangeMax) noexcept
        {
            if (request.Width == 0u || request.Height == 0u)
            {
                return MeshAttributeTextureBakeStatus::InvalidResolution;
            }
            if (!EncoderSupportsFormat(encoder, format))
            {
                return MeshAttributeTextureBakeStatus::UnsupportedPropertyType;
            }
            if ((encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                 encoder == MeshAttributeTextureBakeEncoder::ScalarColormap) &&
                (!std::isfinite(rangeMin) || !std::isfinite(rangeMax) || rangeMin >= rangeMax))
            {
                return MeshAttributeTextureBakeStatus::InvalidRange;
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] Assets::AssetTexture2DPayload MakePayloadShell(
            const MeshAttributeTextureBakeRequest& request,
            const MeshAttributeTextureBakeEncoder encoder,
            const Assets::AssetTexturePixelFormat format)
        {
            Assets::AssetTexture2DPayload payload{};
            payload.Metadata.Width = request.Width;
            payload.Metadata.Height = request.Height;
            payload.Metadata.Components = ComponentsFor(format);
            payload.Metadata.PixelFormat = format;
            payload.Metadata.ColorSpace = request.ColorSpace == Assets::AssetTextureColorSpace::Unknown
                ? DefaultColorSpaceFor(encoder)
                : request.ColorSpace;
            payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::Generated;
            payload.Metadata.SourceFormat = Assets::AssetFileFormat::Unknown;
            payload.Metadata.SourcePath = request.SourcePropertyName;
            payload.Metadata.DebugName = request.DebugName.empty()
                ? request.SourcePropertyName
                : request.DebugName;

            const std::uint32_t bytesPerPixel = BytesPerPixelFor(format);
            payload.PixelBytes.resize(
                static_cast<std::size_t>(request.Width) * request.Height * bytesPerPixel);

            AttributeSample initial{};
            if (encoder == MeshAttributeTextureBakeEncoder::Normal)
            {
                initial.Value = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
            }
            else if (encoder == MeshAttributeTextureBakeEncoder::RgbaColor)
            {
                initial.Value = glm::vec4{1.0f};
            }
            const std::array<std::byte, 16u> initialBytes =
                EncodePixel(encoder, initial, 0.0f, 1.0f);
            for (std::uint32_t y = 0u; y < request.Height; ++y)
            {
                for (std::uint32_t x = 0u; x < request.Width; ++x)
                {
                    WritePixelBytes(payload, bytesPerPixel, x, y, initialBytes);
                }
            }
            return payload;
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

        [[nodiscard]] MeshAttributeTextureBakeStatus BuildSurfaceTriangles(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const std::uint32_t vertexCount,
            std::vector<SurfaceTriangle>& outTriangles)
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
            outTriangles.clear();
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
                    outTriangles.clear();
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }
                if (outcome == FaceRingOutcome::Skip)
                {
                    continue;
                }
                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    outTriangles.push_back(SurfaceTriangle{
                        .Vertices = {ring[0u], ring[i], ring[i + 1u]},
                        .FaceIndex = static_cast<std::uint32_t>(f),
                    });
                }
            }

            return outTriangles.empty()
                ? MeshAttributeTextureBakeStatus::DegenerateAllTriangles
                : MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus BuildSurfaceTriangles(
            const Geometry::HalfedgeMesh::Mesh& mesh,
            std::vector<SurfaceTriangle>& outTriangles)
        {
            outTriangles.clear();
            if (mesh.VerticesSize() == 0u || mesh.HalfedgesSize() == 0u || mesh.FacesSize() == 0u)
            {
                return MeshAttributeTextureBakeStatus::EmptyMesh;
            }

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u; faceIndex < mesh.FacesSize(); ++faceIndex)
            {
                const Geometry::FaceHandle face{static_cast<Geometry::PropertyIndex>(faceIndex)};
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
                    outTriangles.push_back(SurfaceTriangle{
                        .Vertices = {ring[0u], ring[i], ring[i + 1u]},
                        .FaceIndex = static_cast<std::uint32_t>(faceIndex),
                    });
                }
            }

            return outTriangles.empty()
                ? MeshAttributeTextureBakeStatus::DegenerateAllTriangles
                : MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillTexcoords(
            const Geometry::ConstPropertySet& properties,
            const std::string_view propertyName,
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

        template <typename T>
        [[nodiscard]] MeshAttributeTextureBakeStatus FillScalarValues(
            const Geometry::ConstProperty<T>& property,
            const MeshAttributeTextureBakeRequest& request,
            const std::size_t expectedCount,
            std::vector<AttributeSample>& outValues,
            float& outMin,
            float& outMax)
        {
            if (property.Vector().size() != expectedCount)
            {
                return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
            }

            outValues.resize(expectedCount);
            bool sawFinite = false;
            float minValue = std::numeric_limits<float>::max();
            float maxValue = std::numeric_limits<float>::lowest();
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                const float value = static_cast<float>(property.Vector()[i]);
                if (!std::isfinite(value))
                {
                    return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                }
                outValues[i].Value = glm::vec4{value, 0.0f, 0.0f, 1.0f};
                sawFinite = true;
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
            if (!sawFinite)
            {
                return MeshAttributeTextureBakeStatus::EmptyMesh;
            }
            if (request.RangePolicy == MeshAttributeTextureBakeRangePolicy::Manual)
            {
                if (!std::isfinite(request.RangeMin) ||
                    !std::isfinite(request.RangeMax) ||
                    request.RangeMin >= request.RangeMax)
                {
                    return MeshAttributeTextureBakeStatus::InvalidRange;
                }
                outMin = request.RangeMin;
                outMax = request.RangeMax;
            }
            else
            {
                if (minValue == maxValue)
                {
                    maxValue = minValue + 1.0f;
                }
                outMin = minValue;
                outMax = maxValue;
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillLabelValues(
            const Geometry::ConstProperty<std::uint32_t>& property,
            const std::size_t expectedCount,
            std::vector<AttributeSample>& outValues)
        {
            if (property.Vector().size() != expectedCount)
            {
                return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
            }
            outValues.resize(expectedCount);
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                outValues[i].Label = property.Vector()[i];
                outValues[i].Value = glm::vec4{static_cast<float>(property.Vector()[i]), 0.0f, 0.0f, 1.0f};
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        template <typename VecT>
        [[nodiscard]] MeshAttributeTextureBakeStatus FillVectorValues(
            const Geometry::ConstProperty<VecT>& property,
            const std::size_t expectedCount,
            std::vector<AttributeSample>& outValues)
        {
            if (property.Vector().size() != expectedCount)
            {
                return MeshAttributeTextureBakeStatus::MismatchedPropertyCount;
            }
            outValues.resize(expectedCount);
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                if (!IsFinite(property.Vector()[i]))
                {
                    return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                }
                if constexpr (std::is_same_v<VecT, glm::vec2>)
                {
                    outValues[i].Value = glm::vec4{property.Vector()[i], 0.0f, 1.0f};
                }
                else if constexpr (std::is_same_v<VecT, glm::vec3>)
                {
                    outValues[i].Value = glm::vec4{property.Vector()[i], 1.0f};
                }
                else
                {
                    outValues[i].Value = property.Vector()[i];
                }
            }
            return MeshAttributeTextureBakeStatus::Success;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus FillValues(
            const Geometry::ConstPropertySet& properties,
            const MeshAttributeTextureBakeRequest& request,
            const std::size_t expectedCount,
            MeshAttributeTextureBakeDiagnostics& diagnostics,
            BakeInput& input)
        {
            if (request.SourcePropertyName.empty())
            {
                return MeshAttributeTextureBakeStatus::MissingProperty;
            }
            if (!properties.Exists(request.SourcePropertyName))
            {
                return MeshAttributeTextureBakeStatus::MissingProperty;
            }

            diagnostics.ExpectedValueCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(expectedCount, std::numeric_limits<std::uint32_t>::max()));

            const MeshAttributeTextureBakeValueKind requestedKind = request.ValueKind;
            if (requestedKind == MeshAttributeTextureBakeValueKind::Auto ||
                requestedKind == MeshAttributeTextureBakeValueKind::Vector4)
            {
                if (const auto prop = properties.Get<glm::vec4>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Vector4;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillVectorValues(prop, expectedCount, input.Values);
                }
            }
            if (requestedKind == MeshAttributeTextureBakeValueKind::Auto ||
                requestedKind == MeshAttributeTextureBakeValueKind::Vector3)
            {
                if (const auto prop = properties.Get<glm::vec3>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Vector3;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillVectorValues(prop, expectedCount, input.Values);
                }
            }
            if (requestedKind == MeshAttributeTextureBakeValueKind::Auto ||
                requestedKind == MeshAttributeTextureBakeValueKind::Vector2)
            {
                if (const auto prop = properties.Get<glm::vec2>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Vector2;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillVectorValues(prop, expectedCount, input.Values);
                }
            }
            if (requestedKind == MeshAttributeTextureBakeValueKind::Auto ||
                requestedKind == MeshAttributeTextureBakeValueKind::Scalar)
            {
                if (const auto prop = properties.Get<float>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Scalar;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillScalarValues(prop, request, expectedCount, input.Values, input.RangeMin, input.RangeMax);
                }
                if (const auto prop = properties.Get<double>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Scalar;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillScalarValues(prop, request, expectedCount, input.Values, input.RangeMin, input.RangeMax);
                }
            }
            if (requestedKind == MeshAttributeTextureBakeValueKind::Auto ||
                requestedKind == MeshAttributeTextureBakeValueKind::Label)
            {
                if (const auto prop = properties.Get<std::uint32_t>(request.SourcePropertyName))
                {
                    diagnostics.SourceValueCount = static_cast<std::uint32_t>(prop.Vector().size());
                    input.ValueKind = MeshAttributeTextureBakeValueKind::Label;
                    diagnostics.ValueKind = input.ValueKind;
                    return FillLabelValues(prop, expectedCount, input.Values);
                }
            }

            return MeshAttributeTextureBakeStatus::UnsupportedPropertyType;
        }

        [[nodiscard]] MeshAttributeTextureBakeStatus Rasterize(
            const BakeInput& input,
            const MeshAttributeTextureBakeRequest& request,
            Assets::AssetTexture2DPayload& payload,
            MeshAttributeTextureBakeDiagnostics& diagnostics)
        {
            const std::uint32_t width = request.Width;
            const std::uint32_t height = request.Height;
            const std::uint32_t bytesPerPixel = BytesPerPixelFor(payload.Metadata.PixelFormat);
            bool sawNondegenerateTriangle = false;

            diagnostics.SurfaceTriangleCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(input.Triangles.size(), std::numeric_limits<std::uint32_t>::max()));

            for (const SurfaceTriangle& tri : input.Triangles)
            {
                const std::uint32_t ia = tri.Vertices[0u];
                const std::uint32_t ib = tri.Vertices[1u];
                const std::uint32_t ic = tri.Vertices[2u];
                if (ia >= input.Texcoords.size() ||
                    ib >= input.Texcoords.size() ||
                    ic >= input.Texcoords.size())
                {
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }

                if (input.SourceDomain == MeshAttributeTextureBakeSourceDomain::Vertex &&
                    (ia >= input.Values.size() || ib >= input.Values.size() || ic >= input.Values.size()))
                {
                    return MeshAttributeTextureBakeStatus::InvalidTopology;
                }
                if (input.SourceDomain == MeshAttributeTextureBakeSourceDomain::Face &&
                    tri.FaceIndex >= input.Values.size())
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

                const glm::vec2 pA{uvA.x * static_cast<float>(width - 1u),
                                   uvA.y * static_cast<float>(height - 1u)};
                const glm::vec2 pB{uvB.x * static_cast<float>(width - 1u),
                                   uvB.y * static_cast<float>(height - 1u)};
                const glm::vec2 pC{uvC.x * static_cast<float>(width - 1u),
                                   uvC.y * static_cast<float>(height - 1u)};

                const float area = EdgeFunction(pA, pB, pC);
                if (std::abs(area) <= 1.0e-6f || !std::isfinite(area))
                {
                    ++diagnostics.DegenerateUvTriangleCount;
                    continue;
                }
                sawNondegenerateTriangle = true;

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
                        const glm::vec2 p{static_cast<float>(x), static_cast<float>(y)};
                        const float w0 = EdgeFunction(pB, pC, p) / area;
                        const float w1 = EdgeFunction(pC, pA, p) / area;
                        const float w2 = EdgeFunction(pA, pB, p) / area;
                        constexpr float kEpsilon = -1.0e-5f;
                        if (w0 < kEpsilon || w1 < kEpsilon || w2 < kEpsilon)
                        {
                            continue;
                        }

                        AttributeSample sample{};
                        if (input.SourceDomain == MeshAttributeTextureBakeSourceDomain::Face)
                        {
                            sample = input.Values[tri.FaceIndex];
                        }
                        else
                        {
                            sample.Value =
                                input.Values[ia].Value * w0 +
                                input.Values[ib].Value * w1 +
                                input.Values[ic].Value * w2;
                            sample.Label = input.Values[ia].Label;
                        }
                        if (!IsFinite(sample.Value))
                        {
                            return MeshAttributeTextureBakeStatus::NonFinitePropertyValue;
                        }

                        const std::array<std::byte, 16u> bytes =
                            EncodePixel(input.Encoder, sample, input.RangeMin, input.RangeMax);
                        WritePixelBytes(payload, bytesPerPixel, x, y, bytes);
                        ++diagnostics.CoveredPixelCount;
                    }
                }
            }

            if (diagnostics.CoveredPixelCount > 0u)
            {
                return MeshAttributeTextureBakeStatus::Success;
            }
            return sawNondegenerateTriangle
                ? MeshAttributeTextureBakeStatus::ZeroCoverageBake
                : MeshAttributeTextureBakeStatus::DegenerateUvTriangles;
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromPropertySets(
            const Geometry::ConstPropertySet& vertexProperties,
            const Geometry::ConstPropertySet& faceProperties,
            const std::size_t vertexCount,
            const std::size_t faceCount,
            std::vector<SurfaceTriangle> triangles,
            MeshAttributeTextureBakeRequest request)
        {
            MeshAttributeTextureBakeDiagnostics diagnostics = MakeDiagnostics(request);
            if (vertexCount == 0u || faceCount == 0u)
            {
                return Failure(MeshAttributeTextureBakeStatus::EmptyMesh, diagnostics);
            }
            if (request.SourceDomain != MeshAttributeTextureBakeSourceDomain::Vertex &&
                request.SourceDomain != MeshAttributeTextureBakeSourceDomain::Face)
            {
                return Failure(MeshAttributeTextureBakeStatus::UnsupportedDomain, diagnostics);
            }

            BakeInput input{};
            input.Triangles = std::move(triangles);
            input.SourceDomain = request.SourceDomain;
            diagnostics.SourceDomain = request.SourceDomain;

            const MeshAttributeTextureBakeStatus texStatus =
                FillTexcoords(vertexProperties, request.TexcoordPropertyName, vertexCount, input.Texcoords);
            if (texStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(texStatus, diagnostics);
            }

            const Geometry::ConstPropertySet& sourceProperties =
                request.SourceDomain == MeshAttributeTextureBakeSourceDomain::Vertex
                    ? vertexProperties
                    : faceProperties;
            const std::size_t expectedCount =
                request.SourceDomain == MeshAttributeTextureBakeSourceDomain::Vertex
                    ? vertexCount
                    : faceCount;
            const MeshAttributeTextureBakeStatus valueStatus =
                FillValues(sourceProperties, request, expectedCount, diagnostics, input);
            if (valueStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(valueStatus, diagnostics);
            }

            input.Encoder = ResolveEncoder(request, input.ValueKind);
            diagnostics.Encoder = input.Encoder;
            if (!EncoderSupportsValueKind(input.Encoder, input.ValueKind))
            {
                return Failure(MeshAttributeTextureBakeStatus::UnsupportedPropertyType, diagnostics);
            }
            const Assets::AssetTexturePixelFormat format =
                request.PixelFormat == Assets::AssetTexturePixelFormat::Unknown
                    ? DefaultFormatFor(input.Encoder)
                    : request.PixelFormat;
            if (const MeshAttributeTextureBakeStatus requestStatus =
                    ValidateResolvedRequest(request, input.Encoder, format, input.RangeMin, input.RangeMax);
                requestStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(requestStatus, diagnostics);
            }

            Assets::AssetTexture2DPayload payload =
                MakePayloadShell(request, input.Encoder, format);
            const MeshAttributeTextureBakeStatus rasterStatus =
                Rasterize(input, request, payload, diagnostics);
            if (rasterStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(rasterStatus, diagnostics);
            }

            return MeshAttributeTextureBakeResult{
                .Status = MeshAttributeTextureBakeStatus::Success,
                .Diagnostics = diagnostics,
                .Payload = std::move(payload),
            };
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromView(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const MeshAttributeTextureBakeRequest& request)
        {
            using namespace ECS::Components::GeometrySources;

            MeshAttributeTextureBakeDiagnostics diagnostics = MakeDiagnostics(request);
            const SourceAvailability availability = BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != Domain::Mesh)
            {
                return Failure(MeshAttributeTextureBakeStatus::WrongDomain, diagnostics);
            }
            if (view.VertexSource == nullptr)
            {
                return Failure(MeshAttributeTextureBakeStatus::MissingVertexSource, diagnostics);
            }
            if (view.FaceSource == nullptr)
            {
                return Failure(MeshAttributeTextureBakeStatus::MissingFaceTopology, diagnostics);
            }
            const std::size_t vertexCount = view.VertexSource->Properties.Size();
            const std::size_t faceCount = view.FaceSource->Properties.Size();
            if (vertexCount == 0u || faceCount == 0u)
            {
                return Failure(MeshAttributeTextureBakeStatus::EmptyMesh, diagnostics);
            }
            if (vertexCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                faceCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return Failure(MeshAttributeTextureBakeStatus::InvalidTopology, diagnostics);
            }

            std::vector<SurfaceTriangle> triangles;
            const MeshAttributeTextureBakeStatus triStatus = BuildSurfaceTriangles(
                view,
                static_cast<std::uint32_t>(vertexCount),
                triangles);
            if (triStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(triStatus, diagnostics);
            }

            return BakeFromPropertySets(
                Geometry::ConstPropertySet{view.VertexSource->Properties},
                Geometry::ConstPropertySet{view.FaceSource->Properties},
                vertexCount,
                faceCount,
                std::move(triangles),
                request);
        }

        [[nodiscard]] MeshAttributeTextureBakeResult BakeFromMesh(
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const MeshAttributeTextureBakeRequest& request)
        {
            MeshAttributeTextureBakeDiagnostics diagnostics = MakeDiagnostics(request);
            if (mesh.VerticesSize() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                mesh.FacesSize() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return Failure(MeshAttributeTextureBakeStatus::InvalidTopology, diagnostics);
            }

            std::vector<SurfaceTriangle> triangles;
            const MeshAttributeTextureBakeStatus triStatus =
                BuildSurfaceTriangles(mesh, triangles);
            if (triStatus != MeshAttributeTextureBakeStatus::Success)
            {
                return Failure(triStatus, diagnostics);
            }

            return BakeFromPropertySets(
                mesh.VertexProperties(),
                mesh.FaceProperties(),
                mesh.VerticesSize(),
                mesh.FacesSize(),
                std::move(triangles),
                request);
        }

        [[nodiscard]] MeshAttributeTextureBakeRequest NormalRequestFromOptions(
            MeshAttributeTextureBakeOptions options)
        {
            if (options.SourcePropertyName.empty())
            {
                options.SourcePropertyName = kDefaultNormalProperty;
            }
            return MeshAttributeTextureBakeRequest{
                .SourcePropertyName = std::move(options.SourcePropertyName),
                .SourceDomain = MeshAttributeTextureBakeSourceDomain::Vertex,
                .ValueKind = MeshAttributeTextureBakeValueKind::Vector3,
                .TargetSemantic = "normal",
                .Encoder = MeshAttributeTextureBakeEncoder::Normal,
                .TexcoordPropertyName = std::move(options.TexcoordPropertyName),
                .Width = options.Width,
                .Height = options.Height,
                .ColorSpace = Assets::AssetTextureColorSpace::Linear,
                .PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm,
                .DebugName = std::move(options.DebugName),
            };
        }

        [[nodiscard]] MeshAttributeTextureBakeRequest ColorRequestFromOptions(
            MeshAttributeTextureBakeOptions options)
        {
            if (options.SourcePropertyName.empty())
            {
                options.SourcePropertyName = kDefaultColorProperty;
            }
            return MeshAttributeTextureBakeRequest{
                .SourcePropertyName = std::move(options.SourcePropertyName),
                .SourceDomain = MeshAttributeTextureBakeSourceDomain::Vertex,
                .ValueKind = MeshAttributeTextureBakeValueKind::Auto,
                .TargetSemantic = "albedo",
                .Encoder = MeshAttributeTextureBakeEncoder::RgbaColor,
                .TexcoordPropertyName = std::move(options.TexcoordPropertyName),
                .Width = options.Width,
                .Height = options.Height,
                .ColorSpace = Assets::AssetTextureColorSpace::SRGB,
                .PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm,
                .DebugName = std::move(options.DebugName),
            };
        }

        [[nodiscard]] std::string SanitizePathToken(const std::string_view token)
        {
            std::string sanitized;
            sanitized.reserve(token.size());
            for (const char c : token)
            {
                const bool keep =
                    (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' ||
                    c == '_' ||
                    c == '.';
                sanitized.push_back(keep ? c : '-');
            }
            return sanitized.empty() ? std::string{"unnamed"} : sanitized;
        }

        [[nodiscard]] std::string DomainToken(
            const MeshAttributeTextureBakeSourceDomain domain)
        {
            switch (domain)
            {
            case MeshAttributeTextureBakeSourceDomain::Vertex: return "vertex";
            case MeshAttributeTextureBakeSourceDomain::Face: return "face";
            case MeshAttributeTextureBakeSourceDomain::Edge: return "edge";
            case MeshAttributeTextureBakeSourceDomain::Halfedge: return "halfedge";
            }
            return "unknown";
        }
    }

    const char* DebugNameForMeshAttributeTextureBakeStatus(
        const MeshAttributeTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case MeshAttributeTextureBakeStatus::Success: return "MeshAttributeTextureBake.Success";
        case MeshAttributeTextureBakeStatus::WrongDomain: return "MeshAttributeTextureBake.WrongDomain";
        case MeshAttributeTextureBakeStatus::UnsupportedDomain: return "MeshAttributeTextureBake.UnsupportedDomain";
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
        case MeshAttributeTextureBakeStatus::InvalidRange: return "MeshAttributeTextureBake.InvalidRange";
        case MeshAttributeTextureBakeStatus::NonFiniteTexcoord: return "MeshAttributeTextureBake.NonFiniteTexcoord";
        case MeshAttributeTextureBakeStatus::NonFinitePropertyValue: return "MeshAttributeTextureBake.NonFinitePropertyValue";
        case MeshAttributeTextureBakeStatus::DegenerateAllTriangles: return "MeshAttributeTextureBake.DegenerateAllTriangles";
        case MeshAttributeTextureBakeStatus::DegenerateUvTriangles: return "MeshAttributeTextureBake.DegenerateUvTriangles";
        case MeshAttributeTextureBakeStatus::ZeroCoverageBake: return "MeshAttributeTextureBake.ZeroCoverageBake";
        }
        return "MeshAttributeTextureBake.Unknown";
    }

    std::string BuildMeshAttributeTextureBakeAssetPath(
        const std::string_view sourceKey,
        const MeshAttributeTextureBakeRequest& request)
    {
        const std::string_view base = sourceKey.empty()
            ? std::string_view{"mesh"}
            : sourceKey;
        return std::string{base}
            + ".generated-mesh-attribute-texture-"
            + DomainToken(request.SourceDomain)
            + "-"
            + SanitizePathToken(request.TargetSemantic)
            + "-"
            + SanitizePathToken(request.SourcePropertyName)
            + ".texture";
    }

    MeshAttributeTextureBakeResult BakeMeshAttributeTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeRequest& request)
    {
        return BakeFromView(view, request);
    }

    MeshAttributeTextureBakeResult BakeMeshAttributeTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeRequest& request)
    {
        return BakeFromMesh(mesh, request);
    }

    MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromView(view, NormalRequestFromOptions(options));
    }

    MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromMesh(mesh, NormalRequestFromOptions(options));
    }

    MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromView(view, ColorRequestFromOptions(options));
    }

    MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options)
    {
        return BakeFromMesh(mesh, ColorRequestFromOptions(options));
    }
}
