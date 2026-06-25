module;

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Graphics.CurrentRendererContractAdapter;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] std::uint32_t ClampExtentComponent(const int value) noexcept
        {
            return value > 0 ? static_cast<std::uint32_t>(value) : 1u;
        }

        [[nodiscard]] std::string BoolRevision(const std::string_view name, const bool value)
        {
            std::string revision{name};
            revision += ':';
            revision += value ? "true" : "false";
            return revision;
        }

        [[nodiscard]] std::string CountRevision(const std::string_view name, const std::uint64_t value)
        {
            std::string revision{name};
            revision += ':';
            revision += std::to_string(value);
            return revision;
        }

        [[nodiscard]] std::string ViewportRevision(const std::uint32_t width,
                                                   const std::uint32_t height)
        {
            return "viewport:" + std::to_string(width) + "x" + std::to_string(height);
        }

        void AppendOutput(std::vector<ViewOutputDescriptor>& outputs,
                          std::string name,
                          const RenderOutputKind kind,
                          std::string format,
                          const bool required)
        {
            outputs.push_back(ViewOutputDescriptor{
                .Name = std::move(name),
                .Kind = kind,
                .Format = std::move(format),
                .Required = required,
            });
        }

        [[nodiscard]] SnapshotEnvelope MakeBaseSnapshotEnvelope(
            const CurrentRendererSnapshotOptions& options)
        {
            return SnapshotEnvelope{
                .Id = options.SnapshotId.empty()
                    ? std::string{kCurrentRendererDefaultSnapshotId}
                    : options.SnapshotId,
                .Kind = options.Kind,
                .Scope = options.Scope,
                .ProducerRendererId = std::string{kCurrentRendererSnapshotProducerId},
                .ConsumerRendererId = std::string{kCurrentRendererContractId},
                .SourceRevisions = {CountRevision("frame", options.FrameIndex)},
                .ValidationState = SnapshotValidationState::Valid,
                .Generated = true,
                .Lifetime = SnapshotLifetimePolicy::FrameTransient,
                .ReplayMetadata = "current-renderer-contract-adapter",
                .ExportMetadata = "contract-only",
            };
        }

        [[nodiscard]] ViewOutputRecipeDescriptor MakeBaseViewRecipe(
            const std::uint32_t width,
            const std::uint32_t height,
            const bool includePickingOutputs,
            const CurrentRendererOutputOptions& options)
        {
            ViewOutputRecipeDescriptor recipe{
                .RecipeId = options.RecipeId.empty()
                    ? std::string{kCurrentRendererDefaultViewRecipeId}
                    : options.RecipeId,
                .View = includePickingOutputs ? ViewKind::Picking : ViewKind::Camera,
                .ViewportWidth = width,
                .ViewportHeight = height,
                .RenderScale = 1.0f,
                .Target = options.Target,
                .CaptureRequested = options.CaptureRequested,
                .ReadbackRequested = options.ReadbackRequested,
                .Mode = options.Mode,
            };

            AppendOutput(recipe.Outputs, "color", RenderOutputKind::Color, "Backbuffer/RGBA8_UNORM", true);
            AppendOutput(recipe.Outputs, "depth", RenderOutputKind::Depth, "D32_FLOAT", false);
            if (includePickingOutputs)
            {
                AppendOutput(recipe.Outputs, "entity-id", RenderOutputKind::EntityId, "R32_UINT", false);
                AppendOutput(recipe.Outputs, "primitive-id", RenderOutputKind::PrimitiveId, "R32_UINT", false);
            }
            if (options.IncludeMetricsOutput)
            {
                AppendOutput(recipe.Outputs, "metrics", RenderOutputKind::Metrics, "CPU counters", false);
            }
            if (options.ReadbackRequested)
            {
                AppendOutput(recipe.Outputs,
                             "readback",
                             RenderOutputKind::ReadbackBuffer,
                             "Host-visible buffer",
                             false);
            }
            return recipe;
        }
    }

    [[nodiscard]] RendererDescriptor MakeCurrentRendererDescriptor()
    {
        return RendererDescriptor{
            .Id = std::string{kCurrentRendererContractId},
            .Purpose = RendererPurpose::Realtime,
            .SupportedSnapshotScopes = {
                SnapshotScope::FullScene,
                SnapshotScope::Selection,
            },
            .SupportedSnapshotKinds = {
                SnapshotKind::FullScene,
                SnapshotKind::SelectedEntity,
                SnapshotKind::EntitySet,
            },
            .UpdateModes = {
                RendererUpdateMode::PerFrame,
                RendererUpdateMode::OnDemand,
            },
            .RequiredDataCategories = {
                RenderDataCategory::Geometry,
                RenderDataCategory::Materials,
                RenderDataCategory::Cameras,
            },
            .OptionalDataCategories = {
                RenderDataCategory::Lights,
                RenderDataCategory::Visibility,
                RenderDataCategory::Environment,
                RenderDataCategory::Picking,
                RenderDataCategory::Diagnostics,
            },
            .SupportedCapabilities = {
                RendererCapability::Surface,
                RendererCapability::Lines,
                RendererCapability::Points,
                RendererCapability::Shadows,
                RendererCapability::Picking,
                RendererCapability::Readback,
                RendererCapability::Headless,
                RendererCapability::Interactive,
                RendererCapability::DebugView,
                RendererCapability::VisibilityRecipe,
                RendererCapability::LightingRecipe,
            },
            .Outputs = {
                RendererOutputDescriptor{.Name = "color", .Kind = RenderOutputKind::Color},
                RendererOutputDescriptor{.Name = "depth", .Kind = RenderOutputKind::Depth, .Required = false},
                RendererOutputDescriptor{.Name = "entity-id", .Kind = RenderOutputKind::EntityId, .Required = false},
                RendererOutputDescriptor{
                    .Name = "primitive-id",
                    .Kind = RenderOutputKind::PrimitiveId,
                    .Required = false,
                },
                RendererOutputDescriptor{.Name = "metrics", .Kind = RenderOutputKind::Metrics, .Required = false},
                RendererOutputDescriptor{
                    .Name = "readback",
                    .Kind = RenderOutputKind::ReadbackBuffer,
                    .Required = false,
                },
            },
            .DeclaredRecipeSlots = {
                "default-frame-core",
                "visibility",
                "lighting",
                "picking",
                "postprocess",
                "debug-view",
                "transient-debug",
                "visualization-overlay",
                "readback",
            },
            .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
        };
    }

    [[nodiscard]] BindingSet MakeCurrentRendererBindingSet()
    {
        return BindingSet{
            .Intents = {
                BindingIntent{
                    .Role = BindingSemanticRole::Geometry,
                    .SemanticName = "renderables",
                    .Category = RenderDataCategory::Geometry,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "RenderWorld.Renderables",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "RenderableSnapshot/GpuGeometryHandle/GpuInstanceHandle",
                    .Requirement = BindingRequirement::Required,
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                    .ConsumerRole = "surface-geometry",
                    .ConsumerPass = "SurfacePass",
                    .RequiredCapability = RendererCapability::Surface,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Geometry,
                    .SemanticName = "surface-normals",
                    .Category = RenderDataCategory::Geometry,
                    .SourceDomain = BindingSourceDomain::MeshVertex,
                    .SourceIdentity = "GpuWorld.GeometryRecords.Normal",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Vec3,
                    .ValueFormat = "float3",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::SubstituteDefaults,
                    .ColorSpace = BindingColorSpace::NormalizedData,
                    .ConsumerRole = "normal",
                    .ConsumerPass = "SurfacePass",
                    .ConsumerLens = "surface-normal",
                    .RequiredCapability = RendererCapability::Surface,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Geometry,
                    .SemanticName = "surface-colors",
                    .Category = RenderDataCategory::Geometry,
                    .SourceDomain = BindingSourceDomain::MeshVertex,
                    .SourceIdentity = "GpuWorld.GeometryRecords.Color",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Vec4,
                    .ValueFormat = "float4",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::SubstituteDefaults,
                    .ColorSpace = BindingColorSpace::Linear,
                    .ConsumerRole = "color",
                    .ConsumerPass = "SurfacePass",
                    .ConsumerLens = "surface-color",
                    .RequiredCapability = RendererCapability::Surface,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Material,
                    .SemanticName = "material-table",
                    .Category = RenderDataCategory::Materials,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "Graphics.MaterialSystem.MaterialBuffer",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "GpuMaterialRecord",
                    .Requirement = BindingRequirement::Required,
                    .FallbackPolicy = RenderingFallbackPolicy::SubstituteDefaults,
                    .ConsumerRole = "material",
                    .ConsumerPass = "SurfacePass",
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Material,
                    .SemanticName = "material-textures",
                    .Category = RenderDataCategory::Materials,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "Graphics.MaterialSystem.TextureBindings",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Texture2D,
                    .ValueFormat = "bindless sampled textures",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::SubstituteDefaults,
                    .ColorSpace = BindingColorSpace::SRGB,
                    .ConsumerRole = "texture",
                    .ConsumerPass = "SurfacePass",
                    .ConsumerLens = "material-texture",
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Camera,
                    .SemanticName = "camera-view",
                    .Category = RenderDataCategory::Cameras,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "RenderWorld.Camera",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Mat4,
                    .ValueFormat = "CameraViewProjection",
                    .Requirement = BindingRequirement::Required,
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                    .ConsumerRole = "view",
                    .ConsumerLens = "main-camera",
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Light,
                    .SemanticName = "light-snapshots",
                    .Category = RenderDataCategory::Lights,
                    .SourceDomain = BindingSourceDomain::Scene,
                    .SourceIdentity = "RenderWorld.Lights",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "LightSnapshot",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                    .ConsumerRole = "lighting",
                    .ConsumerPass = "DeferredLightingPass",
                    .RequiredCapability = RendererCapability::LightingRecipe,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Visibility,
                    .SemanticName = "visibility-buckets",
                    .Category = RenderDataCategory::Visibility,
                    .SourceDomain = BindingSourceDomain::Generated,
                    .SourceIdentity = "Graphics.CullingSystem.DrawBuckets",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "indirect draw buckets",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                    .ConsumerRole = "visibility",
                    .ConsumerPass = "CullingPass",
                    .RequiredCapability = RendererCapability::VisibilityRecipe,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Debug,
                    .SemanticName = "visualization-attributes",
                    .Category = RenderDataCategory::Diagnostics,
                    .SourceDomain = BindingSourceDomain::Runtime,
                    .SourceIdentity = "RenderWorld.Visualization",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "VisualizationAttributeBufferPacket",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                    .ConsumerRole = "visualization",
                    .ConsumerPass = "VisualizationOverlayPass",
                    .ConsumerLens = "debug-view",
                    .RequiredCapability = RendererCapability::DebugView,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Debug,
                    .SemanticName = "debug-primitives",
                    .Category = RenderDataCategory::Diagnostics,
                    .SourceDomain = BindingSourceDomain::Runtime,
                    .SourceIdentity = "RenderWorld.DebugPrimitives",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::Buffer,
                    .ValueFormat = "DebugLine/Point/TrianglePacket",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                    .ConsumerRole = "transient-debug",
                    .ConsumerPass = "TransientDebugSurfacePass",
                    .RequiredCapability = RendererCapability::DebugView,
                },
                BindingIntent{
                    .Role = BindingSemanticRole::Visibility,
                    .SemanticName = "pick-request",
                    .Category = RenderDataCategory::Picking,
                    .SourceDomain = BindingSourceDomain::Runtime,
                    .SourceIdentity = "RenderWorld.PickRequest",
                    .SourceRevision = "per-frame",
                    .ValueType = BindingValueType::UInt,
                    .ValueFormat = "pixel + correlation sequence",
                    .Requirement = BindingRequirement::Optional,
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                    .ConsumerRole = "picking",
                    .ConsumerPass = "PickingPass",
                    .RequiredCapability = RendererCapability::Picking,
                },
            },
        };
    }

    [[nodiscard]] RenderRecipeDescriptor MakeCurrentRendererRecipeDescriptor()
    {
        return RenderRecipeDescriptor{
            .RecipeId = std::string{kCurrentRendererDefaultRecipeId},
            .FixedCoreName = "Extrinsic.Graphics.FrameRecipe.Default",
            .Slots = {
                RecipeExtensionSlotDescriptor{
                    .StableName = "default-frame-core",
                    .Kind = RecipeSlotKind::FixedCore,
                    .SchemaId = "intrinsic.graphics.default-frame-core/v1",
                    .Defaults = "current renderer default frame recipe",
                    .RequiredCapabilities = {
                        RendererCapability::Surface,
                        RendererCapability::Interactive,
                        RendererCapability::Headless,
                    },
                    .AllowedBindingRoles = {"renderables", "material-table", "camera-view"},
                    .UsedBindingRoles = {"renderables", "material-table", "camera-view"},
                    .ValidationRules = {"fixed-core-required-bindings"},
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "visibility",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.visibility/default-current/v1",
                    .Defaults = "culling buckets from current renderer",
                    .RequiredCapabilities = {RendererCapability::VisibilityRecipe},
                    .AllowedBindingRoles = {"renderables", "visibility-buckets"},
                    .UsedBindingRoles = {"renderables", "visibility-buckets"},
                    .ValidationRules = {"declared-slot-only"},
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "lighting",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.lighting/default-current/v1",
                    .Defaults = "current forward/deferred lighting path",
                    .RequiredCapabilities = {RendererCapability::LightingRecipe},
                    .AllowedBindingRoles = {"light-snapshots", "material-table"},
                    .UsedBindingRoles = {"light-snapshots", "material-table"},
                    .ValidationRules = {"declared-slot-only"},
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "picking",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.picking/default-current/v1",
                    .Defaults = "enabled only when RenderWorld.PickRequest is pending",
                    .RequiredCapabilities = {RendererCapability::Picking},
                    .AllowedBindingRoles = {"pick-request", "camera-view"},
                    .UsedBindingRoles = {"pick-request", "camera-view"},
                    .ValidationRules = {"pending-pick-gates-output"},
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "postprocess",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.postprocess/default-current/v1",
                    .Defaults = "current tone-map/present chain",
                    .AllowedBindingRoles = {"camera-view"},
                    .UsedBindingRoles = {"camera-view"},
                    .ValidationRules = {"present-source-declared"},
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "debug-view",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.debug-view/default-current/v1",
                    .Defaults = "disabled unless current debug-view settings request it",
                    .RequiredCapabilities = {RendererCapability::DebugView},
                    .AllowedBindingRoles = {
                        "camera-view",
                        "visualization-attributes",
                        "debug-primitives",
                    },
                    .UsedBindingRoles = {"camera-view", "visualization-attributes", "debug-primitives"},
                    .ValidationRules = {"debug-view-fallback-diagnostic"},
                    .FallbackPolicy = RenderingFallbackPolicy::Degrade,
                },
                RecipeExtensionSlotDescriptor{
                    .StableName = "readback",
                    .Kind = RecipeSlotKind::Extension,
                    .SchemaId = "intrinsic.graphics.readback/default-current/v1",
                    .Defaults = "off unless a renderer-owned readback buffer is armed",
                    .RequiredCapabilities = {RendererCapability::Readback},
                    .AllowedBindingRoles = {"camera-view"},
                    .UsedBindingRoles = {"camera-view"},
                    .ValidationRules = {"explicit-readback-request"},
                    .FallbackPolicy = RenderingFallbackPolicy::FailClosed,
                },
            },
        };
    }

    [[nodiscard]] SnapshotEnvelope MakeCurrentRendererSnapshotEnvelope(
        const RenderFrameInput& input,
        const CurrentRendererSnapshotOptions& options)
    {
        SnapshotEnvelope snapshot = MakeBaseSnapshotEnvelope(options);
        const std::uint32_t width = ClampExtentComponent(input.Viewport.Width);
        const std::uint32_t height = ClampExtentComponent(input.Viewport.Height);
        snapshot.SourceRevisions.push_back(ViewportRevision(width, height));
        snapshot.SourceRevisions.push_back(BoolRevision("camera-valid", input.Camera.Valid));
        snapshot.SourceRevisions.push_back(BoolRevision("pending-pick", input.HasPendingPick || input.Pick.Pending));
        snapshot.SourceRevisions.push_back(BoolRevision("debug-overlay", input.DebugOverlayEnabled));
        if (!input.Camera.Valid)
        {
            snapshot.Diagnostics.push_back("camera-defaults-in-use");
        }
        return snapshot;
    }

    [[nodiscard]] SnapshotEnvelope MakeCurrentRendererSnapshotEnvelope(
        const RenderWorld& world,
        const CurrentRendererSnapshotOptions& options)
    {
        SnapshotEnvelope snapshot = MakeBaseSnapshotEnvelope(options);
        const std::uint32_t width = ClampExtentComponent(world.Viewport.Width);
        const std::uint32_t height = ClampExtentComponent(world.Viewport.Height);
        snapshot.SourceRevisions.push_back(ViewportRevision(width, height));
        snapshot.SourceRevisions.push_back(BoolRevision("camera-valid", world.Camera.Valid));
        snapshot.SourceRevisions.push_back(CountRevision("renderables", world.Renderables.size()));
        snapshot.SourceRevisions.push_back(CountRevision("lights", world.Lights.size()));
        snapshot.SourceRevisions.push_back(BoolRevision("pending-pick", world.HasPendingPick || world.PickRequest.Pending));
        snapshot.SourceRevisions.push_back(BoolRevision("debug-overlay", world.DebugOverlayEnabled));
        snapshot.SourceRevisions.push_back(BoolRevision("visualization", world.Visualization.HasVisualizationPackets));
        snapshot.SourceRevisions.push_back(BoolRevision("postprocess", world.PostProcess.Enabled));
        snapshot.SourceRevisions.push_back(CountRevision("invalid-records", world.InvalidSnapshotRecordCount));
        if (!world.Camera.Valid)
        {
            snapshot.Diagnostics.push_back("camera-defaults-in-use");
        }
        if (world.InvalidSnapshotRecordCount > 0u)
        {
            snapshot.Diagnostics.push_back(
                "invalid-snapshot-records-dropped:" + std::to_string(world.InvalidSnapshotRecordCount));
        }
        return snapshot;
    }

    [[nodiscard]] ViewOutputRecipeDescriptor MakeCurrentRendererViewOutputRecipe(
        const RenderFrameInput& input,
        const CurrentRendererOutputOptions& options)
    {
        const std::uint32_t width = ClampExtentComponent(input.Viewport.Width);
        const std::uint32_t height = ClampExtentComponent(input.Viewport.Height);
        const bool includePickingOutputs =
            options.IncludePickingOutputs || input.HasPendingPick || input.Pick.Pending;
        return MakeBaseViewRecipe(width, height, includePickingOutputs, options);
    }

    [[nodiscard]] ViewOutputRecipeDescriptor MakeCurrentRendererViewOutputRecipe(
        const RenderWorld& world,
        const CurrentRendererOutputOptions& options)
    {
        const std::uint32_t width = ClampExtentComponent(world.Viewport.Width);
        const std::uint32_t height = ClampExtentComponent(world.Viewport.Height);
        const bool includePickingOutputs =
            options.IncludePickingOutputs || world.HasPendingPick || world.PickRequest.Pending;
        return MakeBaseViewRecipe(width, height, includePickingOutputs, options);
    }

    [[nodiscard]] RenderingContractValidationResult ValidateCurrentRendererContract(
        const CurrentRendererContract& contract)
    {
        return ValidateRenderingContract(contract.Renderer,
                                         contract.Snapshot,
                                         contract.Bindings,
                                         contract.Recipe,
                                         contract.ViewOutput);
    }

    [[nodiscard]] CurrentRendererContract MakeCurrentRendererContract(
        const RenderFrameInput& input,
        const CurrentRendererSnapshotOptions& snapshotOptions,
        const CurrentRendererOutputOptions& outputOptions)
    {
        CurrentRendererContract contract{
            .Renderer = MakeCurrentRendererDescriptor(),
            .Snapshot = MakeCurrentRendererSnapshotEnvelope(input, snapshotOptions),
            .Bindings = MakeCurrentRendererBindingSet(),
            .Recipe = MakeCurrentRendererRecipeDescriptor(),
            .ViewOutput = MakeCurrentRendererViewOutputRecipe(input, outputOptions),
        };
        contract.Diagnostics = ValidateCurrentRendererContract(contract);
        return contract;
    }

    [[nodiscard]] CurrentRendererContract MakeCurrentRendererContract(
        const RenderWorld& world,
        const CurrentRendererSnapshotOptions& snapshotOptions,
        const CurrentRendererOutputOptions& outputOptions)
    {
        CurrentRendererContract contract{
            .Renderer = MakeCurrentRendererDescriptor(),
            .Snapshot = MakeCurrentRendererSnapshotEnvelope(world, snapshotOptions),
            .Bindings = MakeCurrentRendererBindingSet(),
            .Recipe = MakeCurrentRendererRecipeDescriptor(),
            .ViewOutput = MakeCurrentRendererViewOutputRecipe(world, outputOptions),
        };
        contract.Diagnostics = ValidateCurrentRendererContract(contract);
        return contract;
    }
}
