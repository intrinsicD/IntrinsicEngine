module;

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.SandboxEditorFacades;

import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.RenderArtifactPublication;

namespace Extrinsic::Runtime
{
    namespace
    {
        void AddDiagnostic(std::vector<SandboxEditorDiagnostic>& diagnostics,
                           const SandboxEditorDiagnosticCode code,
                           std::string message)
        {
            diagnostics.push_back(SandboxEditorDiagnostic{
                .Code = code,
                .Message = std::move(message),
            });
        }

        [[nodiscard]] const Graphics::RecipeExtensionSlotDescriptor*
        FindRecipeSlot(const Graphics::RenderRecipeDescriptor& recipe,
                       const std::string_view stableName) noexcept
        {
            const auto it = std::find_if(
                recipe.Slots.begin(),
                recipe.Slots.end(),
                [stableName](const Graphics::RecipeExtensionSlotDescriptor& slot)
                {
                    return slot.StableName == stableName;
                });
            return it == recipe.Slots.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool RendererDeclaresSlot(
            const Graphics::RendererDescriptor& renderer,
            const std::string_view stableName) noexcept
        {
            return std::find(renderer.DeclaredRecipeSlots.begin(),
                             renderer.DeclaredRecipeSlots.end(),
                             stableName) != renderer.DeclaredRecipeSlots.end();
        }

        [[nodiscard]] const char* DebugNameForBindingSourceDomain(
            const Graphics::BindingSourceDomain domain) noexcept
        {
            using Domain = Graphics::BindingSourceDomain;
            switch (domain)
            {
            case Domain::Unknown: return "Unknown";
            case Domain::MeshVertex: return "MeshVertex";
            case Domain::MeshFace: return "MeshFace";
            case Domain::GraphNode: return "GraphNode";
            case Domain::GraphEdge: return "GraphEdge";
            case Domain::PointCloudPoint: return "PointCloudPoint";
            case Domain::Scene: return "Scene";
            case Domain::Generated: return "Generated";
            case Domain::Runtime: return "Runtime";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForBindingValueType(
            const Graphics::BindingValueType type) noexcept
        {
            using Type = Graphics::BindingValueType;
            switch (type)
            {
            case Type::Unknown: return "Unknown";
            case Type::Float: return "Float";
            case Type::UInt: return "UInt";
            case Type::Vec2: return "Vec2";
            case Type::Vec3: return "Vec3";
            case Type::Vec4: return "Vec4";
            case Type::Mat4: return "Mat4";
            case Type::Texture2D: return "Texture2D";
            case Type::Buffer: return "Buffer";
            case Type::AccelerationStructure: return "AccelerationStructure";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForViewKind(
            const Graphics::ViewKind view) noexcept
        {
            using View = Graphics::ViewKind;
            switch (view)
            {
            case View::Camera: return "Camera";
            case View::NonCamera: return "NonCamera";
            case View::Picking: return "Picking";
            case View::Metrics: return "Metrics";
            case View::Preview: return "Preview";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForOutputTargetKind(
            const Graphics::OutputTargetKind target) noexcept
        {
            using Target = Graphics::OutputTargetKind;
            switch (target)
            {
            case Target::Window: return "Window";
            case Target::OffscreenTexture: return "OffscreenTexture";
            case Target::File: return "File";
            case Target::ReadbackBuffer: return "ReadbackBuffer";
            case Target::PublishedArtifact: return "PublishedArtifact";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForInteractionMode(
            const Graphics::InteractionMode mode) noexcept
        {
            switch (mode)
            {
            case Graphics::InteractionMode::Interactive:
                return "Interactive";
            case Graphics::InteractionMode::Headless:
                return "Headless";
            }
            return "Unknown";
        }

        [[nodiscard]] std::vector<std::string> CapabilityNames(
            const std::vector<Graphics::RendererCapability>& capabilities)
        {
            std::vector<std::string> names{};
            names.reserve(capabilities.size());
            for (const Graphics::RendererCapability capability : capabilities)
            {
                names.emplace_back(Graphics::ToString(capability));
            }
            return names;
        }

        [[nodiscard]] SandboxEditorRenderRecipeSlotModel BuildRenderRecipeSlotRow(
            const Graphics::RendererDescriptor& renderer,
            const Graphics::RecipeExtensionSlotDescriptor& slot)
        {
            const bool declared = RendererDeclaresSlot(renderer, slot.StableName);
            const bool editable =
                declared && slot.Kind == Graphics::RecipeSlotKind::Extension;
            std::string disabledReason{};
            if (!declared)
                disabledReason = "not declared by renderer";
            else if (slot.Kind == Graphics::RecipeSlotKind::FixedCore)
                disabledReason = "fixed renderer core";

            return SandboxEditorRenderRecipeSlotModel{
                .StableName = slot.StableName,
                .Kind = slot.Kind,
                .SchemaId = slot.SchemaId,
                .Defaults = slot.Defaults,
                .RequiredCapabilities = CapabilityNames(slot.RequiredCapabilities),
                .AllowedBindingRoles = slot.AllowedBindingRoles,
                .UsedBindingRoles = slot.UsedBindingRoles,
                .DeclaredByRenderer = declared,
                .Editable = editable,
                .DisabledReason = std::move(disabledReason),
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeBindingOverrideModel
        BuildRenderRecipeBindingRow(const Graphics::BindingIntent& intent)
        {
            const bool required =
                intent.Requirement == Graphics::BindingRequirement::Required;
            return SandboxEditorRenderRecipeBindingOverrideModel{
                .SemanticName = intent.SemanticName,
                .Slot = intent.ConsumerRole,
                .SourceDomain = DebugNameForBindingSourceDomain(intent.SourceDomain),
                .SourceIdentity = intent.SourceIdentity,
                .SourceRevision = intent.SourceRevision,
                .ValueType = DebugNameForBindingValueType(intent.ValueType),
                .ValueFormat = intent.ValueFormat,
                .Required = required,
                .Editable = !required,
                .DisabledReason = required ? "required binding" : "",
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeOutputModel BuildRenderRecipeOutputRow(
            const Graphics::ViewOutputDescriptor& output)
        {
            return SandboxEditorRenderRecipeOutputModel{
                .Name = output.Name,
                .Kind = std::string{Graphics::ToString(output.Kind)},
                .Format = output.Format,
                .Required = output.Required,
            };
        }

        [[nodiscard]] SandboxEditorRenderArtifactRow BuildRenderArtifactRow(
            const RenderArtifactRecord& record)
        {
            const RenderArtifactUiStatus status = ToUiStatus(record);
            const bool canPublish =
                status == RenderArtifactPublicationState::Unpublished;
            const bool canApply =
                status == RenderArtifactPublicationState::Published &&
                record.Kind == RenderArtifactPublicationKind::CandidateProjectResult;
            std::string disabledReason{};
            if (!canPublish && !canApply)
            {
                disabledReason = std::string{"state:"} +
                                 std::string{ToString(status)};
            }
            return SandboxEditorRenderArtifactRow{
                .ArtifactId = record.Metadata.ArtifactId,
                .Purpose = record.Metadata.Purpose,
                .Kind = record.Kind,
                .Status = status,
                .PayloadUri = record.PayloadUri,
                .ProducerLabel = record.ProducerLabel,
                .CanPublish = canPublish,
                .CanApply = canApply,
                .DisabledReason = std::move(disabledReason),
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeCommandResult
        MakeRenderRecipeCommandResult(
            const SandboxEditorRenderRecipeCommandStatus status,
            const SandboxEditorRenderRecipeEditorState* state)
        {
            SandboxEditorRenderRecipeCommandResult result{
                .Status = status,
            };
            if (state != nullptr)
            {
                result.DraftState = state->DraftState;
                if (state->HasLastPreview)
                {
                    result.ValidationState = state->LastPreview.State;
                    result.RecipeDiagnostics = state->LastPreview.Diagnostics;
                }
            }
            return result;
        }

        [[nodiscard]] SandboxEditorRenderRecipeCommandResult
        MakeRenderRecipeArtifactResult(
            const RenderArtifactOperationResult& operation,
            const SandboxEditorRenderRecipeCommandStatus successStatus)
        {
            return SandboxEditorRenderRecipeCommandResult{
                .Status = operation.Succeeded()
                    ? successStatus
                    : SandboxEditorRenderRecipeCommandStatus::ArtifactCommandFailed,
                .ArtifactStatus = operation.Status,
                .ArtifactState = operation.State,
                .ArtifactId = operation.ArtifactId,
                .Revision = operation.Revision,
                .ProjectMutationAuthorized = operation.ProjectMutationAuthorized,
                .ArtifactDiagnostics = operation.Diagnostics,
            };
        }
    }

    SandboxEditorRenderRecipeEditorModel
    BuildSandboxEditorRenderRecipeEditorModel(const SandboxEditorContext& context)
    {
        SandboxEditorRenderRecipeEditorModel model{};
        if (context.RenderRecipeContext == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable,
                          "Render recipe context is unavailable.");
            return model;
        }

        model.Available = true;
        const Graphics::RenderRecipeConfigContext& recipeContext =
            *context.RenderRecipeContext;
        const SandboxEditorRenderRecipeEditorState* state =
            context.RenderRecipeEditorState;

        const RuntimeRenderRecipeState* runtimeState =
            context.RenderRecipeRuntimeState;
        const bool useRuntimeActiveOverride =
            runtimeState != nullptr && runtimeState->HasActiveConfig;
        const bool useEditorActiveOverride =
            !useRuntimeActiveOverride &&
            state != nullptr &&
            state->HasActiveOverride;
        const Graphics::RenderRecipeDescriptor& recipe =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.Recipe
                : (useEditorActiveOverride ? state->ActiveRecipe : recipeContext.BaseRecipe);
        const Graphics::ViewOutputRecipeDescriptor& viewOutput =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.ViewOutput
                : (useEditorActiveOverride ? state->ActiveViewOutput : recipeContext.BaseViewOutput);
        const Graphics::BindingSet& bindings =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.Bindings
                : (useEditorActiveOverride ? state->ActiveBindings : recipeContext.BaseBindings);

        model.RendererId = recipeContext.Renderer.Id;
        model.ActiveRecipeId = recipe.RecipeId;
        model.ActiveViewOutputRecipeId = viewOutput.RecipeId;
        model.ViewKind = DebugNameForViewKind(viewOutput.View);
        model.OutputTarget = DebugNameForOutputTargetKind(viewOutput.Target);
        model.InteractionMode = DebugNameForInteractionMode(viewOutput.Mode);
        model.ViewportWidth = viewOutput.ViewportWidth;
        model.ViewportHeight = viewOutput.ViewportHeight;
        model.RenderScale = viewOutput.RenderScale;
        model.CaptureRequested = viewOutput.CaptureRequested;
        model.ReadbackRequested = viewOutput.ReadbackRequested;

        if (state != nullptr)
        {
            model.DraftSourceId = state->DraftSourceId;
            model.DraftState = state->DraftState;
            model.DraftRevision = state->DraftRevision;
            model.ActiveRevision = state->ActiveRevision;
            if (state->HasLastPreview)
            {
                model.ValidationState = state->LastPreview.State;
                model.DraftRecipeId = state->LastPreview.Preview.Recipe.RecipeId;
                model.ParsedSlotCount =
                    state->LastPreview.Preview.ParsedSlotCount;
                model.ParsedBindingOverrideCount =
                    state->LastPreview.Preview.ParsedBindingOverrideCount;
                model.RecipeDiagnostics = state->LastPreview.Diagnostics;
            }
        }

        if (!context.RenderRecipeCommandsAvailable)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable,
                          "Render recipe command seams are unavailable.");
        }

        model.CanValidate =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            !state->DraftDocument.empty();
        model.CanPreview = model.CanValidate;
        model.CanActivate =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            state->HasLastPreview &&
            Graphics::IsConfigUsable(state->LastPreview);
        model.CanCancel =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            state->DraftState != SandboxEditorRenderRecipeDraftState::InactiveDraft &&
            state->DraftState != SandboxEditorRenderRecipeDraftState::Canceled;

        model.Slots.reserve(recipeContext.Renderer.DeclaredRecipeSlots.size() +
                            recipe.Slots.size());
        for (const std::string& slotName : recipeContext.Renderer.DeclaredRecipeSlots)
        {
            if (const Graphics::RecipeExtensionSlotDescriptor* slot =
                    FindRecipeSlot(recipe, slotName);
                slot != nullptr)
            {
                model.Slots.push_back(
                    BuildRenderRecipeSlotRow(recipeContext.Renderer, *slot));
            }
            else
            {
                model.Slots.push_back(SandboxEditorRenderRecipeSlotModel{
                    .StableName = slotName,
                    .DeclaredByRenderer = true,
                    .Editable = true,
                });
            }
        }
        for (const Graphics::RecipeExtensionSlotDescriptor& slot : recipe.Slots)
        {
            if (!RendererDeclaresSlot(recipeContext.Renderer, slot.StableName))
            {
                model.Slots.push_back(
                    BuildRenderRecipeSlotRow(recipeContext.Renderer, slot));
            }
        }

        model.BindingOverrides.reserve(bindings.Intents.size());
        for (const Graphics::BindingIntent& intent : bindings.Intents)
        {
            model.BindingOverrides.push_back(BuildRenderRecipeBindingRow(intent));
        }

        model.Outputs.reserve(viewOutput.Outputs.size());
        for (const Graphics::ViewOutputDescriptor& output : viewOutput.Outputs)
        {
            model.Outputs.push_back(BuildRenderRecipeOutputRow(output));
        }

        if (context.RenderArtifacts != nullptr)
        {
            std::vector<RenderArtifactRecord> artifacts =
                context.RenderArtifacts->Snapshot();
            std::sort(artifacts.begin(),
                      artifacts.end(),
                      [](const RenderArtifactRecord& lhs,
                         const RenderArtifactRecord& rhs)
                      {
                          return lhs.Metadata.ArtifactId <
                                 rhs.Metadata.ArtifactId;
                      });
            model.Artifacts.reserve(artifacts.size());
            for (const RenderArtifactRecord& artifact : artifacts)
            {
                model.Artifacts.push_back(BuildRenderArtifactRow(artifact));
            }
        }
        return model;
    }

    SandboxEditorRenderRecipeCommandResult
    ApplySandboxEditorRenderRecipeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderRecipeCommand& command)
    {
        SandboxEditorRenderRecipeEditorState* state =
            context.RenderRecipeEditorState;
        if (state == nullptr)
        {
            return SandboxEditorRenderRecipeCommandResult{
                .Status = SandboxEditorRenderRecipeCommandStatus::MissingEditorState,
            };
        }

        using Kind = SandboxEditorRenderRecipeCommandKind;
        switch (command.Kind)
        {
        case Kind::UpdateDraft:
        {
            if (state->DraftDocument == command.Document &&
                state->DraftSourceId == command.SourceId &&
                !command.Debounced)
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::NoChange,
                    state);
            }
            state->DraftDocument = command.Document;
            if (!command.SourceId.empty())
                state->DraftSourceId = command.SourceId;
            state->HasLastPreview = false;
            ++state->DraftRevision;
            state->DraftState = command.Debounced
                ? SandboxEditorRenderRecipeDraftState::Debounced
                : SandboxEditorRenderRecipeDraftState::InactiveDraft;
            return MakeRenderRecipeCommandResult(
                command.Debounced
                    ? SandboxEditorRenderRecipeCommandStatus::Debounced
                    : SandboxEditorRenderRecipeCommandStatus::DraftUpdated,
                state);
        }
        case Kind::ValidateDraft:
        case Kind::PreviewDraft:
        {
            if (!context.PreviewRenderRecipeDocument)
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::MissingRecipeContext,
                    state);
            }
            if (!command.Document.empty())
            {
                state->DraftDocument = command.Document;
                ++state->DraftRevision;
            }
            if (!command.SourceId.empty())
                state->DraftSourceId = command.SourceId;

            state->LastPreview = context.PreviewRenderRecipeDocument(
                state->DraftDocument,
                state->DraftSourceId);
            state->HasLastPreview = true;
            const bool usable = Graphics::IsConfigUsable(state->LastPreview);
            if (usable)
            {
                state->DraftState = command.Kind == Kind::ValidateDraft
                    ? SandboxEditorRenderRecipeDraftState::Validated
                    : SandboxEditorRenderRecipeDraftState::Previewed;
                return MakeRenderRecipeCommandResult(
                    command.Kind == Kind::ValidateDraft
                        ? SandboxEditorRenderRecipeCommandStatus::Validated
                        : SandboxEditorRenderRecipeCommandStatus::Previewed,
                    state);
            }

            state->DraftState = SandboxEditorRenderRecipeDraftState::Rejected;
            return MakeRenderRecipeCommandResult(
                command.Kind == Kind::ValidateDraft
                    ? SandboxEditorRenderRecipeCommandStatus::ValidationFailed
                    : SandboxEditorRenderRecipeCommandStatus::PreviewFailed,
                state);
        }
        case Kind::ActivatePreview:
        {
            if (!state->HasLastPreview ||
                !Graphics::IsConfigUsable(state->LastPreview))
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::PreviewFailed,
                    state);
            }
            if (context.ApplyRenderRecipePreview)
            {
                const RuntimeRenderRecipeApplyResult applyResult =
                    context.ApplyRenderRecipePreview(state->LastPreview);
                if (!applyResult.Succeeded())
                {
                    return MakeRenderRecipeCommandResult(
                        SandboxEditorRenderRecipeCommandStatus::ActivationFailed,
                        state);
                }
            }
            state->ActiveRecipe = state->LastPreview.Preview.Recipe;
            state->ActiveViewOutput = state->LastPreview.Preview.ViewOutput;
            state->ActiveBindings = state->LastPreview.Preview.Bindings;
            state->HasActiveOverride = true;
            state->DraftState = SandboxEditorRenderRecipeDraftState::Activated;
            ++state->ActiveRevision;
            return MakeRenderRecipeCommandResult(
                SandboxEditorRenderRecipeCommandStatus::Activated,
                state);
        }
        case Kind::CancelDraft:
        {
            if (state->DraftDocument.empty() &&
                !state->HasLastPreview &&
                (state->DraftState ==
                     SandboxEditorRenderRecipeDraftState::InactiveDraft ||
                 state->DraftState ==
                     SandboxEditorRenderRecipeDraftState::Canceled))
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::NoChange,
                    state);
            }
            state->DraftDocument.clear();
            state->HasLastPreview = false;
            state->DraftState = SandboxEditorRenderRecipeDraftState::Canceled;
            ++state->DraftRevision;
            return MakeRenderRecipeCommandResult(
                SandboxEditorRenderRecipeCommandStatus::Canceled,
                state);
        }
        case Kind::PublishArtifact:
        {
            if (context.RenderArtifacts == nullptr)
            {
                return SandboxEditorRenderRecipeCommandResult{
                    .Status = SandboxEditorRenderRecipeCommandStatus::MissingArtifactRegistry,
                };
            }
            const std::string targetUri = command.TargetUri.empty()
                ? "sandbox://render-artifacts/" + command.ArtifactId
                : command.TargetUri;
            RenderArtifactOperationResult operation =
                context.RenderArtifacts->PublishArtifact(
                    RenderArtifactPublishCommand{
                        .ArtifactId = command.ArtifactId,
                        .Provenance = command.Provenance,
                        .TargetUri = targetUri,
                        .Label = command.Label.empty()
                            ? std::string{"Publish Render Artifact"}
                            : command.Label,
                        .UndoLabel = command.UndoLabel.empty()
                            ? std::string{"Unpublish Render Artifact"}
                            : command.UndoLabel,
                    });
            return MakeRenderRecipeArtifactResult(
                operation,
                SandboxEditorRenderRecipeCommandStatus::Published);
        }
        case Kind::ApplyArtifact:
        {
            if (context.RenderArtifacts == nullptr)
            {
                return SandboxEditorRenderRecipeCommandResult{
                    .Status = SandboxEditorRenderRecipeCommandStatus::MissingArtifactRegistry,
                };
            }
            RenderArtifactOperationResult operation =
                context.RenderArtifacts->ApplyArtifact(
                    RenderArtifactApplyCommand{
                        .ArtifactId = command.ArtifactId,
                        .Provenance = command.Provenance,
                        .ProjectTarget = command.ProjectTarget.empty()
                            ? std::string{"sandbox-render-recipe-artifact"}
                            : command.ProjectTarget,
                        .Label = command.Label.empty()
                            ? std::string{"Apply Render Artifact"}
                            : command.Label,
                        .UndoLabel = command.UndoLabel.empty()
                            ? std::string{"Revert Render Artifact Apply"}
                            : command.UndoLabel,
                    });
            return MakeRenderRecipeArtifactResult(
                operation,
                SandboxEditorRenderRecipeCommandStatus::Applied);
        }
        }

        return MakeRenderRecipeCommandResult(
            SandboxEditorRenderRecipeCommandStatus::NoChange,
            state);
    }

}
