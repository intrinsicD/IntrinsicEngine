#include <gtest/gtest.h>

#include <string>
#include <vector>

import Extrinsic.Graphics.RenderingContract;

namespace
{
    using namespace Extrinsic::Graphics;

    [[nodiscard]] RendererDescriptor MakeRenderer()
    {
        return RendererDescriptor{
            .Id = "default-renderer",
            .Purpose = RendererPurpose::Realtime,
            .SupportedSnapshotScopes = {SnapshotScope::FullScene, SnapshotScope::Selection},
            .SupportedSnapshotKinds = {SnapshotKind::FullScene, SnapshotKind::SelectedEntity},
            .UpdateModes = {RendererUpdateMode::PerFrame, RendererUpdateMode::OnDemand},
            .RequiredDataCategories = {
                RenderDataCategory::Geometry,
                RenderDataCategory::Materials,
                RenderDataCategory::Cameras,
            },
            .OptionalDataCategories = {RenderDataCategory::Lights},
            .SupportedCapabilities = {
                RendererCapability::Surface,
                RendererCapability::Interactive,
                RendererCapability::Headless,
                RendererCapability::Readback,
                RendererCapability::DebugView,
                RendererCapability::VisibilityRecipe,
            },
            .Outputs = {
                RendererOutputDescriptor{.Name = "color", .Kind = RenderOutputKind::Color},
                RendererOutputDescriptor{.Name = "depth", .Kind = RenderOutputKind::Depth},
                RendererOutputDescriptor{.Name = "metrics", .Kind = RenderOutputKind::Metrics, .Required = false},
            },
            .DeclaredRecipeSlots = {"debug-view", "visibility"},
            .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
        };
    }

    [[nodiscard]] SnapshotEnvelope MakeSnapshot()
    {
        return SnapshotEnvelope{
            .Id = "snapshot-1",
            .Kind = SnapshotKind::FullScene,
            .Scope = SnapshotScope::FullScene,
            .ProducerRendererId = "runtime-adapter",
            .ConsumerRendererId = "default-renderer",
            .SourceRevisions = {"scene:42", "materials:7"},
            .DependencyHashes = {"hash:scene", "hash:materials"},
            .ValidationState = SnapshotValidationState::Valid,
            .Generated = true,
            .Lifetime = SnapshotLifetimePolicy::FrameTransient,
            .ReplayMetadata = "replay-safe",
            .ExportMetadata = "contract-only",
        };
    }

    [[nodiscard]] BindingSet MakeBindings()
    {
        return BindingSet{
            .Intents = {
                BindingIntent{
                    .Role = BindingSemanticRole::Geometry,
                    .SemanticName = "surface-geometry",
                    .Category = RenderDataCategory::Geometry,
                    .SourceDomain = BindingSourceDomain::MeshVertex,
                    .SourceIdentity = "entity-set",
                    .SourceRevision = "geometry:42",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "GpuGeometryRecord",
                    .Requirement = BindingRequirement::Required,
                    .ConsumerRole = "surface",
                    .ConsumerPass = "SurfacePass",
                    .RequiredCapability = RendererCapability::Surface,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Material,
                    .SemanticName = "material-table",
                    .Category = RenderDataCategory::Materials,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "material-table",
                    .SourceRevision = "materials:7",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "MaterialTable",
                    .Requirement = BindingRequirement::Required,
                    .ConsumerRole = "surface",
                    .ConsumerPass = "SurfacePass",
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Camera,
                    .SemanticName = "main-camera",
                    .Category = RenderDataCategory::Cameras,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "camera:main",
                    .SourceRevision = "camera:3",
                    .ValueType = BindingValueType::Mat4,
                    .ValueFormat = "CameraViewProjection",
                    .Requirement = BindingRequirement::Required,
                    .ConsumerRole = "view",
                    .ConsumerLens = "main",
                },
            },
        };
    }

    [[nodiscard]] RenderRecipeDescriptor MakeRecipe()
    {
        return RenderRecipeDescriptor{
            .RecipeId = "default-recipe",
            .FixedCoreName = "default-frame-core",
            .Slots = {
                RecipeExtensionSlotDescriptor{
                    .StableName = "debug-view",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "debug-view/v1",
                    .Defaults = "disabled",
                    .RequiredCapabilities = {RendererCapability::DebugView},
                    .AllowedBindingRoles = {"main-camera"},
                    .UsedBindingRoles = {"main-camera"},
                    .ValidationRules = {"declared-slot-only"},
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "visibility",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "visibility/v1",
                    .RequiredCapabilities = {RendererCapability::VisibilityRecipe},
                    .AllowedBindingRoles = {"surface-geometry"},
                    .UsedBindingRoles = {"surface-geometry"},
                },
            },
        };
    }

    [[nodiscard]] ViewOutputRecipeDescriptor MakeViewRecipe()
    {
        return ViewOutputRecipeDescriptor{
            .RecipeId = "main-view",
            .View = ViewKind::Camera,
            .ViewportWidth = 1280u,
            .ViewportHeight = 720u,
            .RenderScale = 1.0f,
            .Target = OutputTargetKind::Window,
            .CaptureRequested = true,
            .ReadbackRequested = true,
            .Mode = InteractionMode::Interactive,
            .Outputs = {
                ViewOutputDescriptor{.Name = "color", .Kind = RenderOutputKind::Color, .Format = "RGBA8"},
                ViewOutputDescriptor{.Name = "depth", .Kind = RenderOutputKind::Depth, .Format = "D32"},
            },
        };
    }
}

TEST(RenderingContract, ValidDescriptorSnapshotBindingsRecipeAndViewAreCompatible)
{
    const RendererDescriptor renderer = MakeRenderer();
    const SnapshotEnvelope snapshot = MakeSnapshot();
    const BindingSet bindings = MakeBindings();
    const RenderRecipeDescriptor recipe = MakeRecipe();
    const ViewOutputRecipeDescriptor viewRecipe = MakeViewRecipe();

    EXPECT_TRUE(IsCompatible(ValidateRendererDescriptor(renderer)));
    EXPECT_TRUE(IsCompatible(ValidateSnapshotEnvelope(renderer, snapshot)));
    EXPECT_TRUE(IsCompatible(ValidateBindingSet(renderer, bindings)));
    EXPECT_TRUE(IsCompatible(ValidateRenderRecipeDescriptor(renderer, recipe)));
    EXPECT_TRUE(IsCompatible(ValidateViewOutputRecipe(renderer, viewRecipe)));
    EXPECT_TRUE(IsCompatible(ValidateRenderingContract(renderer, snapshot, bindings, recipe, viewRecipe)));
}

TEST(RenderingContract, RendererDescriptorRequiresStableContractFields)
{
    RendererDescriptor renderer = MakeRenderer();
    renderer.Id.clear();
    renderer.Purpose = RendererPurpose::Unknown;
    renderer.SupportedSnapshotScopes.clear();
    renderer.SupportedSnapshotKinds.clear();
    renderer.UpdateModes.clear();
    renderer.Outputs.clear();

    const RenderingContractValidationResult result = ValidateRendererDescriptor(renderer);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_EQ(CountBySeverity(result, RenderingContractDiagnosticSeverity::Error), 6u);
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::EmptyRendererId));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnknownRendererPurpose));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingSupportedSnapshotScope));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingSupportedSnapshotKind));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingUpdateMode));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingRendererOutput));
}

TEST(RenderingContract, SnapshotCompatibilityRejectsWrongScopeAndStaleDegradedState)
{
    const RendererDescriptor renderer = MakeRenderer();
    SnapshotEnvelope snapshot = MakeSnapshot();
    snapshot.Id = "bad-snapshot";
    snapshot.ConsumerRendererId = "other-renderer";
    snapshot.Scope = SnapshotScope::Chunk;
    snapshot.Kind = SnapshotKind::StreamingChunk;
    snapshot.ValidationState = SnapshotValidationState::Stale;
    snapshot.Stale = true;
    snapshot.MissingData = true;
    snapshot.Degraded = true;

    const RenderingContractValidationResult result = ValidateSnapshotEnvelope(renderer, snapshot);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::SnapshotRendererMismatch));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedSnapshotScope));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedSnapshotKind));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::InvalidSnapshotState));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::StaleSnapshot));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingSnapshotData));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::DegradedSnapshot));
}

TEST(RenderingContract, BindingSetRequiresDeclaredDataAndSupportedCapabilities)
{
    const RendererDescriptor renderer = MakeRenderer();
    BindingSet bindings = MakeBindings();
    bindings.Intents.erase(bindings.Intents.begin());
    bindings.Intents.push_back(BindingIntent{
        .Role = BindingSemanticRole::Output,
        .SemanticName = "",
        .Category = RenderDataCategory::Diagnostics,
        .SourceDomain = BindingSourceDomain::Generated,
        .ValueType = BindingValueType::Buffer,
        .Requirement = BindingRequirement::Optional,
        .RequiredCapability = RendererCapability::Shadows,
    });

    const RenderingContractValidationResult result = ValidateBindingSet(renderer, bindings);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::MissingRequiredBinding));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::EmptyBindingRole));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedBindingCapability));
}

TEST(RenderingContract, RecipeRejectsUnknownSlotsUnsupportedCapabilitiesAndDisallowedBindings)
{
    const RendererDescriptor renderer = MakeRenderer();
    RenderRecipeDescriptor recipe = MakeRecipe();
    recipe.Slots.push_back(RecipeExtensionSlotDescriptor{
        .StableName = "path-tracing",
        .Kind = RecipeSlotKind::Extension,
        .SchemaId = "path-tracing/v1",
        .RequiredCapabilities = {RendererCapability::Shadows},
        .AllowedBindingRoles = {"surface-geometry"},
        .UsedBindingRoles = {"main-camera"},
    });

    const RenderingContractValidationResult result = ValidateRenderRecipeDescriptor(renderer, recipe);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnknownRecipeSlot));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedRecipeCapability));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::DisallowedRecipeBinding));
}

TEST(RenderingContract, ViewOutputRecipeRejectsInvalidViewportScaleReadbackAndOutputs)
{
    RendererDescriptor renderer = MakeRenderer();
    renderer.SupportedCapabilities = {RendererCapability::Surface, RendererCapability::Interactive};
    ViewOutputRecipeDescriptor viewRecipe = MakeViewRecipe();
    viewRecipe.ViewportWidth = 0u;
    viewRecipe.RenderScale = 0.0f;
    viewRecipe.Outputs.push_back(ViewOutputDescriptor{
        .Name = "entity-id",
        .Kind = RenderOutputKind::EntityId,
        .Format = "R32_UINT",
    });

    const RenderingContractValidationResult result = ValidateViewOutputRecipe(renderer, viewRecipe);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::InvalidViewport));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::InvalidRenderScale));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedReadbackRequest));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UnsupportedOutput));
}

TEST(RenderingContract, RenderArtifactMetadataValidatesDeclaredOutputAndClassifiesLifecycle)
{
    const RendererDescriptor renderer = MakeRenderer();
    const ViewOutputRecipeDescriptor viewRecipe = MakeViewRecipe();
    RenderArtifactMetadata artifact{
        .ArtifactId = "artifact-color",
        .RendererId = "default-renderer",
        .SnapshotId = "snapshot-1",
        .ViewOutputRecipeId = "main-view",
        .SourceRevisions = {"scene:42"},
        .Status = RenderArtifactStatus::Available,
        .Lifetime = RenderArtifactLifetime::Cached,
        .Purpose = "color",
    };

    EXPECT_TRUE(IsCompatible(ValidateRenderArtifactMetadata(renderer, viewRecipe, artifact)));
    EXPECT_EQ(ClassifyRenderArtifactLifecycle(artifact), RenderArtifactLifecycleClass::CachedAvailable);

    artifact.Purpose = "undeclared-output";
    artifact.RendererId = "other-renderer";
    artifact.Status = RenderArtifactStatus::Published;
    artifact.Lifetime = RenderArtifactLifetime::Published;

    const RenderingContractValidationResult result =
        ValidateRenderArtifactMetadata(renderer, viewRecipe, artifact);

    EXPECT_FALSE(IsCompatible(result));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::ArtifactRendererMismatch));
    EXPECT_TRUE(HasDiagnostic(result, RenderingContractDiagnosticCode::UndeclaredArtifactOutput));
    EXPECT_EQ(ClassifyRenderArtifactLifecycle(artifact), RenderArtifactLifecycleClass::Published);
}
