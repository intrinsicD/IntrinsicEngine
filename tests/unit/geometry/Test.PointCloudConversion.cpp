#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Geometry.MeshSoup;
import Geometry.PointCloud;
import Geometry.PointCloud.Conversion;
import Geometry.Properties;

namespace
{
    using Geometry::PointCloud::Cloud;
    using Geometry::PointCloud::Conversion::ConversionDiagnosticKind;
    using Geometry::MeshSoup::IndexedMesh;
    using Geometry::MeshSoup::ValidationDiagnosticKind;

    void AddPoint(Cloud& cloud, glm::vec3 position)
    {
        static_cast<void>(cloud.AddPoint(position));
    }

    [[nodiscard]] Cloud MakeFourPointCloud()
    {
        Cloud cloud;
        AddPoint(cloud, {0.0f, 0.0f, 0.0f});
        AddPoint(cloud, {1.0f, 0.0f, 0.0f});
        AddPoint(cloud, {1.0f, 1.0f, 0.0f});
        AddPoint(cloud, {0.0f, 1.0f, 0.0f});
        return cloud;
    }

    [[nodiscard]] IndexedMesh MakeTriangleSoup()
    {
        IndexedMesh mesh;
        static_cast<void>(mesh.AddVertex({0.0f, 0.0f, 0.0f}));
        static_cast<void>(mesh.AddVertex({1.0f, 0.0f, 0.0f}));
        static_cast<void>(mesh.AddVertex({0.0f, 1.0f, 0.0f}));
        static_cast<void>(mesh.AddTriangle(0u, 1u, 2u));
        return mesh;
    }

    [[nodiscard]] bool HasDiagnostic(const auto& diagnostics, ConversionDiagnosticKind kind) noexcept
    {
        return std::ranges::any_of(diagnostics, [kind](const auto& diagnostic) {
            return diagnostic.Kind == kind;
        });
    }
}

TEST(PointCloudConversion, EmptyCloudRoundTripsThroughSoup)
{
    const Cloud cloud;

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(cloud);
    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_TRUE(soupResult.Diagnostics.empty());
    EXPECT_EQ(soupResult.Mesh.VertexCount(), 0u);
    EXPECT_EQ(soupResult.Mesh.FaceCount(), 0u);

    const auto cloudResult = Geometry::PointCloud::Conversion::ToPointCloud(soupResult.Mesh);
    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_TRUE(cloudResult.Diagnostics.empty());
    EXPECT_EQ(cloudResult.Cloud.VertexCount(), 0u);
}

TEST(PointCloudConversion, CloudPositionsRoundTripPreservesOrder)
{
    const Cloud original = MakeFourPointCloud();

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(original);
    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_TRUE(soupResult.Diagnostics.empty());
    EXPECT_EQ(soupResult.Mesh.VertexCount(), 4u);
    EXPECT_EQ(soupResult.Mesh.FaceCount(), 0u);

    const auto cloudResult = Geometry::PointCloud::Conversion::ToPointCloud(soupResult.Mesh);
    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_TRUE(cloudResult.Diagnostics.empty());
    ASSERT_EQ(cloudResult.Cloud.VertexCount(), 4u);

    for (std::size_t i = 0u; i < 4u; ++i)
    {
        const auto handle = Cloud::Handle(i);
        EXPECT_EQ(cloudResult.Cloud.Position(handle), original.Position(handle));
    }
}

TEST(PointCloudConversion, DeletedPointsAreOmittedWithDiagnostic)
{
    Cloud cloud = MakeFourPointCloud();
    cloud.DeletePoint(Cloud::Handle(1u));
    cloud.DeletePoint(Cloud::Handle(3u));

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(cloud);

    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_EQ(soupResult.Mesh.VertexCount(), 2u);
    EXPECT_TRUE(HasDiagnostic(soupResult.Diagnostics, ConversionDiagnosticKind::DeletedPointsOmitted));

    EXPECT_EQ(soupResult.Mesh.Position(0u), (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(soupResult.Mesh.Position(1u), (glm::vec3{1.0f, 1.0f, 0.0f}));
}

TEST(PointCloudConversion, SoupFacesAreDroppedWithDiagnostic)
{
    const IndexedMesh soup = MakeTriangleSoup();

    const auto cloudResult = Geometry::PointCloud::Conversion::ToPointCloud(soup);

    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_EQ(cloudResult.Cloud.VertexCount(), 3u);
    EXPECT_TRUE(HasDiagnostic(cloudResult.Diagnostics, ConversionDiagnosticKind::FacesDropped));
}

TEST(PointCloudConversion, CloudWithBuiltInAttributesEmitsRemapWarnings)
{
    Cloud cloud = MakeFourPointCloud();
    cloud.EnableNormals();
    cloud.EnableColors();
    cloud.EnableRadii();

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(cloud);

    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_EQ(soupResult.Mesh.VertexCount(), 4u);
    const auto remapCount = std::ranges::count_if(soupResult.Diagnostics, [](const auto& diagnostic) {
        return diagnostic.Kind == ConversionDiagnosticKind::AttributeRemapSkipped;
    });
    EXPECT_EQ(remapCount, 3);
}

TEST(PointCloudConversion, CloudWithUserPropertyEmitsRemapWarning)
{
    Cloud cloud = MakeFourPointCloud();
    static_cast<void>(cloud.GetOrAddVertexProperty<float>("v:weight", 0.5f));

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(cloud);

    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_TRUE(HasDiagnostic(soupResult.Diagnostics, ConversionDiagnosticKind::AttributeRemapSkipped));
}

TEST(PointCloudConversion, InvalidSoupIndexHaltsToPointCloud)
{
    IndexedMesh mesh = MakeTriangleSoup();
    static_cast<void>(mesh.AddTriangle(0u, 1u, 99u));

    const auto cloudResult = Geometry::PointCloud::Conversion::ToPointCloud(mesh);

    ASSERT_FALSE(cloudResult.Succeeded());
    EXPECT_EQ(cloudResult.Cloud.VertexCount(), 0u);
    EXPECT_TRUE(std::ranges::any_of(cloudResult.Diagnostics, [](const auto& diagnostic) {
        return diagnostic.Kind == ConversionDiagnosticKind::ValidationDiagnostic &&
               diagnostic.ValidationKind == ValidationDiagnosticKind::InvalidIndex;
    }));
}

TEST(PointCloudConversion, BorrowedViewWithFacePropertiesEmitsRemapWarning)
{
    IndexedMesh mesh = MakeTriangleSoup();
    static_cast<void>(mesh.GetOrAddFaceProperty<float>("f:weight", 1.0f));

    const auto cloudResult = Geometry::PointCloud::Conversion::ToPointCloud(Geometry::MeshSoup::BorrowView(mesh));

    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_TRUE(HasDiagnostic(cloudResult.Diagnostics, ConversionDiagnosticKind::FacesDropped));
    EXPECT_TRUE(HasDiagnostic(cloudResult.Diagnostics, ConversionDiagnosticKind::AttributeRemapSkipped));
}

TEST(PointCloudConversion, SubmeshViewHonorsOffsetAndSize)
{
    Cloud backing;
    AddPoint(backing, {0.0f, 0.0f, 0.0f});
    AddPoint(backing, {1.0f, 0.0f, 0.0f});
    AddPoint(backing, {2.0f, 0.0f, 0.0f});
    AddPoint(backing, {3.0f, 0.0f, 0.0f});
    AddPoint(backing, {4.0f, 0.0f, 0.0f});

    const Cloud view = Cloud::CreateView(backing, Geometry::ElementRange{2u, 2u});
    ASSERT_TRUE(view.IsSubmeshView());
    ASSERT_EQ(view.VerticesSize(), 2u);

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(view);

    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_TRUE(soupResult.Diagnostics.empty());
    ASSERT_EQ(soupResult.Mesh.VertexCount(), 2u);
    EXPECT_EQ(soupResult.Mesh.Position(0u), (glm::vec3{2.0f, 0.0f, 0.0f}));
    EXPECT_EQ(soupResult.Mesh.Position(1u), (glm::vec3{3.0f, 0.0f, 0.0f}));
}

TEST(PointCloudConversion, SubmeshViewSkipsDeletedInRangeAndIgnoresDeletedOutsideRange)
{
    Cloud backing;
    AddPoint(backing, {0.0f, 0.0f, 0.0f});
    AddPoint(backing, {1.0f, 0.0f, 0.0f});
    AddPoint(backing, {2.0f, 0.0f, 0.0f});
    AddPoint(backing, {3.0f, 0.0f, 0.0f});
    AddPoint(backing, {4.0f, 0.0f, 0.0f});
    backing.DeletePoint(Cloud::Handle(0u));  // outside view
    backing.DeletePoint(Cloud::Handle(3u));  // inside view

    const Cloud view = Cloud::CreateView(backing, Geometry::ElementRange{2u, 3u});
    ASSERT_TRUE(view.IsSubmeshView());
    ASSERT_EQ(view.VerticesSize(), 3u);

    const auto soupResult = Geometry::PointCloud::Conversion::ToIndexedMesh(view);

    ASSERT_TRUE(soupResult.Succeeded());
    ASSERT_EQ(soupResult.Mesh.VertexCount(), 2u);
    EXPECT_EQ(soupResult.Mesh.Position(0u), (glm::vec3{2.0f, 0.0f, 0.0f}));
    EXPECT_EQ(soupResult.Mesh.Position(1u), (glm::vec3{4.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(HasDiagnostic(soupResult.Diagnostics, ConversionDiagnosticKind::DeletedPointsOmitted));
    const auto deletedCount = std::ranges::count_if(soupResult.Diagnostics, [](const auto& diagnostic) {
        return diagnostic.Kind == ConversionDiagnosticKind::DeletedPointsOmitted;
    });
    EXPECT_EQ(deletedCount, 1);
}
