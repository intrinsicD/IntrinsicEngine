#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.RenderWorld;

namespace
{
    using namespace Extrinsic::Graphics;

    template <typename T>
    [[nodiscard]] bool Contains(const std::vector<T>& values, const T value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    [[nodiscard]] bool ContainsString(const std::vector<std::string>& values,
                                      const std::string_view value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    [[nodiscard]] const BindingIntent* FindBinding(const BindingSet& bindings,
                                                   const std::string_view semanticName)
    {
        const auto it = std::find_if(bindings.Intents.begin(),
                                     bindings.Intents.end(),
                                     [semanticName](const BindingIntent& intent) {
                                         return intent.SemanticName == semanticName;
                                     });
        return it == bindings.Intents.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool HasOutput(const ViewOutputRecipeDescriptor& recipe,
                                 const RenderOutputKind kind)
    {
        return std::any_of(recipe.Outputs.begin(),
                           recipe.Outputs.end(),
                           [kind](const ViewOutputDescriptor& output) {
                               return output.Kind == kind;
                           });
    }
}

TEST(CurrentRendererContractAdapter, DescriptorDeclaresCurrentRendererContractSurface)
{
    const RendererDescriptor descriptor = MakeCurrentRendererDescriptor();

    EXPECT_EQ(descriptor.Id, kCurrentRendererContractId);
    EXPECT_EQ(descriptor.Purpose, RendererPurpose::Realtime);
    EXPECT_TRUE(Contains(descriptor.SupportedSnapshotScopes, SnapshotScope::FullScene));
    EXPECT_TRUE(Contains(descriptor.SupportedSnapshotScopes, SnapshotScope::Selection));
    EXPECT_TRUE(Contains(descriptor.SupportedCapabilities, RendererCapability::Surface));
    EXPECT_TRUE(Contains(descriptor.SupportedCapabilities, RendererCapability::Readback));
    EXPECT_TRUE(Contains(descriptor.SupportedCapabilities, RendererCapability::VisibilityRecipe));
    EXPECT_TRUE(Contains(descriptor.SupportedCapabilities, RendererCapability::LightingRecipe));
    EXPECT_TRUE(ContainsString(descriptor.DeclaredRecipeSlots, "visibility"));
    EXPECT_TRUE(ContainsString(descriptor.DeclaredRecipeSlots, "lighting"));
    EXPECT_TRUE(ContainsString(descriptor.DeclaredRecipeSlots, "debug-view"));
    EXPECT_TRUE(IsCompatible(ValidateRendererDescriptor(descriptor)));
}

TEST(CurrentRendererContractAdapter, BindingSetMapsCurrentMaterialGeometryAndVisualizationInputs)
{
    const RendererDescriptor descriptor = MakeCurrentRendererDescriptor();
    const BindingSet bindings = MakeCurrentRendererBindingSet();

    const BindingIntent* renderables = FindBinding(bindings, "renderables");
    ASSERT_NE(renderables, nullptr);
    EXPECT_EQ(renderables->Requirement, BindingRequirement::Required);
    EXPECT_EQ(renderables->Category, RenderDataCategory::Geometry);
    ASSERT_TRUE(renderables->RequiredCapability.has_value());
    EXPECT_EQ(*renderables->RequiredCapability, RendererCapability::Surface);

    const BindingIntent* materials = FindBinding(bindings, "material-table");
    ASSERT_NE(materials, nullptr);
    EXPECT_EQ(materials->Requirement, BindingRequirement::Required);
    EXPECT_EQ(materials->Category, RenderDataCategory::Materials);

    const BindingIntent* normals = FindBinding(bindings, "surface-normals");
    ASSERT_NE(normals, nullptr);
    EXPECT_EQ(normals->Requirement, BindingRequirement::Optional);
    EXPECT_EQ(normals->ColorSpace, BindingColorSpace::NormalizedData);

    const BindingIntent* colors = FindBinding(bindings, "surface-colors");
    ASSERT_NE(colors, nullptr);
    EXPECT_EQ(colors->Requirement, BindingRequirement::Optional);
    EXPECT_EQ(colors->ColorSpace, BindingColorSpace::Linear);

    const BindingIntent* textures = FindBinding(bindings, "material-textures");
    ASSERT_NE(textures, nullptr);
    EXPECT_EQ(textures->ValueType, BindingValueType::Texture2D);
    EXPECT_EQ(textures->FallbackPolicy, RenderingFallbackPolicy::SubstituteDefaults);

    const BindingIntent* visualization = FindBinding(bindings, "visualization-attributes");
    ASSERT_NE(visualization, nullptr);
    ASSERT_TRUE(visualization->RequiredCapability.has_value());
    EXPECT_EQ(*visualization->RequiredCapability, RendererCapability::DebugView);

    const BindingIntent* camera = FindBinding(bindings, "camera-view");
    ASSERT_NE(camera, nullptr);
    EXPECT_EQ(camera->Requirement, BindingRequirement::Required);
    EXPECT_EQ(camera->Category, RenderDataCategory::Cameras);

    EXPECT_TRUE(IsCompatible(ValidateBindingSet(descriptor, bindings)));
}

TEST(CurrentRendererContractAdapter, FrameInputBuildsCompatibleSnapshotRecipeAndDiagnostics)
{
    RenderFrameInput input{};
    input.Viewport = Extrinsic::Core::Extent2D{.Width = 1280, .Height = 720};
    input.HasPendingPick = true;
    input.Pick = PickPixelRequest{.X = 17u, .Y = 31u, .Pending = true, .Sequence = 42u};
    input.Camera.Valid = true;
    input.DebugOverlayEnabled = true;

    const CurrentRendererContract contract = MakeCurrentRendererContract(
        input,
        CurrentRendererSnapshotOptions{.SnapshotId = "frame-input-snapshot", .FrameIndex = 12u},
        CurrentRendererOutputOptions{
            .ReadbackRequested = true,
            .IncludeMetricsOutput = true,
        });

    EXPECT_TRUE(IsCompatible(contract.Diagnostics));
    EXPECT_EQ(CountBySeverity(contract.Diagnostics, RenderingContractDiagnosticSeverity::Error), 0u);
    EXPECT_EQ(contract.Snapshot.Id, "frame-input-snapshot");
    EXPECT_EQ(contract.Snapshot.ConsumerRendererId, contract.Renderer.Id);
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "frame:12"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "viewport:1280x720"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "pending-pick:true"));
    EXPECT_TRUE(contract.Snapshot.Diagnostics.empty());
    EXPECT_EQ(contract.ViewOutput.ViewportWidth, 1280u);
    EXPECT_EQ(contract.ViewOutput.ViewportHeight, 720u);
    EXPECT_EQ(contract.ViewOutput.View, ViewKind::Picking);
    EXPECT_TRUE(contract.ViewOutput.ReadbackRequested);
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::Color));
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::Depth));
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::EntityId));
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::PrimitiveId));
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::Metrics));
    EXPECT_TRUE(HasOutput(contract.ViewOutput, RenderOutputKind::ReadbackBuffer));
}

TEST(CurrentRendererContractAdapter, RenderWorldKeepsCurrentFrameDiagnosticsNonBlocking)
{
    RenderWorld world{};
    world.Viewport = Extrinsic::Core::Extent2D{.Width = 0, .Height = -4};
    world.Camera.Valid = false;
    world.DebugOverlayEnabled = true;
    world.HasPendingPick = false;
    world.PostProcess.Enabled = true;
    world.InvalidSnapshotRecordCount = 2u;

    const CurrentRendererContract contract = MakeCurrentRendererContract(
        world,
        CurrentRendererSnapshotOptions{.SnapshotId = "world-snapshot", .FrameIndex = 7u},
        CurrentRendererOutputOptions{.Mode = InteractionMode::Headless});

    EXPECT_TRUE(IsCompatible(contract.Diagnostics));
    EXPECT_EQ(CountBySeverity(contract.Diagnostics, RenderingContractDiagnosticSeverity::Error), 0u);
    EXPECT_EQ(contract.ViewOutput.ViewportWidth, 1u);
    EXPECT_EQ(contract.ViewOutput.ViewportHeight, 1u);
    EXPECT_EQ(contract.ViewOutput.Mode, InteractionMode::Headless);
    EXPECT_EQ(contract.ViewOutput.View, ViewKind::Camera);
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "viewport:1x1"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "camera-valid:false"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.SourceRevisions, "invalid-records:2"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.Diagnostics, "camera-defaults-in-use"));
    EXPECT_TRUE(ContainsString(contract.Snapshot.Diagnostics, "invalid-snapshot-records-dropped:2"));
}

TEST(CurrentRendererContractAdapter, RecipeDescriptorUsesOnlyDeclaredCurrentRendererSlots)
{
    const RendererDescriptor descriptor = MakeCurrentRendererDescriptor();
    const RenderRecipeDescriptor recipe = MakeCurrentRendererRecipeDescriptor();

    EXPECT_EQ(recipe.RecipeId, kCurrentRendererDefaultRecipeId);
    EXPECT_EQ(recipe.FixedCoreName, "Extrinsic.Graphics.FrameRecipe.Default");
    EXPECT_TRUE(std::any_of(recipe.Slots.begin(),
                            recipe.Slots.end(),
                            [](const RecipeExtensionSlotDescriptor& slot) {
                                return slot.StableName == "default-frame-core" &&
                                       slot.Kind == RecipeSlotKind::FixedCore;
                            }));
    EXPECT_TRUE(std::any_of(recipe.Slots.begin(),
                            recipe.Slots.end(),
                            [](const RecipeExtensionSlotDescriptor& slot) {
                                return slot.StableName == "visibility" &&
                                       Contains(slot.RequiredCapabilities,
                                                RendererCapability::VisibilityRecipe);
                            }));
    EXPECT_TRUE(std::any_of(recipe.Slots.begin(),
                            recipe.Slots.end(),
                            [](const RecipeExtensionSlotDescriptor& slot) {
                                return slot.StableName == "lighting" &&
                                       Contains(slot.RequiredCapabilities,
                                                RendererCapability::LightingRecipe);
                            }));
    EXPECT_TRUE(IsCompatible(ValidateRenderRecipeDescriptor(descriptor, recipe)));
}
