#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"
#include "PointDomainFixture.hpp"
import Extrinsic.Runtime.PropertyInspectionOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;

namespace
{
    namespace R = Extrinsic::Runtime;
    using D = R::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    constexpr std::array domains{D::MeshVertex, D::MeshEdge, D::MeshHalfedge, D::MeshFace,
                                D::GraphNode, D::GraphHalfedge, D::GraphEdge, D::PointCloudPoint};
    struct Fixture
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        Intrinsic::Tests::EditorFeatureTestContext Context{.Scene = &Scene, .CommandHistory = &History};
    };
}

TEST(PropertyInspection, EveryCanonicalDomainUsesCopiedReadOnlyQueries)
{
    for (const auto domain : domains)
    {
        SCOPED_TRACE(static_cast<unsigned>(domain));
        Fixture f;
        const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, domain);
        const auto id = R::SelectionController::ToStableEntityId(entity);
        auto& properties = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, domain);
        auto values = properties.GetOrAdd<float>("arbitrary scalar");
        std::ranges::fill(values.Vector(), 2.0f);
        const auto revision = properties.Revision();
        const auto history = f.History.Snapshot();
        const R::GeometryPropertyRef ref{domain, "arbitrary scalar", K::Float};
        const auto catalog = R::GetEditorPropertyCatalog(f.Context, id);
        ASSERT_TRUE(catalog.Success);
        EXPECT_TRUE(std::ranges::any_of(catalog.Catalog.Entries, [&](const auto& entry) { return entry.Ref == ref; }));
        const auto statistics = R::GetEditorPropertyStatistics(f.Context, id, ref, 4);
        ASSERT_TRUE(statistics.Success);
        ASSERT_EQ(statistics.Statistics.Components.size(), 1u);
        EXPECT_DOUBLE_EQ(statistics.Statistics.Components[0].Mean, 2.0);
        const auto comparison = R::CompareEditorProperties(f.Context, id, ref, ref);
        ASSERT_TRUE(comparison.Success);
        EXPECT_EQ(comparison.Comparison.IdenticalRows, properties.Size());
        const auto page = R::ReadEditorPropertyValues(f.Context, id, ref, 0, 1);
        ASSERT_TRUE(page.Success);
        ASSERT_EQ(page.Rows.size(), 1u);
        EXPECT_DOUBLE_EQ(std::get<double>(page.Rows[0].Components[0]), 2.0);
        EXPECT_EQ(properties.Revision(), revision);
        EXPECT_EQ(f.History.Snapshot().Revision, history.Revision);
        EXPECT_EQ(f.History.UndoCount(), history.UndoCount);
        EXPECT_FALSE(f.History.IsDirty());
        values[0] = 99.0f;
        EXPECT_DOUBLE_EQ(std::get<double>(page.Rows[0].Components[0]), 2.0);
    }
}

TEST(PropertyInspection, PagingIsBoundedAndRetainsExactValuesAndDeletedSlots)
{
    Fixture f;
    const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, D::PointCloudPoint);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto& p = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, D::PointCloudPoint);
    auto integers = p.GetOrAdd<std::uint64_t>("labels");
    integers[1] = std::numeric_limits<std::uint64_t>::max();
    p.GetOrAdd<bool>("v:deleted")[1] = true;
    R::GeometryPropertyRef ref{D::PointCloudPoint, "labels", K::UInt64};
    auto page = R::ReadEditorPropertyValues(f.Context, id, ref, 1, 2);
    ASSERT_TRUE(page.Success);
    ASSERT_EQ(page.Rows.size(), 2u);
    EXPECT_EQ(page.Rows[0].Index, 1u);
    EXPECT_TRUE(page.Rows[0].Deleted);
    EXPECT_EQ(std::get<std::string>(page.Rows[0].Components[0]), "18446744073709551615");
    EXPECT_TRUE(page.HasMore);
    for (auto offset : {p.Size(), std::numeric_limits<std::size_t>::max()})
    {
        page = R::ReadEditorPropertyValues(f.Context, id, ref, offset, R::MaxEditorPropertyValues);
        EXPECT_TRUE(page.Success);
        EXPECT_TRUE(page.Rows.empty());
        EXPECT_FALSE(page.HasMore);
    }
    EXPECT_FALSE(R::ReadEditorPropertyValues(f.Context, id, ref, 0, 0).Success);
    EXPECT_FALSE(R::ReadEditorPropertyValues(f.Context, id, ref, 0, R::MaxEditorPropertyValues + 1).Success);
    auto vectors = p.GetOrAdd<glm::vec3>("vectors");
    vectors[0] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()};
    page = R::ReadEditorPropertyValues(f.Context, id, {D::PointCloudPoint, "vectors", K::Vec3}, 0, 1);
    ASSERT_TRUE(page.Success);
    ASSERT_EQ(page.Rows[0].Components.size(), 3u);
    EXPECT_EQ(std::get<std::string>(page.Rows[0].Components[0]), "NaN");
    EXPECT_EQ(std::get<std::string>(page.Rows[0].Components[1]), "+Infinity");
    EXPECT_EQ(std::get<std::string>(page.Rows[0].Components[2]), "-Infinity");
}

TEST(PropertyInspection, StatisticsAndComparisonShareDeletionAndShapeContracts)
{
    Fixture f;
    const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, D::PointCloudPoint);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto& p = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, D::PointCloudPoint);
    p.GetOrAdd<float>("input").Vector() = {0, 2, 4, 6, 100};
    p.GetOrAdd<double>("smoothed").Vector() = {1, 2, 3, 6, -100};
    p.GetOrAdd<bool>("v:deleted")[4] = true;
    const R::GeometryPropertyRef input{D::PointCloudPoint, "input", K::Float};
    const R::GeometryPropertyRef output{D::PointCloudPoint, "smoothed", K::Double};
    auto result = R::CompareEditorProperties(f.Context, id, input, output);
    ASSERT_TRUE(result.Success);
    EXPECT_EQ(result.Comparison.ComparableRows, 4u);
    EXPECT_EQ(result.Comparison.DeletedCount, 1u);
    EXPECT_EQ(result.Comparison.IdenticalRows, 2u);
    EXPECT_DOUBLE_EQ(result.Comparison.MaxAbsError, 1.0);
    EXPECT_DOUBLE_EQ(result.Comparison.MeanAbsError, 0.5);
    const auto stats = R::GetEditorPropertyStatistics(f.Context, id, input, 2);
    ASSERT_TRUE(stats.Success);
    EXPECT_EQ(stats.Statistics.Count, 4u);
    EXPECT_EQ(stats.Statistics.DeletedCount, 1u);
    EXPECT_EQ(stats.Statistics.Components[0].Histogram.Counts, (std::vector<std::size_t>{2, 2}));
    EXPECT_FALSE(R::GetEditorPropertyStatistics(f.Context, id, input, R::MaxEditorPropertyHistogramBins + 1).Success);
    p.GetOrAdd<glm::vec2>("vector");
    EXPECT_FALSE(R::CompareEditorProperties(f.Context, id, input, {D::PointCloudPoint, "vector", K::Vec2}).Success);
}

TEST(PropertyInspection, MissingStaleAndMismatchedInputsProduceDiagnostics)
{
    Fixture f;
    const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, D::PointCloudPoint);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto& p = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, D::PointCloudPoint);
    p.GetOrAdd<float>("scalar");
    R::GeometryPropertyRef ref{D::PointCloudPoint, "scalar", K::Double};
    auto statistics = R::GetEditorPropertyStatistics(f.Context, id, ref);
    EXPECT_FALSE(statistics.Success);
    ASSERT_FALSE(statistics.Diagnostics.empty());
    EXPECT_NE(statistics.Diagnostics.front().Message.find("kind mismatch"), std::string::npos);
    ref.ValueKind = K::Unknown;
    statistics = R::GetEditorPropertyStatistics(f.Context, id, ref);
    EXPECT_TRUE(statistics.Success);
    EXPECT_EQ(statistics.Property.ValueKind, K::Float);
    ref.Name = "missing";
    EXPECT_FALSE(R::ReadEditorPropertyValues(f.Context, id, ref).Success);
    EXPECT_FALSE(R::GetEditorPropertyCatalog({}, id).Success);
    f.Scene.Destroy(entity);
    const auto catalog = R::GetEditorPropertyCatalog(f.Context, id);
    EXPECT_FALSE(catalog.Success);
    EXPECT_FALSE(catalog.Diagnostics.empty());
    bool active = true;
    const auto commands = R::BindEditorProcessingCommands({.Scene = &f.Scene, .AttachmentActive = [&] { return active; }});
    active = false;
    EXPECT_FALSE(R::GetEditorPropertyCatalog(commands, id).Success);
}

TEST(PropertyInspection, MalformedStorageAndDeletionMasksFailClosed)
{
    Fixture f;
    const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, D::PointCloudPoint);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto& p = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, D::PointCloudPoint);
    auto scalar = p.GetOrAdd<float>("scalar");
    const R::GeometryPropertyRef ref{D::PointCloudPoint, "scalar", K::Float};
    scalar.Vector().resize(1);
    EXPECT_FALSE(R::ReadEditorPropertyValues(f.Context, id, ref).Success);
    EXPECT_FALSE(R::GetEditorPropertyStatistics(f.Context, id, ref).Success);
    scalar.Vector().resize(p.Size());
    p.GetOrAdd<float>("bad:deleted");
    EXPECT_FALSE(R::ReadEditorPropertyValues(f.Context, id, ref).Success);
    EXPECT_FALSE(R::GetEditorPropertyStatistics(f.Context, id, ref).Success);
}

TEST(PropertyInspection, ComparesPublishedSmoothingOutputWithoutChangingHistory)
{
    Fixture f;
    const auto entity = Intrinsic::Tests::MakePointDomainSource(f.Scene, D::MeshVertex);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto& p = Intrinsic::Tests::PointDomainProperties(f.Scene, entity, D::MeshVertex);
    auto input = p.GetOrAdd<double>("input");
    for (std::size_t i = 0; i < p.Size(); ++i) input[i] = static_cast<double>(i);
    R::PropertySmoothingConfig config;
    config.Input = {D::MeshVertex, "input", K::Double};
    config.Output = {D::MeshVertex, "smoothed", K::Double};
    const auto applied = R::ApplyEditorPropertySmoothingCommand(f.Context, id, config);
    ASSERT_TRUE(applied.Succeeded()) << applied.Message;
    const auto history = f.History.Snapshot();
    const auto comparison = R::CompareEditorProperties(f.Context, id, config.Input, config.Output);
    ASSERT_TRUE(comparison.Success);
    EXPECT_EQ(comparison.Comparison.ComparableRows, p.Size());
    EXPECT_GT(comparison.Comparison.MaxAbsError, 0);
    EXPECT_EQ(f.History.Snapshot().Revision, history.Revision);
    EXPECT_EQ(f.History.UndoCount(), history.UndoCount);
}
