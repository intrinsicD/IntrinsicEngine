#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    template <class T>
    concept HasMutableSpan = requires(T value) { value.Span(); };

    template <class T>
    concept HasMutableData = requires(T value) { value.Data(); };

    template <class T>
    concept HasConstSpan = requires(const T value) { value.Span(); };

    template <class T>
    concept HasConstData = requires(const T value) { value.Data(); };

    [[nodiscard]] std::vector<std::uint32_t> CollectIndices(Geometry::LiveElementRange<Geometry::VertexHandle> range)
    {
        std::vector<std::uint32_t> out;
        for (const Geometry::VertexHandle handle : range)
        {
            out.push_back(handle.Index);
        }
        return out;
    }
}

static_assert(std::is_same_v<
              decltype(std::declval<const Geometry::PropertySet&>().Get<float>("v:weight")),
              Geometry::ConstProperty<float>>);
static_assert(!HasMutableSpan<Geometry::Property<bool>>);
static_assert(!HasMutableData<Geometry::Property<bool>>);
static_assert(!HasConstSpan<Geometry::ConstProperty<bool>>);
static_assert(!HasConstData<Geometry::ConstProperty<bool>>);
static_assert(HasMutableSpan<Geometry::Property<float>>);
static_assert(HasMutableData<Geometry::Property<float>>);
static_assert(HasConstSpan<Geometry::ConstProperty<float>>);
static_assert(HasConstData<Geometry::ConstProperty<float>>);

TEST(GeometryPropertiesContract, NameAccessorsReturnStableStringView)
{
    Geometry::PropertySet properties;
    properties.Resize(2u);

    auto scalar = properties.Add<float>("v:weight", 0.0f);
    auto vector = properties.Add<glm::vec3>("v:direction", glm::vec3{0.0f});
    ASSERT_TRUE(scalar.IsValid());
    ASSERT_TRUE(vector.IsValid());

    const Geometry::Property<float> scalarCopy = scalar;
    const Geometry::ConstPropertySet constProperties{properties};
    const auto constVector = constProperties.Get<glm::vec3>("v:direction");
    ASSERT_TRUE(constVector.IsValid());

    EXPECT_EQ(std::string_view{"v:weight"}, scalar.Name());
    EXPECT_EQ(std::string_view{"v:weight"}, scalarCopy.Name());
    EXPECT_EQ(std::string_view{"v:direction"}, vector.Name());
    EXPECT_EQ(std::string_view{"v:direction"}, constVector.Name());
}

TEST(GeometryPropertiesContract, RegistryHandlesFailClosed)
{
    Geometry::PropertySet properties;
    properties.Resize(3u);

    auto weights = properties.Add<float>("v:weight", 1.0f);
    auto ids = properties.Add<std::uint32_t>("v:id", 0u);
    ASSERT_TRUE(weights.IsValid());
    ASSERT_TRUE(ids.IsValid());

    const Geometry::PropertyId weightId = weights.Handle().Id();
    const Geometry::PropertyId idId = ids.Handle().Id();

    EXPECT_TRUE(properties.Registry().Get<float>(weightId).has_value());
    EXPECT_FALSE(properties.Registry().Get<glm::vec3>(weightId).has_value());
    EXPECT_FALSE(properties.Registry().Get<float>(static_cast<Geometry::PropertyId>(99999u)).has_value());

    properties.Remove(weights);
    EXPECT_FALSE(weights.IsValid());
    EXPECT_FALSE(properties.Registry().Get<float>(weightId).has_value());
    EXPECT_FALSE(properties.Registry().Get<float>(idId).has_value());
    EXPECT_TRUE(properties.Registry().Get<std::uint32_t>(idId).has_value());
}

TEST(GeometryPropertiesContract, DefaultConstPropertySetIsSafeEmptyView)
{
    const Geometry::ConstPropertySet invalid;

    EXPECT_FALSE(invalid.IsValid());
    EXPECT_FALSE(static_cast<bool>(invalid));
    EXPECT_EQ(invalid.Size(), 0u);
    EXPECT_TRUE(invalid.Empty());
    EXPECT_FALSE(invalid.Exists("v:any"));
    EXPECT_TRUE(invalid.Properties().empty());
    EXPECT_TRUE(invalid.Descriptors().empty());
    EXPECT_FALSE(invalid.Get<float>("v:any").IsValid());

    Geometry::PropertySet backing;
    const Geometry::ConstPropertySet valid{backing};
    EXPECT_TRUE(valid.IsValid());
    EXPECT_TRUE(valid.Empty());
}

TEST(GeometryPropertiesContract, ConstLookupReturnsReadOnlyProperty)
{
    Geometry::PropertySet properties;
    properties.Resize(2u);
    auto mutableWeights = properties.GetOrAdd<float>("v:weight", 0.0f);
    mutableWeights[0] = 2.0f;
    mutableWeights[1] = 4.0f;

    const Geometry::PropertySet& constProperties = properties;
    const auto weights = constProperties.Get<float>("v:weight");

    ASSERT_TRUE(weights.IsValid());
    EXPECT_EQ(weights.Vector().size(), 2u);
    EXPECT_FLOAT_EQ(weights[0], 2.0f);
    EXPECT_FLOAT_EQ(weights[1], 4.0f);
}

TEST(GeometryPropertiesContract, BoolPropertySupportsScalarProxyAccessOnly)
{
    Geometry::PropertySet properties;
    properties.Resize(4u);

    auto selected = properties.GetOrAdd<bool>("v:selected", false);
    ASSERT_TRUE(selected.IsValid());

    selected[1] = true;
    selected[3] = true;

    EXPECT_FALSE(static_cast<bool>(selected[0]));
    EXPECT_TRUE(static_cast<bool>(selected[1]));
    EXPECT_FALSE(static_cast<bool>(selected[2]));
    EXPECT_TRUE(static_cast<bool>(selected[3]));
    EXPECT_EQ(selected.Vector().size(), 4u);
}

TEST(GeometryPropertiesContract, DescriptorCatalogReportsTypeAndAccessMetadata)
{
    Geometry::PropertySet properties;
    properties.Resize(5u);
    (void)properties.GetOrAdd<float>("v:weight", 0.0f);
    (void)properties.GetOrAdd<glm::vec3>("v:direction", glm::vec3{0.0f});
    (void)properties.GetOrAdd<bool>("v:selected", false);

    const auto descriptors = properties.Descriptors();
    ASSERT_EQ(descriptors.size(), 3u);

    const auto find = [&descriptors](std::string_view name) -> const Geometry::PropertyDescriptor*
    {
        const auto it = std::find_if(descriptors.begin(), descriptors.end(), [name](const Geometry::PropertyDescriptor& descriptor)
        {
            return descriptor.Name == name;
        });
        return it != descriptors.end() ? &*it : nullptr;
    };

    const auto* weight = find("v:weight");
    const auto* direction = find("v:direction");
    const auto* selected = find("v:selected");
    ASSERT_NE(weight, nullptr);
    ASSERT_NE(direction, nullptr);
    ASSERT_NE(selected, nullptr);

    EXPECT_EQ(weight->ValueKind, Geometry::PropertyValueKind::Float);
    EXPECT_EQ(direction->ValueKind, Geometry::PropertyValueKind::Vec3);
    EXPECT_EQ(selected->ValueKind, Geometry::PropertyValueKind::Bool);
    EXPECT_EQ(weight->ElementCount, 5u);
    EXPECT_TRUE(weight->Mutable);
    EXPECT_TRUE(weight->SupportsContiguousSpan);
    EXPECT_TRUE(weight->SupportsRawData);
    EXPECT_FALSE(selected->SupportsContiguousSpan);
    EXPECT_FALSE(selected->SupportsRawData);

    const Geometry::ConstPropertySet constView{properties};
    const auto constDescriptors = constView.Descriptors();
    ASSERT_EQ(constDescriptors.size(), 3u);
    EXPECT_TRUE(std::none_of(constDescriptors.begin(), constDescriptors.end(), [](const Geometry::PropertyDescriptor& descriptor)
    {
        return descriptor.Mutable;
    }));
}

TEST(GeometryPropertiesContract, ShrinkToFitIsCanonicalSpelling)
{
    Geometry::PropertySet properties;
    properties.Resize(2u);
    auto ids = properties.GetOrAdd<std::uint32_t>("v:id", 0u);
    ASSERT_TRUE(ids.IsValid());

    properties.ShrinkToFit();
    EXPECT_TRUE(properties.Get<std::uint32_t>("v:id").IsValid());
}

TEST(GeometryPropertiesContract, LiveElementRangeSkipsDeletedAndFailsClosed)
{
    Geometry::LiveElementRange<Geometry::VertexHandle> missingPredicate{5u, {}};
    EXPECT_TRUE(CollectIndices(missingPredicate).empty());

    Geometry::LiveElementRange<Geometry::VertexHandle> range{1u, 6u, [](Geometry::VertexHandle handle)
    {
        return (handle.Index % 2u) == 1u;
    }};

    EXPECT_EQ(CollectIndices(range), (std::vector<std::uint32_t>{2u, 4u, 6u}));
}

TEST(GeometryPropertiesContract, MeshLiveRangesMatchManualDeletedFilter)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto v0 = mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
    const auto v3 = mesh.AddVertex(glm::vec3{1.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(mesh.AddTriangle(v1, v3, v2).has_value());

    mesh.DeleteFace(Geometry::FaceHandle{0u});

    std::vector<std::uint32_t> manualFaces;
    for (std::uint32_t i = 0; i < mesh.FacesSize(); ++i)
    {
        const Geometry::FaceHandle face{i};
        if (!mesh.IsDeleted(face))
        {
            manualFaces.push_back(i);
        }
    }

    std::vector<std::uint32_t> rangedFaces;
    for (const Geometry::FaceHandle face : mesh.LiveFaces())
    {
        rangedFaces.push_back(face.Index);
    }

    EXPECT_EQ(rangedFaces, manualFaces);

    const auto view = Geometry::HalfedgeMesh::Mesh::CreateView(mesh, Geometry::ElementRange{1u, 2u}, {}, {});
    EXPECT_EQ(CollectIndices(view.LiveVertices()), (std::vector<std::uint32_t>{1u, 2u}));
}

TEST(GeometryPropertiesContract, ConstDomainViewsExposeReadOnlyLiveRanges)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto v0 = mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    Geometry::DomainViews::ConstMeshBackedGraphView graphView{mesh};
    Geometry::DomainViews::ConstMeshBackedCloudView cloudView{mesh};

    EXPECT_EQ(CollectIndices(graphView.LiveVertices()), (std::vector<std::uint32_t>{0u, 1u, 2u}));
    EXPECT_EQ(CollectIndices(cloudView.LivePoints()), (std::vector<std::uint32_t>{0u, 1u, 2u}));
}
