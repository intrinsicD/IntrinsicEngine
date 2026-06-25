#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.SharedRenderRecipeExecution;
import Extrinsic.RHI.Types;

namespace Graphics = Extrinsic::Graphics;
namespace RHI = Extrinsic::RHI;

namespace
{
    [[nodiscard]] Graphics::SnapshotEnvelope MakeSnapshot(const bool stale = false)
    {
        return Graphics::SnapshotEnvelope{
            .Id = "shared-recipe-snapshot",
            .Kind = Graphics::SnapshotKind::FullScene,
            .Scope = Graphics::SnapshotScope::FullScene,
            .ProducerRendererId = "runtime-extraction",
            .ConsumerRendererId = "shared-recipe-test",
            .SourceRevisions = {"scene:1"},
            .ValidationState = stale ? Graphics::SnapshotValidationState::Stale
                                      : Graphics::SnapshotValidationState::Valid,
            .Stale = stale,
        };
    }

    [[nodiscard]] RHI::GpuBounds MakeBounds(const glm::vec3 center, const float radius)
    {
        RHI::GpuBounds bounds{};
        bounds.WorldSphere = glm::vec4{center, radius};
        bounds.WorldAabbMin = glm::vec4{center - glm::vec3{radius}, 1.0f};
        bounds.WorldAabbMax = glm::vec4{center + glm::vec3{radius}, 1.0f};
        return bounds;
    }

    [[nodiscard]] Graphics::RenderableSnapshot MakeRenderable(
        const std::uint32_t stableId,
        const std::uint32_t flags,
        const glm::vec3 center,
        const std::uint32_t materialSlot = 1u)
    {
        return Graphics::RenderableSnapshot{
            .StableId = stableId,
            .Instance = Graphics::GpuInstanceHandle{stableId, 1u},
            .Geometry = Graphics::GpuGeometryHandle{stableId + 100u, 1u},
            .Bounds = MakeBounds(center, 1.0f),
            .RenderFlags = flags,
            .MaterialSlot = materialSlot,
            .HasMaterialSlot = true,
        };
    }

    [[nodiscard]] Graphics::RenderWorld MakeWorld(
        std::vector<Graphics::RenderableSnapshot>& renderables,
        std::vector<Graphics::LightSnapshot>& lights)
    {
        Graphics::RenderWorld world{};
        world.Renderables = renderables;
        world.Lights = lights;
        world.Camera.Valid = true;
        world.Camera.Position = glm::vec3{0.0f, 0.0f, 0.0f};
        world.Camera.Forward = glm::vec3{0.0f, 0.0f, -1.0f};
        return world;
    }

    [[nodiscard]] const Graphics::VisibilityRecipeVisibleItem* FindVisible(
        const Graphics::VisibilityRecipeExecutionResult& result,
        const std::uint32_t stableId,
        const Graphics::VisibilityRecipeDomain domain)
    {
        const auto it = std::find_if(
            result.VisibleItems.begin(),
            result.VisibleItems.end(),
            [stableId, domain](const Graphics::VisibilityRecipeVisibleItem& item) {
                return item.StableId == stableId && item.Domain == domain;
            });
        return it == result.VisibleItems.end() ? nullptr : &*it;
    }

    void RemoveCapability(Graphics::RendererDescriptor& renderer,
                          const Graphics::RendererCapability capability)
    {
        renderer.SupportedCapabilities.erase(
            std::remove(renderer.SupportedCapabilities.begin(),
                        renderer.SupportedCapabilities.end(),
                        capability),
            renderer.SupportedCapabilities.end());
    }
}

TEST(VisibilityRecipe, FiltersGroupsSelectsLodAndRequestsAccelerationStructures)
{
    std::vector<Graphics::RenderableSnapshot> renderables{
        MakeRenderable(10u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface |
                           RHI::GpuRender_CastShadow | RHI::GpuRender_Selectable,
                       glm::vec3{0.0f, 0.0f, -10.0f},
                       7u),
        MakeRenderable(11u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{3.0f, 0.0f, -45.0f},
                       7u),
        MakeRenderable(12u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Point,
                       glm::vec3{0.0f, 0.0f, -160.0f},
                       2u),
        MakeRenderable(13u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Line,
                       glm::vec3{36.0f, 0.0f, -20.0f},
                       3u),
    };
    std::vector<Graphics::LightSnapshot> lights{};
    const Graphics::RenderWorld world = MakeWorld(renderables, lights);

    const Graphics::VisibilityRecipeExecutionResult result =
        Graphics::ExecuteVisibilityRecipe(
            world,
            MakeSnapshot(),
            Graphics::VisibilityRecipeOptions{
                .RequestAccelerationStructures = true,
                .NearLodDistance = 25.0f,
                .FarLodDistance = 100.0f,
                .SpatialPartitionCellSize = 32.0f,
            });

    EXPECT_EQ(result.SurfaceItemCount, 2u);
    EXPECT_EQ(result.LineItemCount, 1u);
    EXPECT_EQ(result.PointItemCount, 1u);
    EXPECT_EQ(result.ShadowItemCount, 1u);
    EXPECT_EQ(result.SelectionItemCount, 1u);
    EXPECT_EQ(result.AccelerationStructures.size(), 2u);

    const auto* nearSurface =
        FindVisible(result, 10u, Graphics::VisibilityRecipeDomain::Surface);
    const auto* midSurface =
        FindVisible(result, 11u, Graphics::VisibilityRecipeDomain::Surface);
    const auto* farPoint =
        FindVisible(result, 12u, Graphics::VisibilityRecipeDomain::Point);
    ASSERT_NE(nearSurface, nullptr);
    ASSERT_NE(midSurface, nullptr);
    ASSERT_NE(farPoint, nullptr);
    EXPECT_EQ(nearSurface->GroupKey, midSurface->GroupKey);
    EXPECT_EQ(nearSurface->BatchGroup, midSurface->BatchGroup);
    EXPECT_EQ(nearSurface->LodLevel, 0u);
    EXPECT_EQ(midSurface->LodLevel, 1u);
    EXPECT_EQ(farPoint->LodLevel, 2u);
    EXPECT_NE(nearSurface->SpatialPartition,
              FindVisible(result, 13u, Graphics::VisibilityRecipeDomain::Line)
                  ->SpatialPartition);
}

TEST(GroupingRecipe, RejectsHiddenMalformedUnsupportedAndEmptyInputs)
{
    std::vector<Graphics::RenderableSnapshot> renderables{
        MakeRenderable(20u, RHI::GpuRender_Surface, glm::vec3{0.0f}),
        MakeRenderable(21u, RHI::GpuRender_Visible, glm::vec3{0.0f}),
        MakeRenderable(22u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{0.0f}),
        MakeRenderable(23u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{0.0f}),
    };
    renderables[2].Geometry = Graphics::GpuGeometryHandle{};
    renderables[3].Bounds.WorldSphere = glm::vec4{0.0f, 0.0f, 0.0f, -1.0f};
    std::vector<Graphics::LightSnapshot> lights{};
    const Graphics::RenderWorld world = MakeWorld(renderables, lights);

    const Graphics::VisibilityRecipeExecutionResult result =
        Graphics::ExecuteVisibilityRecipe(world, MakeSnapshot(true));

    EXPECT_TRUE(result.StaleInput);
    EXPECT_TRUE(result.Degraded);
    EXPECT_EQ(result.VisibleItems.size(), 0u);
    EXPECT_EQ(result.RejectedItems.size(), 4u);
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::StaleInput));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::NotVisible));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::UnsupportedRenderDomain));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::MissingGeometry));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::NonFiniteBounds));

    std::vector<Graphics::RenderableSnapshot> emptyRenderables{};
    const Graphics::RenderWorld emptyWorld = MakeWorld(emptyRenderables, lights);
    const Graphics::VisibilityRecipeExecutionResult emptyResult =
        Graphics::ExecuteVisibilityRecipe(emptyWorld, MakeSnapshot());
    EXPECT_TRUE(emptyResult.VisibleItems.empty());
    EXPECT_TRUE(Graphics::HasDiagnostic(
        emptyResult.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::EmptyVisibilityInput));
}

TEST(LightingRecipe, ResolvesLightsEnvironmentTagsIntentsAndEmissiveGeometry)
{
    std::vector<Graphics::RenderableSnapshot> renderables{
        MakeRenderable(30u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{0.0f, 0.0f, -5.0f}),
    };
    std::vector<Graphics::LightSnapshot> lights{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Directional,
            .Direction = glm::vec3{0.0f, -1.0f, 0.0f},
            .Intensity = 2.0f,
            .Color = glm::vec3{1.0f, 0.95f, 0.9f},
        },
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Point,
            .Position = glm::vec3{1.0f, 2.0f, 3.0f},
            .Range = 25.0f,
            .Intensity = 4.0f,
            .Color = glm::vec3{0.4f, 0.7f, 1.0f},
        },
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Spot,
            .Position = glm::vec3{0.0f, 3.0f, 0.0f},
            .Range = 30.0f,
            .Direction = glm::vec3{0.0f, -1.0f, 0.0f},
            .Intensity = 5.0f,
            .InnerConeCos = 0.9f,
            .OuterConeCos = 0.6f,
        },
    };
    const Graphics::RenderWorld world = MakeWorld(renderables, lights);

    const Graphics::LightingRecipeExecutionResult result =
        Graphics::ExecuteLightingRecipe(
            world,
            MakeSnapshot(),
            Graphics::LightingRecipeOptions{
                .HasEnvironmentMap = true,
                .EnvironmentMapId = "studio-hdr",
                .ProbeIds = {"probe-main"},
                .VolumeIds = {"volume-main"},
                .Tags = {"editor-preview", "high-quality"},
                .EmissiveGeometryStableIds = {30u},
                .QualityPreset = "high",
                .RequestProbeIntents = true,
                .RequestGlobalIlluminationIntent = true,
                .DebugMode = true,
            });

    EXPECT_EQ(result.Lights.size(), 3u);
    EXPECT_EQ(result.DirectionalLightCount, 1u);
    EXPECT_EQ(result.PointLightCount, 1u);
    EXPECT_EQ(result.SpotLightCount, 1u);
    EXPECT_FALSE(result.Degraded);
    EXPECT_FALSE(result.Environment.UsedFallbackEnvironment);
    EXPECT_EQ(result.Environment.EnvironmentMapId, "studio-hdr");
    EXPECT_EQ(result.Environment.QualityPreset, "high");
    EXPECT_EQ(result.EmissiveGeometryStableIds, std::vector<std::uint32_t>({30u}));
    EXPECT_EQ(result.Intents.size(), 3u);
    EXPECT_TRUE(std::find(result.Products.begin(),
                          result.Products.end(),
                          Graphics::SharedRecipeProductKind::DebugMode) !=
                result.Products.end());
}

TEST(EnvironmentRecipe, EmptyInvalidAndUnsupportedLightingFallsBackDeterministically)
{
    std::vector<Graphics::RenderableSnapshot> renderables{
        MakeRenderable(40u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{0.0f}),
    };
    std::vector<Graphics::LightSnapshot> lights{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Point,
            .Range = -1.0f,
            .Intensity = 1.0f,
        },
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Spot,
            .Range = 10.0f,
            .Intensity = 1.0f,
            .InnerConeCos = 0.25f,
            .OuterConeCos = 0.75f,
        },
    };
    const Graphics::RenderWorld world = MakeWorld(renderables, lights);

    const Graphics::LightingRecipeExecutionResult result =
        Graphics::ExecuteLightingRecipe(
            world,
            MakeSnapshot(true),
            Graphics::LightingRecipeOptions{
                .EmissiveGeometryStableIds = {99u},
            });

    EXPECT_TRUE(result.StaleInput);
    EXPECT_TRUE(result.Degraded);
    EXPECT_TRUE(result.UsedFallbackDirectional);
    ASSERT_EQ(result.Lights.size(), 1u);
    EXPECT_EQ(result.Lights.front().Type,
              Graphics::LightingRecipeResolvedLightType::FallbackDirectional);
    EXPECT_TRUE(result.Environment.UsedFallbackEnvironment);
    EXPECT_EQ(result.RejectedLightCount, 2u);
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::UnsupportedLight));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::EmptyLightingInput));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::FallbackUsed));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::MissingEnvironment));
    EXPECT_TRUE(Graphics::HasDiagnostic(
        result.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::InvalidRenderable));
}

TEST(VisibilityRecipe, RendererProductCompatibilityUsesDeclaredCapabilitiesAndProducedProducts)
{
    std::vector<Graphics::RenderableSnapshot> renderables{
        MakeRenderable(50u,
                       RHI::GpuRender_Visible | RHI::GpuRender_Surface,
                       glm::vec3{0.0f, 0.0f, -5.0f}),
    };
    std::vector<Graphics::LightSnapshot> lights{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Directional,
            .Intensity = 1.0f,
        },
    };
    const Graphics::RenderWorld world = MakeWorld(renderables, lights);
    const Graphics::VisibilityRecipeExecutionResult visibility =
        Graphics::ExecuteVisibilityRecipe(world, MakeSnapshot());
    const Graphics::LightingRecipeExecutionResult lighting =
        Graphics::ExecuteLightingRecipe(
            world,
            MakeSnapshot(),
            Graphics::LightingRecipeOptions{.HasEnvironmentMap = true});
    std::vector<Graphics::SharedRecipeProductKind> products = visibility.Products;
    products.insert(products.end(), lighting.Products.begin(), lighting.Products.end());

    const Graphics::SharedRecipeCompatibilityResult compatible =
        Graphics::CheckSharedRecipeCompatibility(
            Graphics::SharedRecipeRendererProductDeclaration{
                .Renderer = Graphics::MakeCurrentRendererDescriptor(),
                .ConsumedProducts = {
                    Graphics::SharedRecipeProductKind::VisibleItemSet,
                    Graphics::SharedRecipeProductKind::GroupingKeys,
                    Graphics::SharedRecipeProductKind::LightSet,
                    Graphics::SharedRecipeProductKind::EnvironmentMap,
                },
            },
            products);
    EXPECT_TRUE(compatible.Compatible());

    Graphics::RendererDescriptor noVisibility = Graphics::MakeCurrentRendererDescriptor();
    RemoveCapability(noVisibility, Graphics::RendererCapability::VisibilityRecipe);
    const Graphics::SharedRecipeCompatibilityResult unsupportedCapability =
        Graphics::CheckSharedRecipeCompatibility(
            Graphics::SharedRecipeRendererProductDeclaration{
                .Renderer = noVisibility,
                .ConsumedProducts = {
                    Graphics::SharedRecipeProductKind::VisibleItemSet,
                },
            },
            products);
    EXPECT_FALSE(unsupportedCapability.Compatible());
    EXPECT_TRUE(Graphics::HasDiagnostic(
        unsupportedCapability.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::MissingRendererCapability));

    const Graphics::SharedRecipeCompatibilityResult productNotProduced =
        Graphics::CheckSharedRecipeCompatibility(
            Graphics::SharedRecipeRendererProductDeclaration{
                .Renderer = Graphics::MakeCurrentRendererDescriptor(),
                .ConsumedProducts = {
                    Graphics::SharedRecipeProductKind::VisibleItemSet,
                    Graphics::SharedRecipeProductKind::DebugMode,
                },
            },
            visibility.Products);
    EXPECT_FALSE(productNotProduced.Compatible());
    EXPECT_TRUE(Graphics::HasDiagnostic(
        productNotProduced.Diagnostics,
        Graphics::SharedRecipeDiagnosticCode::ProductNotProduced));
}
