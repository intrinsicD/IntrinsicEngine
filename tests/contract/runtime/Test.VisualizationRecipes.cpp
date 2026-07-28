#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry.Properties;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.VisualizationRecipes;

namespace Assets = Extrinsic::Assets;
namespace G = Extrinsic::Graphics;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    using namespace std::chrono_literals;

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 1)
        {
            if (Extrinsic::Core::Tasks::Scheduler::IsInitialized())
                Extrinsic::Core::Tasks::Scheduler::Shutdown();
            Extrinsic::Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            Extrinsic::Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(Predicate&& predicate,
                                 const std::chrono::milliseconds timeout = 2s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    [[nodiscard]] Geometry::PropertySet MakeScalarProperties()
    {
        Geometry::PropertySet properties;
        properties.Resize(4u);

        auto curvature = properties.Add<float>("curvature", 0.0f);
        curvature[0] = -1.0f;
        curvature[1] = 0.25f;
        curvature[2] = 2.0f;
        curvature[3] = 1.0f;

        auto heat = properties.Add<double>("heat", 0.0);
        heat[0] = 10.0;
        heat[1] = 12.0;
        heat[2] = 11.0;
        heat[3] = 13.0;

        auto color = properties.Add<glm::vec3>("color", glm::vec3{1.0f});
        color[0] = glm::vec3{1.0f, 0.0f, 0.0f};

        auto kmeansColor = properties.Add<glm::vec4>("v:kmeans_color", glm::vec4{1.0f});
        kmeansColor[0] = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        kmeansColor[1] = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
        kmeansColor[2] = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        kmeansColor[3] = glm::vec4{1.0f, 1.0f, 0.0f, 1.0f};

        auto velocity = properties.Add<glm::vec3>("velocity", glm::vec3{0.0f});
        velocity[0] = glm::vec3{1.0f, 0.0f, 0.0f};
        velocity[1] = glm::vec3{0.0f, 1.0f, 0.0f};
        velocity[2] = glm::vec3{0.0f, 0.0f, 1.0f};
        velocity[3] = glm::vec3{-1.0f, 0.0f, 0.0f};

        auto positions =
            properties.Add<glm::vec3>(std::string{PN::kPosition}, glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};
        positions[3] = glm::vec3{0.0f, 0.0f, 1.0f};

        return properties;
    }

    struct RecipeSourceFixture
    {
        GS::Vertices Vertices{};
        GS::Faces Faces{};
        R::GeometryEntityAvailability Availability{};

        RecipeSourceFixture()
        {
            Vertices.Properties = MakeScalarProperties();
            Faces.Properties = MakeScalarProperties();
            Availability = R::BuildGeometryAvailability(GS::ConstSourceView{
                .ActiveDomain = GS::Domain::Mesh,
                .HasMeshTopologyMarker = true,
                .VertexSource = &Vertices,
                .FaceSource = &Faces,
            });
        }

        RecipeSourceFixture(const RecipeSourceFixture&) = delete;
        RecipeSourceFixture& operator=(const RecipeSourceFixture&) = delete;
    };
}

TEST(VisualizationRecipes, EncodesScalarColorAndLabelProperties)
{
    RecipeSourceFixture source{};

    const R::VisualizationEncodingResult scalar = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshFace,
                .Name = "curvature",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
            .OutputName = "curvature_faces",
            .BufferSourceKey = "curvature.upload",
            .DirtyStamp = 17u,
            .Colormap = G::Colormap::Type::Plasma,
        }});
    ASSERT_TRUE(scalar.Succeeded());
    EXPECT_EQ(scalar.Status, R::VisualizationRecipeStatus::Encoded);
    ASSERT_EQ(scalar.Batch.Scalars.size(), 1u);
    ASSERT_EQ(scalar.Batch.PropertyBuffers.size(), 1u);
    EXPECT_EQ(scalar.Batch.Scalars.front().Name, "curvature_faces");
    EXPECT_EQ(scalar.Batch.Scalars.front().SourceBufferKey,
              "curvature.upload");
    EXPECT_EQ(scalar.Batch.Scalars.front().Domain,
              G::VisualizationAttributeDomain::Face);
    EXPECT_EQ(scalar.Batch.Scalars.front().Colormap,
              G::Colormap::Type::Plasma);
    EXPECT_EQ(scalar.Batch.PropertyBuffers.front().DirtyStamp, 17u);

    const R::VisualizationEncodingResult color = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ColorVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "v:kmeans_color",
                .ValueKind = Geometry::PropertyValueKind::Vec4,
            },
            .OutputName = "cluster_color",
            .BufferBDA = 0xC010u,
        }});
    ASSERT_TRUE(color.Succeeded());
    ASSERT_EQ(color.Batch.Colors.size(), 1u);
    EXPECT_EQ(color.Batch.Colors.front().Name, "cluster_color");
    EXPECT_EQ(color.Batch.Colors.front().Domain,
              G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(color.Batch.Colors.front().ColorBufferBDA, 0xC010u);
    EXPECT_TRUE(color.Batch.PropertyBuffers.empty());

    const R::VisualizationEncodingResult labels = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::LabelVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "v:kmeans_color",
                .ValueKind = Geometry::PropertyValueKind::Vec4,
            },
            .OutputName = "cluster_labels",
            .BufferSourceKey = "cluster_labels.upload",
            .DirtyStamp = 21u,
        }});
    ASSERT_TRUE(labels.Succeeded());
    ASSERT_EQ(labels.Batch.Colors.size(), 1u);
    ASSERT_EQ(labels.Batch.PropertyBuffers.size(), 1u);
    EXPECT_EQ(labels.Batch.Colors.front().Name, "cluster_labels");
    EXPECT_EQ(labels.Batch.PropertyBuffers.front().ValueType,
              G::VisualizationValueType::RgbaFloat4);
    EXPECT_EQ(labels.Batch.PropertyBuffers.front().DirtyStamp, 21u);
}

TEST(VisualizationRecipes, EncodesVectorAndIsolineProperties)
{
    RecipeSourceFixture source{};

    const R::VisualizationEncodingResult vector = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::VectorFieldVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "velocity",
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
            .PositionSource = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = std::string{PN::kPosition},
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
            .OutputName = "velocity_glyphs",
            .VectorBufferSourceKey = "velocity.upload",
            .DirtyStamp = 33u,
            .Scale = 2.5f,
            .Color = glm::vec4{0.25f, 0.5f, 0.75f, 1.0f},
            .DepthTested = false,
        }});
    ASSERT_TRUE(vector.Succeeded());
    ASSERT_EQ(vector.Batch.VectorFields.size(), 1u);
    ASSERT_EQ(vector.Batch.PropertyBuffers.size(), 1u);
    const G::VectorFieldOverlayPacket& vectorPacket =
        vector.Batch.VectorFields.front();
    EXPECT_EQ(vectorPacket.PositionBufferSourceKey, PN::kPosition);
    EXPECT_EQ(vectorPacket.VectorBufferSourceKey, "velocity.upload");
    EXPECT_FLOAT_EQ(vectorPacket.Scale, 2.5f);
    EXPECT_FALSE(vectorPacket.DepthTested);

    const R::VisualizationEncodingResult isoline = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::IsolineVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshFace,
                .Name = "heat",
                .ValueKind = Geometry::PropertyValueKind::Double,
            },
            .OutputName = "heat_isolines",
            .BufferSourceKey = "heat.upload",
            .DirtyStamp = 34u,
            .AutoRange = false,
            .RangeMin = 0.0f,
            .RangeMax = 20.0f,
            .IsoValueCount = 5u,
            .LineWidth = 1.5f,
            .Color = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f},
            .DepthTested = false,
        }});
    ASSERT_TRUE(isoline.Succeeded());
    ASSERT_EQ(isoline.Batch.Isolines.size(), 1u);
    ASSERT_EQ(isoline.Batch.PropertyBuffers.size(), 1u);
    const G::IsolineOverlayPacket& isolinePacket =
        isoline.Batch.Isolines.front();
    EXPECT_EQ(isolinePacket.Domain, G::VisualizationAttributeDomain::Face);
    EXPECT_EQ(isolinePacket.IsoValueCount, 5u);
    EXPECT_FLOAT_EQ(isolinePacket.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(isolinePacket.RangeMax, 20.0f);
    EXPECT_FALSE(isolinePacket.DepthTested);
}

TEST(VisualizationRecipes, EncodesAtlasMetadataWithoutSchedulingWork)
{
    RecipeSourceFixture source{};

    const R::VisualizationEncodingResult preview = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::HtexPreviewVisualizationRecipe{
            .Name = "htex.preview",
            .PatchCount = 8u,
            .AtlasWidth = 512u,
            .AtlasHeight = 256u,
        }});
    ASSERT_TRUE(preview.Succeeded());
    ASSERT_EQ(preview.Batch.HtexAtlases.size(), 1u);
    EXPECT_EQ(preview.Batch.HtexAtlases.front().PatchCount, 8u);

    const R::VisualizationEncodingResult bake = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::FragmentBakeVisualizationRecipe{
            .Name = "curvature.bake",
            .Source = {
                .Domain = R::GeometryElementDomain::MeshFace,
                .Name = "curvature",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
            .Mapping = G::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 32u,
            .TexcoordBufferSourceKey = "mesh.texcoords",
            .AtlasTextureAsset = Assets::AssetId{90u, 2u},
            .GeneratedTextureSemantic =
                G::VisualizationGeneratedTextureSemantic::ScalarAttribute,
            .TexcoordDirtyStamp = 41u,
            .SourceAttributeDirtyStamp = 42u,
        }});
    ASSERT_TRUE(bake.Succeeded());
    ASSERT_EQ(bake.Batch.FragmentBakeAtlases.size(), 1u);
    const G::FragmentBakeAtlasPacket& packet =
        bake.Batch.FragmentBakeAtlases.front();
    EXPECT_EQ(packet.SourceAttributeName, "curvature");
    EXPECT_EQ(packet.TexcoordBufferSourceKey, "mesh.texcoords");
    EXPECT_EQ(packet.TexcoordProvenance,
              G::VisualizationTexcoordProvenance::RuntimeResolved);
    EXPECT_EQ(packet.TexcoordDirtyStamp, 41u);
    EXPECT_EQ(packet.SourceAttributeDirtyStamp, 42u);

    const R::VisualizationEncodingResult recreate =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::FragmentBakeVisualizationRecipe{
                .Name = "curvature.recreate",
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshFace,
                    .Name = "curvature",
                    .ValueKind = Geometry::PropertyValueKind::Float,
                },
                .Mapping =
                    G::VisualizationFragmentBakeMapping::RecreateHtex,
                .FaceCount = 4u,
                .AtlasWidth = 64u,
                .AtlasHeight = 64u,
            }});
    ASSERT_TRUE(recreate.Succeeded());
    ASSERT_EQ(recreate.Batch.FragmentBakeAtlases.size(), 1u);
}

TEST(VisualizationRecipes, ReportsDeterministicFailureStatusAndEmptyOutput)
{
    RecipeSourceFixture source{};

    const R::VisualizationEncodingResult empty =
        R::EncodeVisualizationRecipe(source.Availability, {});
    EXPECT_EQ(empty.Status, R::VisualizationRecipeStatus::EmptyRecipe);
    EXPECT_EQ(R::ToString(empty.Status), "EmptyRecipe");
    EXPECT_EQ(empty.Diagnostics.PacketAppendCount, 0u);

    const R::VisualizationEncodingResult missing = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "missing",
            },
        }});
    EXPECT_EQ(missing.Status, R::VisualizationRecipeStatus::MissingSource);
    EXPECT_EQ(R::ToString(missing.Status), "MissingSource");
    EXPECT_TRUE(missing.Batch.Scalars.empty());

    const R::VisualizationEncodingResult wrongType =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "velocity",
                },
            }});
    EXPECT_EQ(wrongType.Status,
              R::VisualizationRecipeStatus::UnsupportedSourceType);
    EXPECT_TRUE(wrongType.Batch.Scalars.empty());

    const R::VisualizationEncodingResult badRange =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::IsolineVisualizationRecipe{
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshFace,
                    .Name = "heat",
                },
                .AutoRange = false,
                .RangeMin = 1.0f,
                .RangeMax = 1.0f,
                .IsoValueCount = 4u,
            }});
    EXPECT_EQ(badRange.Status, R::VisualizationRecipeStatus::InvalidRange);
    EXPECT_TRUE(badRange.Batch.Isolines.empty());

    const R::VisualizationEncodingResult wrongPositionType =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::VectorFieldVisualizationRecipe{
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "velocity",
                },
                .PositionSource = {
                    .Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "curvature",
                },
            }});
    EXPECT_EQ(wrongPositionType.Status,
              R::VisualizationRecipeStatus::UnsupportedSourceType);
    EXPECT_TRUE(wrongPositionType.Batch.VectorFields.empty());

    source.Faces.Properties.Resize(2u);
    const R::VisualizationEncodingResult stalePosition =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::VectorFieldVisualizationRecipe{
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "velocity",
                },
                .PositionSource = {
                    .Domain = R::GeometryElementDomain::MeshFace,
                    .Name = std::string{PN::kPosition},
                },
            }});
    EXPECT_EQ(stalePosition.Status,
              R::VisualizationRecipeStatus::ElementCountMismatch);
    EXPECT_EQ(R::ToString(stalePosition.Status), "ElementCountMismatch");
    EXPECT_TRUE(stalePosition.Batch.VectorFields.empty());

    const R::VisualizationEncodingResult missingTexcoords =
        R::EncodeVisualizationRecipe(
            source.Availability,
            R::VisualizationRecipe{.Data = R::FragmentBakeVisualizationRecipe{
                .Name = "missing.uv",
                .Source = {
                    .Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "curvature",
                },
                .Mapping =
                    G::VisualizationFragmentBakeMapping::ExistingTexcoords,
                .FaceCount = 4u,
                .AtlasWidth = 32u,
                .AtlasHeight = 32u,
            }});
    EXPECT_EQ(missingTexcoords.Status,
              R::VisualizationRecipeStatus::MissingTexcoord);
    EXPECT_TRUE(missingTexcoords.Batch.FragmentBakeAtlases.empty());
}

TEST(VisualizationRecipes, HtexRecreateIsASeparateTypedJobOperation)
{
    SchedulerScope scheduler{1};
    R::JobService jobs{};
    R::KernelEventBus events{};

    const R::VisualizationHtexRecreateResult scheduled =
        R::ScheduleVisualizationHtexRecreate(
            jobs,
            R::VisualizationHtexRecreateRequest{
                .DebugName = "curvature",
                .PayloadToken = 77u,
            });
    ASSERT_TRUE(scheduled.Scheduled());
    EXPECT_TRUE(scheduled.Diagnostic.empty());

    ASSERT_TRUE(WaitUntil([&]
    {
        (void)jobs.DrainCompletions(events);
        return jobs.IsComplete(scheduled.Task);
    }));
    EXPECT_EQ(jobs.GetState(scheduled.Task), R::JobState::Published);
}

TEST(VisualizationRecipes, ScalarRangePoliciesRemainDeterministic)
{
    RecipeSourceFixture source{};

    const R::VisualizationEncodingResult manual = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshFace,
                .Name = "heat",
                .ValueKind = Geometry::PropertyValueKind::Double,
            },
            .AutoRange = false,
            .RangeMin = -5.0f,
            .RangeMax = 20.0f,
        }});
    ASSERT_TRUE(manual.Succeeded());
    ASSERT_EQ(manual.Batch.Scalars.size(), 1u);
    EXPECT_FLOAT_EQ(manual.Batch.Scalars.front().RangeMin, -5.0f);
    EXPECT_FLOAT_EQ(manual.Batch.Scalars.front().RangeMax, 20.0f);
    EXPECT_EQ(manual.Diagnostics.ManualRangeCount, 1u);
    EXPECT_EQ(manual.Diagnostics.ScalarValueScanCount, 4u);

    auto flat = source.Vertices.Properties.Add<float>("flat", 2.0f);
    ASSERT_TRUE(flat.IsValid());
    const R::VisualizationEncodingResult expanded = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "flat",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
        }});
    ASSERT_TRUE(expanded.Succeeded());
    ASSERT_EQ(expanded.Batch.Scalars.size(), 1u);
    EXPECT_FLOAT_EQ(expanded.Batch.Scalars.front().RangeMin, 1.5f);
    EXPECT_FLOAT_EQ(expanded.Batch.Scalars.front().RangeMax, 2.5f);
    EXPECT_EQ(expanded.Diagnostics.FlatAutoRangeExpandedCount, 1u);

    const R::VisualizationEncodingResult exact = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "curvature",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
        }});
    ASSERT_TRUE(exact.Succeeded());
    EXPECT_FLOAT_EQ(exact.Batch.Scalars.front().RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(exact.Batch.Scalars.front().RangeMax, 2.0f);
    EXPECT_EQ(exact.Diagnostics.RobustAutoRangeClampedCount, 0u);

    source.Vertices.Properties.Resize(100u);
    auto heavy = source.Vertices.Properties.Add<float>("heavy", 0.0f);
    ASSERT_TRUE(heavy.IsValid());
    for (std::size_t index = 0u; index < 100u; ++index)
        heavy[index] = static_cast<float>(index);
    heavy[0] = -10'000.0f;
    heavy[99] = 10'000.0f;
    source.Availability = R::BuildGeometryAvailability(GS::ConstSourceView{
        .ActiveDomain = GS::Domain::Mesh,
        .HasMeshTopologyMarker = true,
        .VertexSource = &source.Vertices,
        .FaceSource = &source.Faces,
    });

    const auto heavyRecipe = R::VisualizationRecipe{
        .Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "heavy",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
        }};
    const R::VisualizationEncodingResult robust =
        R::EncodeVisualizationRecipe(source.Availability, heavyRecipe);
    ASSERT_TRUE(robust.Succeeded());
    EXPECT_FLOAT_EQ(robust.Batch.Scalars.front().RangeMin, 1.0f);
    EXPECT_FLOAT_EQ(robust.Batch.Scalars.front().RangeMax, 97.0f);
    EXPECT_EQ(robust.Diagnostics.RobustAutoRangeClampedCount, 1u);

    heavy[50] = std::numeric_limits<float>::quiet_NaN();
    const R::VisualizationEncodingResult nonFinite =
        R::EncodeVisualizationRecipe(source.Availability, heavyRecipe);
    EXPECT_EQ(nonFinite.Status,
              R::VisualizationRecipeStatus::NonFiniteValue);
    EXPECT_EQ(nonFinite.Diagnostics.NonFiniteValueCount, 1u);
    EXPECT_TRUE(nonFinite.Batch.Scalars.empty());
}

TEST(VisualizationRecipes, EncodingBatchAppendOwnsPayloadSpansAndClearResetsAllLanes)
{
    RecipeSourceFixture source{};
    R::VisualizationEncodingResult scalar = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "curvature",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
            .BufferSourceKey = "scalar.upload",
        }});
    R::VisualizationEncodingResult color = R::EncodeVisualizationRecipe(
        source.Availability,
        R::VisualizationRecipe{.Data = R::ColorVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "v:kmeans_color",
                .ValueKind = Geometry::PropertyValueKind::Vec4,
            },
            .BufferSourceKey = "color.upload",
        }});
    ASSERT_TRUE(scalar.Succeeded());
    ASSERT_TRUE(color.Succeeded());

    R::VisualizationEncodingBatch combined{};
    combined.Append(std::move(scalar.Batch));
    combined.Append(std::move(color.Batch));

    ASSERT_EQ(combined.PropertyBuffers.size(), 2u);
    ASSERT_EQ(combined.PropertyBufferPayloads.size(), 2u);
    EXPECT_EQ(combined.PropertyBuffers[0].Bytes.data(),
              combined.PropertyBufferPayloads[0].data());
    EXPECT_EQ(combined.PropertyBuffers[1].Bytes.data(),
              combined.PropertyBufferPayloads[1].data());
    EXPECT_FALSE(combined.PropertyBuffers[0].Bytes.empty());
    EXPECT_FALSE(combined.PropertyBuffers[1].Bytes.empty());

    const G::VisualizationPacketBatch packets = combined.AsPacketBatch();
    EXPECT_EQ(packets.PropertyBuffers.size(), 2u);
    EXPECT_EQ(packets.Scalars.size(), 1u);
    EXPECT_EQ(packets.Colors.size(), 1u);

    combined.Clear();
    EXPECT_TRUE(combined.PropertyBuffers.empty());
    EXPECT_TRUE(combined.PropertyBufferPayloads.empty());
    EXPECT_TRUE(combined.Scalars.empty());
    EXPECT_TRUE(combined.Colors.empty());
    const G::VisualizationPacketBatch cleared = combined.AsPacketBatch();
    EXPECT_TRUE(cleared.PropertyBuffers.empty());
    EXPECT_TRUE(cleared.Scalars.empty());
    EXPECT_TRUE(cleared.Colors.empty());
}

TEST(VisualizationRecipes, RecipeIdentityIsClosedAndComparable)
{
    const R::VisualizationRecipe scalar{
        .Data = R::ScalarVisualizationRecipe{
            .Source = {
                .Domain = R::GeometryElementDomain::MeshVertex,
                .Name = "curvature",
                .ValueKind = Geometry::PropertyValueKind::Float,
            },
            .OutputName = "curvature.view",
        }};
    R::VisualizationRecipe same = scalar;
    R::VisualizationRecipe different = scalar;
    std::get<R::ScalarVisualizationRecipe>(different.Data).OutputName =
        "curvature.other";

    EXPECT_EQ(R::GetVisualizationRecipeKind({}),
              R::VisualizationRecipeKind::Empty);
    EXPECT_EQ(R::GetVisualizationRecipeKind(scalar),
              R::VisualizationRecipeKind::Scalar);
    EXPECT_EQ(R::ToString(R::GetVisualizationRecipeKind(scalar)), "Scalar");
    EXPECT_TRUE(R::SameVisualizationRecipe(scalar, same));
    EXPECT_FALSE(R::SameVisualizationRecipe(scalar, different));
    EXPECT_FALSE(R::SameVisualizationRecipe(scalar, {}));
}
