#include <array>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.WorldHandle;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Runtime::AssetImportExecutionIdentity Identity(
        const std::uint32_t generation = 1u)
    {
        return Runtime::AssetImportExecutionIdentity{
            .Request = Runtime::RuntimeAssetIngestHandle{7u, generation},
            .World = Runtime::WorldHandle{3u, 2u},
            .BindingGeneration = 9u,
            .CancellationGeneration = generation,
        };
    }
}

TEST(RuntimeAssetImportRecipe, RequiresAnExplicitVisiblePayloadRoute)
{
    Runtime::AssetImportRecipe recipe{
        .Path = "mesh.obj",
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    };
    EXPECT_TRUE(Runtime::ValidateAssetImportRecipe(recipe).has_value());

    recipe.PayloadKind = Assets::AssetPayloadKind::Unknown;
    EXPECT_EQ(
        Runtime::ValidateAssetImportRecipe(recipe).error(),
        Core::ErrorCode::InvalidArgument);

    recipe.PayloadKind = Assets::AssetPayloadKind::Mesh;
    recipe.Authoring.AuthorSelectableIdentity = false;
    EXPECT_EQ(
        Runtime::ValidateAssetImportRecipe(recipe).error(),
        Core::ErrorCode::InvalidArgument);
}

TEST(RuntimeAssetImportRecipe, ReimportIdentityMustMatchTheRecipeSource)
{
    Runtime::AssetImportRecipe recipe{
        .Path = "mesh.obj",
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
        .Source = Runtime::RuntimeAssetIngestSource::Reimport,
    };
    EXPECT_EQ(
        Runtime::ValidateAssetImportRecipe(recipe).error(),
        Core::ErrorCode::InvalidArgument);

    recipe.ExistingAsset = Assets::AssetId{4u, 2u};
    EXPECT_TRUE(Runtime::ValidateAssetImportRecipe(recipe).has_value());

    recipe.Source = Runtime::RuntimeAssetIngestSource::ManualImport;
    EXPECT_EQ(
        Runtime::ValidateAssetImportRecipe(recipe).error(),
        Core::ErrorCode::InvalidArgument);
}

TEST(RuntimeAssetImportRecipe, StageTraceAcceptsOnlyTheNamedOrder)
{
    constexpr std::array orderedStages{
        Runtime::AssetImportStage::Route,
        Runtime::AssetImportStage::Decode,
        Runtime::AssetImportStage::CpuMaterialize,
        Runtime::AssetImportStage::EcsAuthor,
        Runtime::AssetImportStage::Postprocess,
        Runtime::AssetImportStage::GpuResidency,
        Runtime::AssetImportStage::Complete,
    };
    Runtime::AssetImportStageTrace trace{.Identity = Identity()};

    for (const Runtime::AssetImportStage stage : orderedStages)
    {
        ASSERT_TRUE(Runtime::AppendAssetImportStageResult(
                        trace,
                        Runtime::AssetImportStageResult{
                            .Identity = trace.Identity,
                            .Stage = stage,
                        })
                        .has_value());
    }

    EXPECT_TRUE(trace.Terminal);
    EXPECT_EQ(trace.Results.size(), orderedStages.size());
    EXPECT_EQ(
        Runtime::AppendAssetImportStageResult(
            trace,
            Runtime::AssetImportStageResult{
                .Identity = trace.Identity,
                .Stage = Runtime::AssetImportStage::Complete,
            })
            .error(),
        Core::ErrorCode::InvalidState);
}

TEST(RuntimeAssetImportRecipe, StageTraceFailsClosedForMalformedOrStaleResults)
{
    Runtime::AssetImportStageTrace trace{.Identity = Identity()};

    EXPECT_EQ(
        Runtime::AppendAssetImportStageResult(
            trace,
            Runtime::AssetImportStageResult{
                .Identity = trace.Identity,
                .Stage = Runtime::AssetImportStage::Decode,
            })
            .error(),
        Core::ErrorCode::InvalidState);

    EXPECT_EQ(
        Runtime::AppendAssetImportStageResult(
            trace,
            Runtime::AssetImportStageResult{
                .Identity = Identity(2u),
                .Stage = Runtime::AssetImportStage::Route,
            })
            .error(),
        Core::ErrorCode::InvalidState);

    ASSERT_TRUE(Runtime::AppendAssetImportStageResult(
                    trace,
                    Runtime::AssetImportStageResult{
                        .Identity = trace.Identity,
                        .Stage = Runtime::AssetImportStage::Route,
                    })
                    .has_value());
    ASSERT_TRUE(Runtime::AppendAssetImportStageResult(
                    trace,
                    Runtime::AssetImportStageResult{
                        .Identity = trace.Identity,
                        .Stage = Runtime::AssetImportStage::Decode,
                        .Error = Core::ErrorCode::AssetDecodeFailed,
                        .Diagnostic =
                            Runtime::RuntimeAssetIngestDiagnostic::DecodeFailed,
                    })
                    .has_value());
    EXPECT_TRUE(trace.Terminal);
    EXPECT_EQ(trace.Results.size(), 2u);
}
