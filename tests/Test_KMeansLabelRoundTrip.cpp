#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

import Graphics;
import Geometry;
import ECS;
import Runtime.PointCloudKMeans;

namespace
{
    [[nodiscard]] std::shared_ptr<Geometry::Halfedge::Mesh> MakeTriangleMesh()
    {
        auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
        const auto v0 = mesh->AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh->AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh->AddVertex({0.0f, 1.0f, 0.0f});
        EXPECT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());
        return mesh;
    }
}

TEST(KMeansLabelRoundTrip, MeshVertexPublicationPreservesLabelsAndColormapContract)
{
    auto mesh = MakeTriangleMesh();

    ECS::Mesh::Data meshData{};
    meshData.MeshRef = mesh;
    meshData.AttributesDirty = false;

    Geometry::KMeans::Result result{};
    result.Labels = {2u, 0u, 2u};
    result.SquaredDistances = {0.25f, 0.5f, 0.75f};
    result.Iterations = 7u;
    result.Converged = true;
    result.Inertia = 1.5f;
    result.MaxDistanceIndex = 1u;
    result.ActualBackend = Geometry::KMeans::Backend::CPU;

    ASSERT_TRUE(Runtime::PointCloudKMeans::PublishMeshVertexResult(meshData, result, 3.25));

    EXPECT_EQ(meshData.Visualization.VertexColors.PropertyName, "v:kmeans_label_f");
    EXPECT_TRUE(meshData.Visualization.VertexColors.AutoRange);
    EXPECT_TRUE(meshData.Visualization.UseNearestVertexColors);
    EXPECT_TRUE(meshData.AttributesDirty);
    EXPECT_EQ(meshData.KMeansLastIterations, 7u);
    EXPECT_TRUE(meshData.KMeansLastConverged);
    EXPECT_FLOAT_EQ(meshData.KMeansLastInertia, 1.5f);
    EXPECT_EQ(meshData.KMeansLastMaxDistanceIndex, 1u);
    EXPECT_DOUBLE_EQ(meshData.KMeansLastDurationMs, 3.25);

    const auto labels = Geometry::VertexProperty<uint32_t>(mesh->VertexProperties().Get<uint32_t>("v:kmeans_label"));
    const auto labelFloats = Geometry::VertexProperty<float>(mesh->VertexProperties().Get<float>("v:kmeans_label_f"));
    const auto labelColors = Geometry::VertexProperty<glm::vec4>(mesh->VertexProperties().Get<glm::vec4>("v:kmeans_color"));
    ASSERT_TRUE(labels.IsValid());
    ASSERT_TRUE(labelFloats.IsValid());
    ASSERT_TRUE(labelColors.IsValid());

    EXPECT_EQ(labels[Geometry::VertexHandle{0u}], 2u);
    EXPECT_EQ(labels[Geometry::VertexHandle{1u}], 0u);
    EXPECT_EQ(labels[Geometry::VertexHandle{2u}], 2u);
    EXPECT_FLOAT_EQ(labelFloats[Geometry::VertexHandle{0u}], 2.0f);
    EXPECT_FLOAT_EQ(labelFloats[Geometry::VertexHandle{1u}], 0.0f);
    EXPECT_FLOAT_EQ(labelFloats[Geometry::VertexHandle{2u}], 2.0f);

    auto colorConfig = meshData.Visualization.VertexColors;
    colorConfig.Map = Graphics::Colormap::Type::Viridis;
    const auto mapped = Graphics::ColorMapper::MapProperty(mesh->VertexProperties(), colorConfig);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_EQ(mapped->Colors.size(), 3u);
    EXPECT_EQ(mapped->Colors[0], mapped->Colors[2]);
    EXPECT_NE(mapped->Colors[0], mapped->Colors[1]);
}

TEST(KMeansLabelRoundTrip, PointCloudPublicationPreservesLabelsAndDirectColorContract)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.0f, 0.0f, 0.0f});
    cloud->AddPoint({1.0f, 0.0f, 0.0f});
    cloud->AddPoint({0.0f, 1.0f, 0.0f});

    ECS::PointCloud::Data pointCloudData{};
    pointCloudData.CloudRef = cloud;

    Geometry::KMeans::Result result{};
    result.Labels = {1u, 0u, 1u};
    result.SquaredDistances = {0.1f, 0.2f, 0.3f};
    result.Iterations = 4u;
    result.Converged = false;
    result.Inertia = 0.6f;
    result.MaxDistanceIndex = 2u;
    result.ActualBackend = Geometry::KMeans::Backend::CPU;

    ASSERT_TRUE(Runtime::PointCloudKMeans::PublishPointCloudPointResult(pointCloudData, result, 1.75));

    EXPECT_EQ(pointCloudData.Visualization.VertexColors.PropertyName, "p:kmeans_color");
    EXPECT_EQ(pointCloudData.KMeansLastIterations, 4u);
    EXPECT_FALSE(pointCloudData.KMeansLastConverged);
    EXPECT_FLOAT_EQ(pointCloudData.KMeansLastInertia, 0.6f);
    EXPECT_EQ(pointCloudData.KMeansLastMaxDistanceIndex, 2u);
    EXPECT_DOUBLE_EQ(pointCloudData.KMeansLastDurationMs, 1.75);

    const auto labels = cloud->GetVertexProperty<uint32_t>("p:kmeans_label");
    const auto colors = cloud->GetVertexProperty<glm::vec4>("p:kmeans_color");
    ASSERT_TRUE(labels.IsValid());
    ASSERT_TRUE(colors.IsValid());

    EXPECT_EQ(labels[Geometry::PointCloud::Cloud::Handle(0)], 1u);
    EXPECT_EQ(labels[Geometry::PointCloud::Cloud::Handle(1)], 0u);
    EXPECT_EQ(labels[Geometry::PointCloud::Cloud::Handle(2)], 1u);

    auto colorConfig = pointCloudData.Visualization.VertexColors;
    const auto mapped = Graphics::ColorMapper::MapProperty(cloud->PointProperties(), colorConfig);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_EQ(mapped->Colors.size(), 3u);
    EXPECT_EQ(mapped->Colors[0], mapped->Colors[2]);
    EXPECT_NE(mapped->Colors[0], mapped->Colors[1]);
}
