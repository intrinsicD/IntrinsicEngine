#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;
import Geometry.Properties;

namespace Runtime = Extrinsic::Runtime;

TEST(RuntimeTextureBakeModule, RepresentationDefaultsPreserveRawScalarData)
{
    const std::vector<Runtime::EditorTextureBakeTarget> targets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::ScalarField,
        },
    };
    const Runtime::PropertyTextureBakeRepresentation representation =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::Float,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            targets);

    EXPECT_EQ(
        representation.Storage,
        Runtime::PropertyTextureBakeStorage::RawFloat);
    EXPECT_EQ(
        representation.Encoding,
        Runtime::PropertyTextureBakeEncoding::LinearScalar);
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        targets[0],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        targets[1],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
}

TEST(RuntimeTextureBakeModule, NormalAndLabelDefaultsChooseEncodedStorage)
{
    const std::vector<Runtime::EditorTextureBakeTarget> normalTargets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Normal,
        },
    };
    const auto normal =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::Vec3,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            normalTargets);
    EXPECT_EQ(
        normal.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        normal.Encoding,
        Runtime::PropertyTextureBakeEncoding::Normal);
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        normalTargets.front(),
        Geometry::PropertyValueKind::Vec3,
        normal.Storage,
        normal.Encoding));

    const std::vector<Runtime::EditorTextureBakeTarget> albedoTargets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
    };
    const auto label =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::UInt32,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            albedoTargets);
    EXPECT_EQ(
        label.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        label.Encoding,
        Runtime::PropertyTextureBakeEncoding::LabelPalette);
}

TEST(
    RuntimeTextureBakeModule,
    RepresentationMatrixRejectsEncodersThatDoNotMatchStorageAndValueType)
{
    using Runtime::IsPropertyTextureBakeRepresentationCompatible;
    using Runtime::PropertyTextureBakeEncoding;
    using Runtime::PropertyTextureBakeStorage;

    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LinearScalar));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::ScalarColormap));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec4,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::Auto,
        PropertyTextureBakeEncoding::Vector3));
}

TEST(RuntimeTextureBakeModule, ServiceFailsClosedWithoutGpuComposition)
{
    Runtime::TextureBakeService service{};
    EXPECT_FALSE(service.Available());
    const Runtime::PropertyTextureBakeResult result =
        service.Bake(Runtime::PropertyTextureBakeRequest{});
    EXPECT_EQ(
        result.Status,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
}
