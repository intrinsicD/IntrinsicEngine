module;

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderingContract;

namespace Extrinsic::Graphics
{
    namespace
    {
        template <typename T>
        [[nodiscard]] bool Contains(const std::vector<T>& values, const T value) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        [[nodiscard]] bool ContainsString(const std::vector<std::string>& values,
                                          const std::string_view value) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        void AddDiagnostic(RenderingContractValidationResult& result,
                           const RenderingContractDiagnosticCode code,
                           const RenderingContractDiagnosticSeverity severity,
                           std::string subject,
                           std::string message)
        {
            result.Diagnostics.push_back(RenderingContractDiagnostic{
                .Code = code,
                .Severity = severity,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        void AddError(RenderingContractValidationResult& result,
                      const RenderingContractDiagnosticCode code,
                      std::string subject,
                      std::string message)
        {
            AddDiagnostic(result,
                          code,
                          RenderingContractDiagnosticSeverity::Error,
                          std::move(subject),
                          std::move(message));
        }

        [[nodiscard]] bool RendererDeclaresOutputKind(const RendererDescriptor& renderer,
                                                      const RenderOutputKind kind) noexcept
        {
            return std::any_of(renderer.Outputs.begin(),
                               renderer.Outputs.end(),
                               [kind](const RendererOutputDescriptor& output) {
                                   return output.Kind == kind;
                               });
        }

        [[nodiscard]] bool ViewDeclaresOutputName(const ViewOutputRecipeDescriptor& viewRecipe,
                                                  const std::string_view name) noexcept
        {
            return std::any_of(viewRecipe.Outputs.begin(),
                               viewRecipe.Outputs.end(),
                               [name](const ViewOutputDescriptor& output) {
                                   return output.Name == name;
                               });
        }

        [[nodiscard]] bool BindingSetCoversCategory(const BindingSet& bindings,
                                                    const RenderDataCategory category) noexcept
        {
            return std::any_of(bindings.Intents.begin(),
                               bindings.Intents.end(),
                               [category](const BindingIntent& intent) {
                                   return intent.Category == category &&
                                          intent.Requirement == BindingRequirement::Required;
                               });
        }
    }

    [[nodiscard]] std::string_view ToString(const RendererPurpose value) noexcept
    {
        switch (value)
        {
        case RendererPurpose::Realtime: return "Realtime";
        case RendererPurpose::Offline: return "Offline";
        case RendererPurpose::Preview: return "Preview";
        case RendererPurpose::Picking: return "Picking";
        case RendererPurpose::Metrics: return "Metrics";
        case RendererPurpose::Unknown: return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const SnapshotKind value) noexcept
    {
        switch (value)
        {
        case SnapshotKind::FullScene: return "FullScene";
        case SnapshotKind::SelectedEntity: return "SelectedEntity";
        case SnapshotKind::EntitySet: return "EntitySet";
        case SnapshotKind::StreamingChunk: return "StreamingChunk";
        case SnapshotKind::OfflinePackage: return "OfflinePackage";
        case SnapshotKind::BenchmarkPackage: return "BenchmarkPackage";
        case SnapshotKind::Unknown: return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const SnapshotScope value) noexcept
    {
        switch (value)
        {
        case SnapshotScope::FullScene: return "FullScene";
        case SnapshotScope::Selection: return "Selection";
        case SnapshotScope::EntitySet: return "EntitySet";
        case SnapshotScope::Chunk: return "Chunk";
        case SnapshotScope::Offline: return "Offline";
        case SnapshotScope::Benchmark: return "Benchmark";
        case SnapshotScope::Unknown: return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RendererUpdateMode value) noexcept
    {
        switch (value)
        {
        case RendererUpdateMode::Static: return "Static";
        case RendererUpdateMode::PerFrame: return "PerFrame";
        case RendererUpdateMode::Streaming: return "Streaming";
        case RendererUpdateMode::OnDemand: return "OnDemand";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RenderDataCategory value) noexcept
    {
        switch (value)
        {
        case RenderDataCategory::Geometry: return "Geometry";
        case RenderDataCategory::Materials: return "Materials";
        case RenderDataCategory::Transforms: return "Transforms";
        case RenderDataCategory::Cameras: return "Cameras";
        case RenderDataCategory::Lights: return "Lights";
        case RenderDataCategory::Visibility: return "Visibility";
        case RenderDataCategory::Environment: return "Environment";
        case RenderDataCategory::Picking: return "Picking";
        case RenderDataCategory::Diagnostics: return "Diagnostics";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RendererCapability value) noexcept
    {
        switch (value)
        {
        case RendererCapability::Surface: return "Surface";
        case RendererCapability::Lines: return "Lines";
        case RendererCapability::Points: return "Points";
        case RendererCapability::Shadows: return "Shadows";
        case RendererCapability::Picking: return "Picking";
        case RendererCapability::Readback: return "Readback";
        case RendererCapability::Headless: return "Headless";
        case RendererCapability::Interactive: return "Interactive";
        case RendererCapability::DebugView: return "DebugView";
        case RendererCapability::VisibilityRecipe: return "VisibilityRecipe";
        case RendererCapability::LightingRecipe: return "LightingRecipe";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RenderOutputKind value) noexcept
    {
        switch (value)
        {
        case RenderOutputKind::Color: return "Color";
        case RenderOutputKind::Depth: return "Depth";
        case RenderOutputKind::EntityId: return "EntityId";
        case RenderOutputKind::PrimitiveId: return "PrimitiveId";
        case RenderOutputKind::Metrics: return "Metrics";
        case RenderOutputKind::ReadbackBuffer: return "ReadbackBuffer";
        case RenderOutputKind::Artifact: return "Artifact";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RenderingContractDiagnosticCode value) noexcept
    {
        switch (value)
        {
        case RenderingContractDiagnosticCode::None: return "None";
        case RenderingContractDiagnosticCode::EmptyRendererId: return "EmptyRendererId";
        case RenderingContractDiagnosticCode::UnknownRendererPurpose: return "UnknownRendererPurpose";
        case RenderingContractDiagnosticCode::MissingSupportedSnapshotScope: return "MissingSupportedSnapshotScope";
        case RenderingContractDiagnosticCode::MissingSupportedSnapshotKind: return "MissingSupportedSnapshotKind";
        case RenderingContractDiagnosticCode::MissingUpdateMode: return "MissingUpdateMode";
        case RenderingContractDiagnosticCode::MissingRendererOutput: return "MissingRendererOutput";
        case RenderingContractDiagnosticCode::EmptySnapshotId: return "EmptySnapshotId";
        case RenderingContractDiagnosticCode::SnapshotRendererMismatch: return "SnapshotRendererMismatch";
        case RenderingContractDiagnosticCode::UnsupportedSnapshotScope: return "UnsupportedSnapshotScope";
        case RenderingContractDiagnosticCode::UnsupportedSnapshotKind: return "UnsupportedSnapshotKind";
        case RenderingContractDiagnosticCode::InvalidSnapshotState: return "InvalidSnapshotState";
        case RenderingContractDiagnosticCode::StaleSnapshot: return "StaleSnapshot";
        case RenderingContractDiagnosticCode::MissingSnapshotData: return "MissingSnapshotData";
        case RenderingContractDiagnosticCode::DegradedSnapshot: return "DegradedSnapshot";
        case RenderingContractDiagnosticCode::EmptyBindingRole: return "EmptyBindingRole";
        case RenderingContractDiagnosticCode::MissingRequiredBinding: return "MissingRequiredBinding";
        case RenderingContractDiagnosticCode::UnsupportedBindingCapability: return "UnsupportedBindingCapability";
        case RenderingContractDiagnosticCode::EmptyRecipeId: return "EmptyRecipeId";
        case RenderingContractDiagnosticCode::UnknownRecipeSlot: return "UnknownRecipeSlot";
        case RenderingContractDiagnosticCode::UnsupportedRecipeCapability: return "UnsupportedRecipeCapability";
        case RenderingContractDiagnosticCode::DisallowedRecipeBinding: return "DisallowedRecipeBinding";
        case RenderingContractDiagnosticCode::EmptyViewRecipeId: return "EmptyViewRecipeId";
        case RenderingContractDiagnosticCode::InvalidViewport: return "InvalidViewport";
        case RenderingContractDiagnosticCode::InvalidRenderScale: return "InvalidRenderScale";
        case RenderingContractDiagnosticCode::UnsupportedOutput: return "UnsupportedOutput";
        case RenderingContractDiagnosticCode::UnsupportedReadbackRequest: return "UnsupportedReadbackRequest";
        case RenderingContractDiagnosticCode::EmptyArtifactId: return "EmptyArtifactId";
        case RenderingContractDiagnosticCode::ArtifactRendererMismatch: return "ArtifactRendererMismatch";
        case RenderingContractDiagnosticCode::ArtifactSnapshotMissing: return "ArtifactSnapshotMissing";
        case RenderingContractDiagnosticCode::ArtifactViewRecipeMissing: return "ArtifactViewRecipeMissing";
        case RenderingContractDiagnosticCode::UndeclaredArtifactOutput: return "UndeclaredArtifactOutput";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string_view ToString(const RenderingContractDiagnosticSeverity value) noexcept
    {
        switch (value)
        {
        case RenderingContractDiagnosticSeverity::Info: return "Info";
        case RenderingContractDiagnosticSeverity::Warning: return "Warning";
        case RenderingContractDiagnosticSeverity::Error: return "Error";
        }
        return "Unknown";
    }

    [[nodiscard]] bool HasErrors(const RenderingContractValidationResult& result) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [](const RenderingContractDiagnostic& diagnostic) {
                               return diagnostic.Severity == RenderingContractDiagnosticSeverity::Error;
                           });
    }

    [[nodiscard]] bool IsCompatible(const RenderingContractValidationResult& result) noexcept
    {
        return !HasErrors(result);
    }

    [[nodiscard]] std::uint32_t CountBySeverity(const RenderingContractValidationResult& result,
                                                const RenderingContractDiagnosticSeverity severity) noexcept
    {
        return static_cast<std::uint32_t>(
            std::count_if(result.Diagnostics.begin(),
                          result.Diagnostics.end(),
                          [severity](const RenderingContractDiagnostic& diagnostic) {
                              return diagnostic.Severity == severity;
                          }));
    }

    [[nodiscard]] bool HasDiagnostic(const RenderingContractValidationResult& result,
                                     const RenderingContractDiagnosticCode code) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [code](const RenderingContractDiagnostic& diagnostic) {
                               return diagnostic.Code == code;
                           });
    }

    void AppendDiagnostics(RenderingContractValidationResult& out,
                           const RenderingContractValidationResult& in)
    {
        out.Diagnostics.insert(out.Diagnostics.end(), in.Diagnostics.begin(), in.Diagnostics.end());
    }

    [[nodiscard]] RenderingContractValidationResult MergeDiagnostics(
        std::initializer_list<RenderingContractValidationResult> results)
    {
        RenderingContractValidationResult merged{};
        for (const RenderingContractValidationResult& result : results)
        {
            AppendDiagnostics(merged, result);
        }
        return merged;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateRendererDescriptor(
        const RendererDescriptor& descriptor)
    {
        RenderingContractValidationResult result{};
        if (descriptor.Id.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::EmptyRendererId,
                     "RendererDescriptor.Id",
                     "renderer descriptors must carry a stable id");
        }
        if (descriptor.Purpose == RendererPurpose::Unknown)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::UnknownRendererPurpose,
                     descriptor.Id,
                     "renderer descriptors must declare their purpose");
        }
        if (descriptor.SupportedSnapshotScopes.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::MissingSupportedSnapshotScope,
                     descriptor.Id,
                     "renderer descriptors must declare at least one supported snapshot scope");
        }
        if (descriptor.SupportedSnapshotKinds.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::MissingSupportedSnapshotKind,
                     descriptor.Id,
                     "renderer descriptors must declare at least one supported snapshot kind");
        }
        if (descriptor.UpdateModes.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::MissingUpdateMode,
                     descriptor.Id,
                     "renderer descriptors must declare at least one update mode");
        }
        if (descriptor.Outputs.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::MissingRendererOutput,
                     descriptor.Id,
                     "renderer descriptors must declare at least one output");
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateSnapshotEnvelope(
        const RendererDescriptor& renderer,
        const SnapshotEnvelope& snapshot)
    {
        RenderingContractValidationResult result = ValidateRendererDescriptor(renderer);
        if (snapshot.Id.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::EmptySnapshotId,
                     "SnapshotEnvelope.Id",
                     "snapshot envelopes must carry a stable id");
        }
        if (!snapshot.ConsumerRendererId.empty() && snapshot.ConsumerRendererId != renderer.Id)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::SnapshotRendererMismatch,
                     snapshot.Id,
                     "snapshot consumer renderer does not match the renderer descriptor");
        }
        if (!Contains(renderer.SupportedSnapshotScopes, snapshot.Scope))
        {
            AddError(result,
                     RenderingContractDiagnosticCode::UnsupportedSnapshotScope,
                     snapshot.Id,
                     "snapshot scope is not declared by the renderer descriptor");
        }
        if (!Contains(renderer.SupportedSnapshotKinds, snapshot.Kind))
        {
            AddError(result,
                     RenderingContractDiagnosticCode::UnsupportedSnapshotKind,
                     snapshot.Id,
                     "snapshot kind is not declared by the renderer descriptor");
        }
        if (snapshot.ValidationState != SnapshotValidationState::Valid)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::InvalidSnapshotState,
                     snapshot.Id,
                     "snapshot validation state must be valid before rendering");
        }
        if (snapshot.Stale || snapshot.ValidationState == SnapshotValidationState::Stale)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::StaleSnapshot,
                     snapshot.Id,
                     "stale snapshots fail closed until refreshed");
        }
        if (snapshot.MissingData)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::MissingSnapshotData,
                     snapshot.Id,
                     "snapshots with missing required data fail closed");
        }
        if (snapshot.Degraded)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::DegradedSnapshot,
                     snapshot.Id,
                     "degraded snapshots fail closed at the public contract boundary");
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateBindingSet(
        const RendererDescriptor& renderer,
        const BindingSet& bindings)
    {
        RenderingContractValidationResult result = ValidateRendererDescriptor(renderer);
        for (const RenderDataCategory category : renderer.RequiredDataCategories)
        {
            if (!BindingSetCoversCategory(bindings, category))
            {
                AddError(result,
                         RenderingContractDiagnosticCode::MissingRequiredBinding,
                         std::string{ToString(category)},
                         "required renderer data category is not covered by a required binding intent");
            }
        }

        for (const BindingIntent& intent : bindings.Intents)
        {
            if (intent.SemanticName.empty())
            {
                AddError(result,
                         RenderingContractDiagnosticCode::EmptyBindingRole,
                         std::string{ToString(intent.Category)},
                         "binding intents must name the semantic role they satisfy");
            }
            if (intent.RequiredCapability.has_value() &&
                !Contains(renderer.SupportedCapabilities, *intent.RequiredCapability))
            {
                AddError(result,
                         RenderingContractDiagnosticCode::UnsupportedBindingCapability,
                         intent.SemanticName,
                         "binding intent requires a renderer capability that is not declared");
            }
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateRenderRecipeDescriptor(
        const RendererDescriptor& renderer,
        const RenderRecipeDescriptor& recipe)
    {
        RenderingContractValidationResult result = ValidateRendererDescriptor(renderer);
        if (recipe.RecipeId.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::EmptyRecipeId,
                     "RenderRecipeDescriptor.RecipeId",
                     "render recipes must carry a stable id");
        }

        for (const RecipeExtensionSlotDescriptor& slot : recipe.Slots)
        {
            if (slot.StableName.empty())
            {
                AddError(result,
                         RenderingContractDiagnosticCode::UnknownRecipeSlot,
                         recipe.RecipeId,
                         "recipe slots must carry a stable name");
                continue;
            }
            if (slot.Kind == RecipeSlotKind::Extension &&
                !ContainsString(renderer.DeclaredRecipeSlots, slot.StableName))
            {
                AddError(result,
                         RenderingContractDiagnosticCode::UnknownRecipeSlot,
                         slot.StableName,
                         "extension recipe slot is not declared by the renderer descriptor");
            }
            for (const RendererCapability capability : slot.RequiredCapabilities)
            {
                if (!Contains(renderer.SupportedCapabilities, capability))
                {
                    AddError(result,
                             RenderingContractDiagnosticCode::UnsupportedRecipeCapability,
                             slot.StableName,
                             "recipe slot requires a renderer capability that is not declared");
                }
            }
            for (const std::string& usedRole : slot.UsedBindingRoles)
            {
                if (!ContainsString(slot.AllowedBindingRoles, usedRole))
                {
                    AddError(result,
                             RenderingContractDiagnosticCode::DisallowedRecipeBinding,
                             slot.StableName,
                             "recipe slot references a binding role outside its allowed binding set");
                }
            }
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateViewOutputRecipe(
        const RendererDescriptor& renderer,
        const ViewOutputRecipeDescriptor& recipe)
    {
        RenderingContractValidationResult result = ValidateRendererDescriptor(renderer);
        if (recipe.RecipeId.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::EmptyViewRecipeId,
                     "ViewOutputRecipeDescriptor.RecipeId",
                     "view/output recipes must carry a stable id");
        }
        if (recipe.ViewportWidth == 0u || recipe.ViewportHeight == 0u)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::InvalidViewport,
                     recipe.RecipeId,
                     "view/output recipes must use non-zero viewport dimensions");
        }
        if (!(recipe.RenderScale > 0.0f))
        {
            AddError(result,
                     RenderingContractDiagnosticCode::InvalidRenderScale,
                     recipe.RecipeId,
                     "view/output recipes must use a positive render scale");
        }
        if (recipe.ReadbackRequested &&
            !Contains(renderer.SupportedCapabilities, RendererCapability::Readback))
        {
            AddError(result,
                     RenderingContractDiagnosticCode::UnsupportedReadbackRequest,
                     recipe.RecipeId,
                     "view/output recipe requests readback from a renderer without readback capability");
        }
        for (const ViewOutputDescriptor& output : recipe.Outputs)
        {
            if (!RendererDeclaresOutputKind(renderer, output.Kind))
            {
                AddError(result,
                         RenderingContractDiagnosticCode::UnsupportedOutput,
                         output.Name,
                         "view/output recipe declares an output kind the renderer does not support");
            }
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateRenderArtifactMetadata(
        const RendererDescriptor& renderer,
        const ViewOutputRecipeDescriptor& viewRecipe,
        const RenderArtifactMetadata& artifact)
    {
        RenderingContractValidationResult result{};
        if (artifact.ArtifactId.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::EmptyArtifactId,
                     "RenderArtifactMetadata.ArtifactId",
                     "render artifacts must carry a stable id");
        }
        if (artifact.RendererId != renderer.Id)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::ArtifactRendererMismatch,
                     artifact.ArtifactId,
                     "artifact renderer id does not match the renderer descriptor");
        }
        if (artifact.SnapshotId.empty())
        {
            AddError(result,
                     RenderingContractDiagnosticCode::ArtifactSnapshotMissing,
                     artifact.ArtifactId,
                     "artifacts must name the snapshot that produced them");
        }
        if (artifact.ViewOutputRecipeId.empty() || artifact.ViewOutputRecipeId != viewRecipe.RecipeId)
        {
            AddError(result,
                     RenderingContractDiagnosticCode::ArtifactViewRecipeMissing,
                     artifact.ArtifactId,
                     "artifacts must name the view/output recipe that produced them");
        }
        if (!artifact.Purpose.empty() && !ViewDeclaresOutputName(viewRecipe, artifact.Purpose))
        {
            AddError(result,
                     RenderingContractDiagnosticCode::UndeclaredArtifactOutput,
                     artifact.ArtifactId,
                     "artifact purpose must match a declared view/output output name");
        }
        return result;
    }

    [[nodiscard]] RenderingContractValidationResult ValidateRenderingContract(
        const RendererDescriptor& renderer,
        const SnapshotEnvelope& snapshot,
        const BindingSet& bindings,
        const RenderRecipeDescriptor& recipe,
        const ViewOutputRecipeDescriptor& viewRecipe)
    {
        return MergeDiagnostics({
            ValidateRendererDescriptor(renderer),
            ValidateSnapshotEnvelope(renderer, snapshot),
            ValidateBindingSet(renderer, bindings),
            ValidateRenderRecipeDescriptor(renderer, recipe),
            ValidateViewOutputRecipe(renderer, viewRecipe),
        });
    }

    [[nodiscard]] RenderArtifactLifecycleClass ClassifyRenderArtifactLifecycle(
        const RenderArtifactMetadata& artifact) noexcept
    {
        switch (artifact.Status)
        {
        case RenderArtifactStatus::Declared:
            return RenderArtifactLifecycleClass::Declared;
        case RenderArtifactStatus::Available:
            switch (artifact.Lifetime)
            {
            case RenderArtifactLifetime::Transient:
                return RenderArtifactLifecycleClass::TransientAvailable;
            case RenderArtifactLifetime::Cached:
                return RenderArtifactLifecycleClass::CachedAvailable;
            case RenderArtifactLifetime::Published:
                return RenderArtifactLifecycleClass::Published;
            }
            break;
        case RenderArtifactStatus::Stale:
            return RenderArtifactLifecycleClass::Stale;
        case RenderArtifactStatus::Missing:
            return RenderArtifactLifecycleClass::Missing;
        case RenderArtifactStatus::Failed:
            return RenderArtifactLifecycleClass::Failed;
        case RenderArtifactStatus::Published:
            return RenderArtifactLifecycleClass::Published;
        case RenderArtifactStatus::Discarded:
            return RenderArtifactLifecycleClass::Discarded;
        }
        return RenderArtifactLifecycleClass::Invalid;
    }
}
