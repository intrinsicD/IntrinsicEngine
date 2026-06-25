#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.PointCloudGeometryPacker;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Properties;

using Extrinsic::ECS::Components::GeometrySources::ConstSourceView;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::GeometrySources::Vertices;
using Extrinsic::Runtime::PackCloud;
using Extrinsic::Runtime::PointCloudPackBuffer;
using Extrinsic::Runtime::PointCloudPackResult;
using Extrinsic::Runtime::PointCloudPackStatus;
using Extrinsic::Runtime::PointCloudVertex;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    struct CloudScratch
    {
        Vertices VertexSource{};

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::PointCloud;
            view.VertexSource = &VertexSource;
            return view;
        }
    };

    void SetPositions(Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetNormals(Vertices& v, const std::vector<glm::vec3>& normals)
    {
        auto normal = v.Properties.GetOrAdd<glm::vec3>(
            std::string{pn::kNormal}, glm::vec3(0.0f, 0.0f, 1.0f));
        normal.Vector() = normals;
    }

    void SetVec3Property(Vertices& v,
                         const std::string& name,
                         const std::vector<glm::vec3>& values)
    {
        auto prop = v.Properties.GetOrAdd<glm::vec3>(name, glm::vec3(0.0f));
        prop.Vector() = values;
    }

    void SetVec4Property(Vertices& v,
                         const std::string& name,
                         const std::vector<glm::vec4>& values)
    {
        auto prop = v.Properties.GetOrAdd<glm::vec4>(name, glm::vec4(1.0f));
        prop.Vector() = values;
    }

    [[nodiscard]] PointCloudVertex ReadVertex(const PointCloudPackBuffer& buffer, std::size_t i)
    {
        PointCloudVertex v{};
        std::memcpy(&v, buffer.VertexBytes.data() + i * sizeof(PointCloudVertex), sizeof(PointCloudVertex));
        return v;
    }
}

TEST(PointCloudGeometryPacker, PacksPositionsWithoutIndices)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);

    ASSERT_EQ(result.Status, PointCloudPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 3u);
    EXPECT_TRUE(result.Upload->SurfaceIndices.empty());
    EXPECT_TRUE(result.Upload->LineIndices.empty());
    EXPECT_EQ(result.Upload->PositionBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    EXPECT_EQ(result.Upload->TexcoordBytes.size_bytes(), sizeof(glm::vec2) * 3u);
    EXPECT_TRUE(result.Upload->NormalBytes.empty());

    const PointCloudVertex v0 = ReadVertex(buffer, 0);
    const PointCloudVertex v1 = ReadVertex(buffer, 1);
    EXPECT_FLOAT_EQ(v0.Px, 0.0f);
    EXPECT_FLOAT_EQ(v1.Px, 1.0f);
    EXPECT_FLOAT_EQ(v0.U, 0.0f);
    EXPECT_FLOAT_EQ(v0.V, 0.0f);

    ASSERT_EQ(result.Upload->PositionBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    const auto* positions = reinterpret_cast<const glm::vec3*>(result.Upload->PositionBytes.data());
    EXPECT_FLOAT_EQ(positions[0].x, 0.0f);
    EXPECT_FLOAT_EQ(positions[1].x, 1.0f);
}

TEST(PointCloudGeometryPacker, DoesNotEncodeNormalsIntoUv)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    });
    SetNormals(c.VertexSource, {
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
    });

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);

    ASSERT_EQ(result.Status, PointCloudPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const PointCloudVertex zNormal = ReadVertex(buffer, 0);
    EXPECT_FLOAT_EQ(zNormal.U, 0.0f);
    EXPECT_FLOAT_EQ(zNormal.V, 0.0f);

    const PointCloudVertex xNormal = ReadVertex(buffer, 1);
    EXPECT_FLOAT_EQ(xNormal.U, 0.0f);
    EXPECT_FLOAT_EQ(xNormal.V, 0.0f);
}

TEST(PointCloudGeometryPacker, WrongDomainFailsClosed)
{
    ConstSourceView view{};

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(view, buffer);
    EXPECT_EQ(result.Status, PointCloudPackStatus::WrongDomain);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(PointCloudGeometryPacker, MissingPositionsFailsClosed)
{
    CloudScratch c{};
    // VertexSource present but no `v:position` slot.

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);
    EXPECT_EQ(result.Status, PointCloudPackStatus::MissingPositions);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(PointCloudGeometryPacker, EmptyCloudFailsClosed)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {});

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);
    EXPECT_EQ(result.Status, PointCloudPackStatus::EmptyCloud);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(PointCloudGeometryPacker, NonFinitePositionFailsClosed)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
    });

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);
    EXPECT_EQ(result.Status, PointCloudPackStatus::NonFinitePosition);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(PointCloudGeometryPacker, ChannelBindingsPublishNormalAndColorStreams)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    });
    SetVec3Property(c.VertexSource, "v:custom_normal", {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });
    SetVec4Property(c.VertexSource, "v:paint", {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
    });
    Extrinsic::Runtime::VertexChannelBindingSet bindings{};
    bindings.Normal = Extrinsic::Runtime::VertexChannelSourceBinding{
        .Enabled = true,
        .SourceType = Extrinsic::Runtime::AttributeSourceType::Vec3,
        .SourceProperty = "v:custom_normal",
    };
    bindings.Color = Extrinsic::Runtime::VertexChannelSourceBinding{
        .Enabled = true,
        .SourceType = Extrinsic::Runtime::AttributeSourceType::Vec4,
        .SourceProperty = "v:paint",
    };

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), &bindings, buffer);

    ASSERT_EQ(result.Status, PointCloudPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    ASSERT_EQ(result.Upload->NormalBytes.size_bytes(), sizeof(glm::vec3) * 2u);
    const auto* normals =
        reinterpret_cast<const glm::vec3*>(result.Upload->NormalBytes.data());
    EXPECT_FLOAT_EQ(normals[0].x, 1.0f);
    EXPECT_FLOAT_EQ(normals[1].y, 1.0f);
    ASSERT_EQ(result.Upload->PackedVertexColors.size(), 2u);
    EXPECT_EQ(result.Upload->PackedVertexColors[0], 0xFF0000FFu);
    EXPECT_EQ(result.Upload->PackedVertexColors[1], 0xFF00FF00u);
}

TEST(PointCloudGeometryPacker, LocalSphereCoversPointBounds)
{
    CloudScratch c{};
    SetPositions(c.VertexSource, {{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});

    PointCloudPackBuffer buffer{};
    const PointCloudPackResult result = PackCloud(c.View(), buffer);
    ASSERT_EQ(result.Status, PointCloudPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const glm::vec4 sphere = result.Upload->LocalBounds.LocalSphere;
    EXPECT_FLOAT_EQ(sphere.x, 0.0f);
    EXPECT_FLOAT_EQ(sphere.w, 1.0f);
}
