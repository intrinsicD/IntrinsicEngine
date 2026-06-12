#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Geometry.Properties;

namespace Assets = Extrinsic::Assets;
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
