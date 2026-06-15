#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Geometry.Properties;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    struct MeshBakeScratch
    {
        GS::Vertices VertexSource{};
        GS::Halfedges HalfedgeSource{};
        GS::Faces FaceSource{};

        [[nodiscard]] GS::ConstSourceView View() const noexcept
        {
            GS::ConstSourceView view{};
            view.ActiveDomain = GS::Domain::Mesh;
            view.VertexSource = &VertexSource;
            view.HalfedgeSource = &HalfedgeSource;
            view.FaceSource = &FaceSource;
            return view;
        }
    };

    void SetTopology(MeshBakeScratch& mesh)
    {
        mesh.VertexSource.Properties.Resize(3u);

        mesh.HalfedgeSource.Properties.Resize(6u);
        mesh.HalfedgeSource.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeToVertex}, kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 0u, 2u, 1u};
        mesh.HalfedgeSource.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeNext}, kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 5u, 3u, 4u};
        mesh.HalfedgeSource.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeFace}, kInvalidIndex)
            .Vector() = {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};

        mesh.FaceSource.Properties.Resize(1u);
        mesh.FaceSource.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kFaceHalfedge}, kInvalidIndex)
            .Vector() = {0u};
    }

    void SetTexcoords(MeshBakeScratch& mesh)
    {
        auto texcoords = mesh.VertexSource.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f});
        texcoords.Vector() = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
        };
    }

    [[nodiscard]] MeshBakeScratch MakeTexturedTriangle()
    {
        MeshBakeScratch mesh{};
        SetTopology(mesh);
        SetTexcoords(mesh);
        return mesh;
    }

    [[nodiscard]] std::uint8_t ByteAt(
        const std::vector<std::byte>& bytes,
        const std::size_t index)
    {
        return static_cast<std::uint8_t>(bytes[index]);
    }
}

TEST(RuntimeMeshAttributeTextureBake, BakesVertexNormalTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto normals = mesh.VertexSource.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
    normals.Vector() = {
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };

    Runtime::MeshAttributeTextureBakeOptions options{};
    options.Width = 4u;
    options.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshVertexNormalTexture(mesh.View(), options);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.Width, 4u);
    EXPECT_EQ(result.Payload.Metadata.Height, 4u);
    EXPECT_EQ(result.Payload.Metadata.Components, 4u);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rgba8Unorm);
    EXPECT_EQ(result.Payload.Metadata.ColorSpace, Assets::AssetTextureColorSpace::Linear);
    EXPECT_EQ(result.Payload.Metadata.SourceKind, Assets::AssetTextureSourceKind::Generated);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 255u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 255u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesVec4VertexColorTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto colors = mesh.VertexSource.Properties.GetOrAdd<glm::vec4>("v:user_color", glm::vec4{0.0f});
    colors.Vector() = {
        {0.25f, 0.5f, 1.0f, 0.5f},
        {0.25f, 0.5f, 1.0f, 0.5f},
        {0.25f, 0.5f, 1.0f, 0.5f},
    };

    Runtime::MeshAttributeTextureBakeOptions options{};
    options.SourcePropertyName = "v:user_color";
    options.Width = 4u;
    options.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshVertexColorTexture(mesh.View(), options);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.ColorSpace, Assets::AssetTextureColorSpace::SRGB);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 64u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), 255u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 128u);
}

TEST(RuntimeMeshAttributeTextureBake, MissingTexcoordsFailClosed)
{
    MeshBakeScratch mesh{};
    SetTopology(mesh);
    auto normals = mesh.VertexSource.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
    normals.Vector() = {
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    };

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshVertexNormalTexture(mesh.View());

    EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::MissingTexcoords);
    EXPECT_TRUE(result.Payload.PixelBytes.empty());
}

TEST(RuntimeMeshAttributeTextureBake, UnsupportedColorPropertyTypeFailsClosed)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto labels = mesh.VertexSource.Properties.GetOrAdd<std::uint32_t>("v:label", 0u);
    labels.Vector() = {1u, 2u, 3u};

    Runtime::MeshAttributeTextureBakeOptions options{};
    options.SourcePropertyName = "v:label";

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshVertexColorTexture(mesh.View(), options);

    EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::UnsupportedPropertyType);
}

TEST(RuntimeMeshAttributeTextureBake, BakesGenericVertexScalarLinearTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
    scalar.Vector() = {0.0f, 1.0f, 0.0f};

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:heat";
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;
    request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::LinearScalar;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Diagnostics.ValueKind, Runtime::MeshAttributeTextureBakeValueKind::Scalar);
    EXPECT_EQ(result.Diagnostics.Encoder, Runtime::MeshAttributeTextureBakeEncoder::LinearScalar);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::R8Unorm);
    EXPECT_EQ(result.Payload.Metadata.Components, 1u);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 0u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 255u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesGenericVertexVec2TexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto vectors = mesh.VertexSource.Properties.GetOrAdd<glm::vec2>("v:uv_flow", glm::vec2{0.0f});
    vectors.Vector() = {
        {0.25f, 0.75f},
        {0.25f, 0.75f},
        {0.25f, 0.75f},
    };

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:uv_flow";
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Vector2;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rg8Unorm);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 2u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 64u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 191u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesGenericVertexVec3TexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto vectors = mesh.VertexSource.Properties.GetOrAdd<glm::vec3>("v:direction", glm::vec3{0.0f});
    vectors.Vector() = {
        {0.25f, 0.5f, 1.0f},
        {0.25f, 0.5f, 1.0f},
        {0.25f, 0.5f, 1.0f},
    };

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:direction";
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Vector3;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rgb8Unorm);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 3u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 64u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), 255u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesGenericVertexVec4TexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto colors = mesh.VertexSource.Properties.GetOrAdd<glm::vec4>("v:rgba", glm::vec4{0.0f});
    colors.Vector() = {
        {0.25f, 0.5f, 1.0f, 0.5f},
        {0.25f, 0.5f, 1.0f, 0.5f},
        {0.25f, 0.5f, 1.0f, 0.5f},
    };

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:rgba";
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Vector4;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rgba8Unorm);
    EXPECT_EQ(result.Payload.Metadata.ColorSpace, Assets::AssetTextureColorSpace::SRGB);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 64u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), 255u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 128u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesGenericLabelPaletteTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto labels = mesh.VertexSource.Properties.GetOrAdd<std::uint32_t>("v:cluster", 0u);
    labels.Vector() = {7u, 7u, 7u};

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:cluster";
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Label;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Diagnostics.Encoder, Runtime::MeshAttributeTextureBakeEncoder::LabelPalette);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rgba8Unorm);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 255u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), ByteAt(result.Payload.PixelBytes, 4u));
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), ByteAt(result.Payload.PixelBytes, 5u));
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), ByteAt(result.Payload.PixelBytes, 6u));
}

TEST(RuntimeMeshAttributeTextureBake, BakesFaceDomainConstantScalarTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto faceScalar = mesh.FaceSource.Properties.GetOrAdd<float>("f:heat", 0.0f);
    faceScalar.Vector() = {0.5f};

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "f:heat";
    request.SourceDomain = Runtime::MeshAttributeTextureBakeSourceDomain::Face;
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;
    request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::LinearScalar;
    request.RangePolicy = Runtime::MeshAttributeTextureBakeRangePolicy::Manual;
    request.RangeMin = 0.0f;
    request.RangeMax = 1.0f;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Diagnostics.SourceDomain, Runtime::MeshAttributeTextureBakeSourceDomain::Face);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 128u);
    EXPECT_GT(result.Diagnostics.CoveredPixelCount, 0u);
}

TEST(RuntimeMeshAttributeTextureBake, BakesFaceDomainConstantColorTexturePayload)
{
    MeshBakeScratch mesh = MakeTexturedTriangle();
    auto faceColor = mesh.FaceSource.Properties.GetOrAdd<glm::vec4>("f:rgba", glm::vec4{0.0f});
    faceColor.Vector() = {{0.25f, 0.5f, 1.0f, 0.5f}};

    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "f:rgba";
    request.SourceDomain = Runtime::MeshAttributeTextureBakeSourceDomain::Face;
    request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Vector4;
    request.Width = 4u;
    request.Height = 4u;

    const Runtime::MeshAttributeTextureBakeResult result =
        Runtime::BakeMeshAttributeTexture(mesh.View(), request);

    ASSERT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::Success);
    EXPECT_EQ(result.Payload.Metadata.PixelFormat, Assets::AssetTexturePixelFormat::Rgba8Unorm);
    ASSERT_EQ(result.Payload.PixelBytes.size(), 4u * 4u * 4u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 0u), 64u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 1u), 128u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 2u), 255u);
    EXPECT_EQ(ByteAt(result.Payload.PixelBytes, 3u), 128u);
}

TEST(RuntimeMeshAttributeTextureBake, RejectsInvalidGenericInputsWithExplicitStatuses)
{
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, 1.0f};

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::MismatchedPropertyCount);
    }
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, 1.0f, 0.0f};

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.SourceDomain = Runtime::MeshAttributeTextureBakeSourceDomain::Edge;
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::UnsupportedDomain);
    }
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, 1.0f, 0.0f};

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;
        request.RangePolicy = Runtime::MeshAttributeTextureBakeRangePolicy::Manual;
        request.RangeMin = 1.0f;
        request.RangeMax = 0.0f;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::InvalidRange);
    }
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, 1.0f, 0.0f};

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;
        request.Width = 0u;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::InvalidResolution);
    }
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::NonFinitePropertyValue);
    }
    {
        MeshBakeScratch mesh = MakeTexturedTriangle();
        auto scalar = mesh.VertexSource.Properties.GetOrAdd<float>("v:heat", 0.0f);
        scalar.Vector() = {0.0f, 1.0f, 0.0f};
        auto texcoords = mesh.VertexSource.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f});
        texcoords.Vector() = {
            {0.0f, 0.0f},
            {0.0f, 0.0f},
            {0.0f, 0.0f},
        };

        Runtime::MeshAttributeTextureBakeRequest request{};
        request.SourcePropertyName = "v:heat";
        request.ValueKind = Runtime::MeshAttributeTextureBakeValueKind::Scalar;

        const Runtime::MeshAttributeTextureBakeResult result =
            Runtime::BakeMeshAttributeTexture(mesh.View(), request);
        EXPECT_EQ(result.Status, Runtime::MeshAttributeTextureBakeStatus::DegenerateUvTriangles);
        EXPECT_EQ(result.Diagnostics.DegenerateUvTriangleCount, 1u);
    }
}

TEST(RuntimeMeshAttributeTextureBake, GeneratedAssetPathIsStableAcrossDirtyStampRebakes)
{
    Runtime::MeshAttributeTextureBakeRequest request{};
    request.SourcePropertyName = "v:heat";
    request.TargetSemantic = "scalar heat";
    request.SourceDomain = Runtime::MeshAttributeTextureBakeSourceDomain::Vertex;
    request.DirtyStamp = 7u;

    const std::string firstPath =
        Runtime::BuildMeshAttributeTextureBakeAssetPath("mesh asset.obj", request);
    request.DirtyStamp = 8u;
    const std::string secondPath =
        Runtime::BuildMeshAttributeTextureBakeAssetPath("mesh asset.obj", request);

    EXPECT_EQ(firstPath, secondPath);
    EXPECT_NE(firstPath.find("scalar-heat"), std::string::npos);
    EXPECT_NE(firstPath.find("v-heat"), std::string::npos);

    Assets::AssetService service;
    Assets::AssetTexture2DPayload payload{};
    payload.Metadata.Width = 1u;
    payload.Metadata.Height = 1u;
    payload.Metadata.Components = 1u;
    payload.Metadata.PixelFormat = Assets::AssetTexturePixelFormat::R8Unorm;
    payload.Metadata.ColorSpace = Assets::AssetTextureColorSpace::Linear;
    payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::Generated;
    payload.PixelBytes = {std::byte{0x11}};

    auto firstId = service.Load<Assets::AssetTexture2DPayload>(
        firstPath,
        [payload](std::string_view, Assets::AssetId) -> Core::Expected<Assets::AssetTexture2DPayload>
        {
            return payload;
        });
    ASSERT_TRUE(firstId.has_value());

    payload.PixelBytes = {std::byte{0x22}};
    auto secondId = service.Load<Assets::AssetTexture2DPayload>(
        secondPath,
        [payload](std::string_view, Assets::AssetId) -> Core::Expected<Assets::AssetTexture2DPayload>
        {
            return payload;
        });
    ASSERT_TRUE(secondId.has_value());
    EXPECT_EQ(*firstId, *secondId);
    EXPECT_EQ(service.LiveAssetCount(), 1u);
}
