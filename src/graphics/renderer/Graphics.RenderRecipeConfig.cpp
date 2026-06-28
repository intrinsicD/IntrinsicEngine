module;

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Graphics.RenderRecipeConfig;

namespace Extrinsic::Graphics
{
    namespace
    {
        using json = nlohmann::json;

        [[nodiscard]] bool ContainsString(const std::vector<std::string>& values,
                                          const std::string_view value) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        template <typename T>
        [[nodiscard]] bool Contains(const std::vector<T>& values, const T value) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        [[nodiscard]] bool JsonContains(const json& object, const std::string_view key)
        {
            return object.contains(std::string{key});
        }

        [[nodiscard]] const json* FindMember(const json& object, const std::string_view key)
        {
            const auto it = object.find(std::string{key});
            return it == object.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool IsObjectWithOnly(const json& object,
                                            const std::initializer_list<std::string_view> allowed) noexcept
        {
            if (!object.is_object())
            {
                return false;
            }
            for (const auto& [key, value] : object.items())
            {
                (void)value;
                if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                {
                    return false;
                }
            }
            return true;
        }

        void AddDiagnostic(RenderRecipeConfigLoadResult& result,
                           const RenderRecipeConfigState state,
                           const RenderingContractDiagnosticSeverity severity,
                           const RenderRecipeConfigDiagnosticCode code,
                           std::string subject,
                           std::string message)
        {
            result.Diagnostics.push_back(RenderRecipeConfigDiagnostic{
                .State = state,
                .Severity = severity,
                .Code = code,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        void AddError(RenderRecipeConfigLoadResult& result,
                      const RenderRecipeConfigState state,
                      const RenderRecipeConfigDiagnosticCode code,
                      std::string subject,
                      std::string message)
        {
            AddDiagnostic(result,
                          state,
                          RenderingContractDiagnosticSeverity::Error,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        void AddWarning(RenderRecipeConfigLoadResult& result,
                        const RenderRecipeConfigState state,
                        const RenderRecipeConfigDiagnosticCode code,
                        std::string subject,
                        std::string message)
        {
            AddDiagnostic(result,
                          state,
                          RenderingContractDiagnosticSeverity::Warning,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        void AddInfo(RenderRecipeConfigLoadResult& result,
                     const RenderRecipeConfigState state,
                     const RenderRecipeConfigDiagnosticCode code,
                     std::string subject,
                     std::string message)
        {
            AddDiagnostic(result,
                          state,
                          RenderingContractDiagnosticSeverity::Info,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        [[nodiscard]] std::optional<RendererCapability> ParseRendererCapability(
            const std::string_view value) noexcept
        {
            if (value == "Surface") return RendererCapability::Surface;
            if (value == "Lines") return RendererCapability::Lines;
            if (value == "Points") return RendererCapability::Points;
            if (value == "Shadows") return RendererCapability::Shadows;
            if (value == "Picking") return RendererCapability::Picking;
            if (value == "Readback") return RendererCapability::Readback;
            if (value == "Headless") return RendererCapability::Headless;
            if (value == "Interactive") return RendererCapability::Interactive;
            if (value == "DebugView") return RendererCapability::DebugView;
            if (value == "VisibilityRecipe") return RendererCapability::VisibilityRecipe;
            if (value == "LightingRecipe") return RendererCapability::LightingRecipe;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<RenderOutputKind> ParseRenderOutputKind(
            const std::string_view value) noexcept
        {
            if (value == "Color") return RenderOutputKind::Color;
            if (value == "Depth") return RenderOutputKind::Depth;
            if (value == "EntityId") return RenderOutputKind::EntityId;
            if (value == "PrimitiveId") return RenderOutputKind::PrimitiveId;
            if (value == "Metrics") return RenderOutputKind::Metrics;
            if (value == "ReadbackBuffer") return RenderOutputKind::ReadbackBuffer;
            if (value == "Artifact") return RenderOutputKind::Artifact;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<RenderingFallbackPolicy> ParseFallbackPolicy(
            const std::string_view value) noexcept
        {
            if (value == "FailClosed") return RenderingFallbackPolicy::FailClosed;
            if (value == "Degrade") return RenderingFallbackPolicy::Degrade;
            if (value == "SubstituteDefaults") return RenderingFallbackPolicy::SubstituteDefaults;
            if (value == "PreservePrevious") return RenderingFallbackPolicy::PreservePrevious;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<BindingSourceDomain> ParseBindingSourceDomain(
            const std::string_view value) noexcept
        {
            if (value == "MeshVertex") return BindingSourceDomain::MeshVertex;
            if (value == "MeshFace") return BindingSourceDomain::MeshFace;
            if (value == "GraphNode") return BindingSourceDomain::GraphNode;
            if (value == "GraphEdge") return BindingSourceDomain::GraphEdge;
            if (value == "PointCloudPoint") return BindingSourceDomain::PointCloudPoint;
            if (value == "Scene") return BindingSourceDomain::Scene;
            if (value == "Generated") return BindingSourceDomain::Generated;
            if (value == "Runtime") return BindingSourceDomain::Runtime;
            if (value == "Unknown") return BindingSourceDomain::Unknown;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<BindingValueType> ParseBindingValueType(
            const std::string_view value) noexcept
        {
            if (value == "Float") return BindingValueType::Float;
            if (value == "UInt") return BindingValueType::UInt;
            if (value == "Vec2") return BindingValueType::Vec2;
            if (value == "Vec3") return BindingValueType::Vec3;
            if (value == "Vec4") return BindingValueType::Vec4;
            if (value == "Mat4") return BindingValueType::Mat4;
            if (value == "Texture2D") return BindingValueType::Texture2D;
            if (value == "Buffer") return BindingValueType::Buffer;
            if (value == "AccelerationStructure") return BindingValueType::AccelerationStructure;
            if (value == "Unknown") return BindingValueType::Unknown;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<BindingColorSpace> ParseBindingColorSpace(
            const std::string_view value) noexcept
        {
            if (value == "None") return BindingColorSpace::None;
            if (value == "Linear") return BindingColorSpace::Linear;
            if (value == "SRGB") return BindingColorSpace::SRGB;
            if (value == "NormalizedData") return BindingColorSpace::NormalizedData;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ViewKind> ParseViewKind(const std::string_view value) noexcept
        {
            if (value == "Camera") return ViewKind::Camera;
            if (value == "NonCamera") return ViewKind::NonCamera;
            if (value == "Picking") return ViewKind::Picking;
            if (value == "Metrics") return ViewKind::Metrics;
            if (value == "Preview") return ViewKind::Preview;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<OutputTargetKind> ParseOutputTargetKind(
            const std::string_view value) noexcept
        {
            if (value == "Window") return OutputTargetKind::Window;
            if (value == "OffscreenTexture") return OutputTargetKind::OffscreenTexture;
            if (value == "File") return OutputTargetKind::File;
            if (value == "ReadbackBuffer") return OutputTargetKind::ReadbackBuffer;
            if (value == "PublishedArtifact") return OutputTargetKind::PublishedArtifact;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<InteractionMode> ParseInteractionMode(
            const std::string_view value) noexcept
        {
            if (value == "Interactive") return InteractionMode::Interactive;
            if (value == "Headless") return InteractionMode::Headless;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<RenderRecipeConfigState> ParseConfigState(
            const std::string_view value) noexcept
        {
            if (value == "valid" || value == "Valid") return RenderRecipeConfigState::Valid;
            if (value == "invalid" || value == "Invalid") return RenderRecipeConfigState::Invalid;
            if (value == "unsupported" || value == "Unsupported") return RenderRecipeConfigState::Unsupported;
            if (value == "stale" || value == "Stale") return RenderRecipeConfigState::Stale;
            if (value == "degraded" || value == "Degraded") return RenderRecipeConfigState::Degraded;
            if (value == "fallbackApplied" || value == "FallbackApplied")
            {
                return RenderRecipeConfigState::FallbackApplied;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsSafeConfigDomain(const BindingSourceDomain domain) noexcept
        {
            switch (domain)
            {
            case BindingSourceDomain::MeshVertex:
            case BindingSourceDomain::MeshFace:
            case BindingSourceDomain::GraphNode:
            case BindingSourceDomain::GraphEdge:
            case BindingSourceDomain::PointCloudPoint:
            case BindingSourceDomain::Scene:
                return true;
            case BindingSourceDomain::Unknown:
            case BindingSourceDomain::Generated:
            case BindingSourceDomain::Runtime:
                return false;
            }
            return false;
        }

        [[nodiscard]] const RecipeExtensionSlotDescriptor* FindRecipeSlot(
            const RenderRecipeDescriptor& recipe,
            const std::string_view stableName) noexcept
        {
            const auto it = std::find_if(recipe.Slots.begin(),
                                         recipe.Slots.end(),
                                         [stableName](const RecipeExtensionSlotDescriptor& slot) {
                                             return slot.StableName == stableName;
                                         });
            return it == recipe.Slots.end() ? nullptr : &*it;
        }

        [[nodiscard]] RecipeExtensionSlotDescriptor* FindRecipeSlot(
            RenderRecipeDescriptor& recipe,
            const std::string_view stableName) noexcept
        {
            const auto it = std::find_if(recipe.Slots.begin(),
                                         recipe.Slots.end(),
                                         [stableName](const RecipeExtensionSlotDescriptor& slot) {
                                             return slot.StableName == stableName;
                                         });
            return it == recipe.Slots.end() ? nullptr : &*it;
        }

        [[nodiscard]] BindingIntent* FindBinding(BindingSet& bindings,
                                                 const std::string_view semanticName) noexcept
        {
            const auto it = std::find_if(bindings.Intents.begin(),
                                         bindings.Intents.end(),
                                         [semanticName](const BindingIntent& intent) {
                                             return intent.SemanticName == semanticName;
                                         });
            return it == bindings.Intents.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool BindingRoleIsDeclaredByExtensionSlot(
            const RenderRecipeDescriptor& recipe,
            const std::string_view semanticName) noexcept
        {
            return std::any_of(recipe.Slots.begin(),
                               recipe.Slots.end(),
                               [semanticName](const RecipeExtensionSlotDescriptor& slot) {
                                   return slot.Kind == RecipeSlotKind::Extension &&
                                          ContainsString(slot.AllowedBindingRoles, semanticName);
                               });
        }

        [[nodiscard]] std::vector<std::string> ParseStringArray(
            RenderRecipeConfigLoadResult& result,
            const json& value,
            const std::string_view subject)
        {
            std::vector<std::string> values{};
            if (!value.is_array())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         std::string{subject},
                         "expected an array of strings");
                return values;
            }
            for (const json& entry : value)
            {
                if (!entry.is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             std::string{subject},
                             "array entries must be strings");
                    continue;
                }
                values.push_back(entry.get<std::string>());
            }
            return values;
        }

        [[nodiscard]] std::vector<RendererCapability> ParseCapabilityArray(
            RenderRecipeConfigLoadResult& result,
            const RendererDescriptor& renderer,
            const json& value,
            const std::string_view subject)
        {
            std::vector<RendererCapability> capabilities{};
            for (const std::string& name : ParseStringArray(result, value, subject))
            {
                const std::optional<RendererCapability> capability = ParseRendererCapability(name);
                if (!capability.has_value() ||
                    !Contains(renderer.SupportedCapabilities, *capability))
                {
                    AddError(result,
                             RenderRecipeConfigState::Unsupported,
                             RenderRecipeConfigDiagnosticCode::UnsupportedCapability,
                             name,
                             "recipe config requires a renderer capability that is not declared");
                    continue;
                }
                capabilities.push_back(*capability);
            }
            return capabilities;
        }

        void ParseDisabledExtensionSlots(
            RenderRecipeConfigLoadResult& result,
            const RendererDescriptor& renderer,
            const RenderRecipeDescriptor& baseRecipe,
            const json& value,
            std::vector<std::string>& disabledExtensionSlots)
        {
            for (const std::string& slotName :
                 ParseStringArray(result, value, "recipe.disabledExtensionSlots"))
            {
                const RecipeExtensionSlotDescriptor* baseSlot =
                    FindRecipeSlot(baseRecipe, slotName);
                if (baseSlot == nullptr ||
                    !ContainsString(renderer.DeclaredRecipeSlots, slotName))
                {
                    AddError(result,
                             RenderRecipeConfigState::Unsupported,
                             RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot,
                             slotName,
                             "disabled recipe slot is not declared by the renderer");
                    continue;
                }
                if (baseSlot->Kind == RecipeSlotKind::FixedCore)
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::FixedCoreMutation,
                             slotName,
                             "recipe configs cannot disable the fixed renderer core");
                    continue;
                }
                if (!ContainsString(disabledExtensionSlots, slotName))
                {
                    disabledExtensionSlots.push_back(slotName);
                }
            }
        }

        template <typename Parser>
        [[nodiscard]] auto ParseStringEnum(RenderRecipeConfigLoadResult& result,
                                           const json& object,
                                           const std::string_view key,
                                           Parser parser,
                                           const RenderRecipeConfigDiagnosticCode code,
                                           const std::string_view subject)
            -> decltype(parser(std::string_view{}))
        {
            using OptionalEnum = decltype(parser(std::string_view{}));
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return OptionalEnum{};
            }
            if (!value->is_string())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         code,
                         std::string{subject},
                         "expected a string enum value");
                return OptionalEnum{};
            }
            OptionalEnum parsed = parser(value->get<std::string>());
            if (!parsed.has_value())
            {
                AddError(result,
                         RenderRecipeConfigState::Unsupported,
                         code,
                         value->get<std::string>(),
                         "enum value is not supported by the recipe config schema");
            }
            return parsed;
        }

        void ParseSlotStateDiagnostics(RenderRecipeConfigLoadResult& result,
                                       const std::string_view slotName,
                                       const json& slotJson)
        {
            const json* state = FindMember(slotJson, "state");
            if (state == nullptr)
            {
                return;
            }
            if (!state->is_string())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         std::string{slotName},
                         "slot state must be a string");
                return;
            }
            const std::optional<RenderRecipeConfigState> parsedState =
                ParseConfigState(state->get<std::string>());
            if (!parsedState.has_value())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         std::string{slotName},
                         "slot state is not recognized");
                return;
            }
            switch (*parsedState)
            {
            case RenderRecipeConfigState::Stale:
                AddError(result,
                         RenderRecipeConfigState::Stale,
                         RenderRecipeConfigDiagnosticCode::StaleConfig,
                         std::string{slotName},
                         "stale recipe slots fail closed until refreshed");
                break;
            case RenderRecipeConfigState::Degraded:
                AddWarning(result,
                           RenderRecipeConfigState::Degraded,
                           RenderRecipeConfigDiagnosticCode::DegradedConfig,
                           std::string{slotName},
                           "recipe slot declares degraded operation");
                break;
            case RenderRecipeConfigState::FallbackApplied:
                AddInfo(result,
                        RenderRecipeConfigState::FallbackApplied,
                        RenderRecipeConfigDiagnosticCode::FallbackApplied,
                        std::string{slotName},
                        "recipe slot declares an applied fallback");
                break;
            case RenderRecipeConfigState::Valid:
            case RenderRecipeConfigState::Invalid:
            case RenderRecipeConfigState::Unsupported:
                break;
            }
        }

        void ParseRecipeSlot(RenderRecipeConfigLoadResult& result,
                             const RendererDescriptor& renderer,
                             const RenderRecipeDescriptor& baseRecipe,
                             RenderRecipeDescriptor& recipe,
                             const json& slotJson)
        {
            if (!IsObjectWithOnly(slotJson,
                                  {
                                      "name",
                                      "kind",
                                      "schemaId",
                                      "defaults",
                                      "requiredCapabilities",
                                      "allowedBindingRoles",
                                      "usedBindingRoles",
                                      "validationRules",
                                      "fallbackPolicy",
                                      "state",
                                      "diagnostics",
                                  }))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         "recipe.slots[]",
                         "recipe slots use unknown fields or are not objects");
                return;
            }

            const json* nameJson = FindMember(slotJson, "name");
            if (nameJson == nullptr || !nameJson->is_string() || nameJson->get<std::string>().empty())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot,
                         "recipe.slots[].name",
                         "recipe slots must name a declared extension slot");
                return;
            }
            const std::string slotName = nameJson->get<std::string>();
            const RecipeExtensionSlotDescriptor* baseSlot = FindRecipeSlot(baseRecipe, slotName);
            RecipeExtensionSlotDescriptor* targetSlot = FindRecipeSlot(recipe, slotName);
            if (baseSlot == nullptr || targetSlot == nullptr ||
                !ContainsString(renderer.DeclaredRecipeSlots, slotName))
            {
                AddError(result,
                         RenderRecipeConfigState::Unsupported,
                         RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot,
                         slotName,
                         "recipe config references a slot not declared by the renderer");
                return;
            }
            if (baseSlot->Kind == RecipeSlotKind::FixedCore)
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::FixedCoreMutation,
                         slotName,
                         "recipe configs cannot replace or edit the fixed renderer core");
                return;
            }
            if (const json* kind = FindMember(slotJson, "kind");
                kind != nullptr)
            {
                if (!kind->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::FixedCoreMutation,
                             slotName,
                             "recipe slot kind must be Extension when present");
                    return;
                }
                const std::string kindValue = kind->get<std::string>();
                if (kindValue != "Extension")
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::FixedCoreMutation,
                             slotName,
                             "recipe configs cannot declare fixed-core slots");
                    return;
                }
            }

            RecipeExtensionSlotDescriptor parsed = *baseSlot;
            if (const json* schemaId = FindMember(slotJson, "schemaId");
                schemaId != nullptr)
            {
                if (!schemaId->is_string() || schemaId->get<std::string>().empty())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             slotName,
                             "slot schemaId must be a non-empty string");
                }
                else
                {
                    parsed.SchemaId = schemaId->get<std::string>();
                }
            }
            if (const json* defaults = FindMember(slotJson, "defaults");
                defaults != nullptr)
            {
                if (!defaults->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidDefaults,
                             slotName,
                             "slot defaults must be a string summary, not executable data");
                }
                else
                {
                    parsed.Defaults = defaults->get<std::string>();
                }
            }
            if (const json* requiredCapabilities = FindMember(slotJson, "requiredCapabilities");
                requiredCapabilities != nullptr)
            {
                parsed.RequiredCapabilities =
                    ParseCapabilityArray(result, renderer, *requiredCapabilities, slotName);
            }
            if (const json* allowedRoles = FindMember(slotJson, "allowedBindingRoles");
                allowedRoles != nullptr)
            {
                std::vector<std::string> roles = ParseStringArray(result, *allowedRoles, slotName);
                for (const std::string& role : roles)
                {
                    if (!ContainsString(baseSlot->AllowedBindingRoles, role))
                    {
                        AddError(result,
                                 RenderRecipeConfigState::Invalid,
                                 RenderRecipeConfigDiagnosticCode::DisallowedBindingRole,
                                 role,
                                 "recipe configs cannot expand a slot outside its declared binding roles");
                    }
                }
                parsed.AllowedBindingRoles = std::move(roles);
            }
            if (const json* usedRoles = FindMember(slotJson, "usedBindingRoles");
                usedRoles != nullptr)
            {
                parsed.UsedBindingRoles = ParseStringArray(result, *usedRoles, slotName);
            }
            if (const json* rules = FindMember(slotJson, "validationRules");
                rules != nullptr)
            {
                parsed.ValidationRules = ParseStringArray(result, *rules, slotName);
            }
            if (const auto fallback = ParseStringEnum(result,
                                                      slotJson,
                                                      "fallbackPolicy",
                                                      ParseFallbackPolicy,
                                                      RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                                      slotName);
                fallback.has_value())
            {
                parsed.FallbackPolicy = *fallback;
            }
            if (const json* diagnostics = FindMember(slotJson, "diagnostics");
                diagnostics != nullptr)
            {
                parsed.Diagnostics = ParseStringArray(result, *diagnostics, slotName);
            }

            ParseSlotStateDiagnostics(result, slotName, slotJson);
            *targetSlot = std::move(parsed);
            ++result.Preview.ParsedSlotCount;
        }

        void ParseRecipe(RenderRecipeConfigLoadResult& result,
                         const RendererDescriptor& renderer,
                         const RenderRecipeDescriptor& baseRecipe,
                         RenderRecipeDescriptor& recipe,
                         std::vector<std::string>& disabledExtensionSlots,
                         const json& recipeJson)
        {
            if (!IsObjectWithOnly(recipeJson,
                                  {"recipeId",
                                   "fixedCoreName",
                                   "slots",
                                   "disabledExtensionSlots"}))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         "recipe",
                         "recipe object uses unknown fields or is not an object");
                return;
            }
            if (const json* recipeId = FindMember(recipeJson, "recipeId");
                recipeId != nullptr)
            {
                if (!recipeId->is_string() || recipeId->get<std::string>().empty())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             "recipe.recipeId",
                             "recipeId must be a non-empty string");
                }
                else
                {
                    recipe.RecipeId = recipeId->get<std::string>();
                }
            }
            if (const json* fixedCoreName = FindMember(recipeJson, "fixedCoreName");
                fixedCoreName != nullptr)
            {
                if (!fixedCoreName->is_string() ||
                    fixedCoreName->get<std::string>() != baseRecipe.FixedCoreName)
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::FixedCoreMutation,
                             "recipe.fixedCoreName",
                             "recipe configs cannot replace the renderer fixed frame core");
                }
            }
            if (const json* slots = FindMember(recipeJson, "slots");
                slots != nullptr)
            {
                if (!slots->is_array())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             "recipe.slots",
                             "recipe slots must be an array");
                    return;
                }
                for (const json& slotJson : *slots)
                {
                    ParseRecipeSlot(result, renderer, baseRecipe, recipe, slotJson);
                }
            }
            if (const json* disabledSlots = FindMember(recipeJson, "disabledExtensionSlots");
                disabledSlots != nullptr)
            {
                ParseDisabledExtensionSlots(result,
                                            renderer,
                                            baseRecipe,
                                            *disabledSlots,
                                            disabledExtensionSlots);
            }
        }

        [[nodiscard]] ViewOutputDescriptor ParseViewOutputDescriptor(
            RenderRecipeConfigLoadResult& result,
            const json& outputJson)
        {
            ViewOutputDescriptor output{};
            if (!IsObjectWithOnly(outputJson, {"name", "kind", "format", "required"}))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                         "viewOutput.outputs[]",
                         "view outputs use unknown fields or are not objects");
                return output;
            }
            if (const json* name = FindMember(outputJson, "name");
                name != nullptr && name->is_string())
            {
                output.Name = name->get<std::string>();
            }
            else
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                         "viewOutput.outputs[].name",
                         "view outputs must carry a name");
            }
            if (const auto kind = ParseStringEnum(result,
                                                  outputJson,
                                                  "kind",
                                                  ParseRenderOutputKind,
                                                  RenderRecipeConfigDiagnosticCode::UnsupportedOutput,
                                                  output.Name);
                kind.has_value())
            {
                output.Kind = *kind;
            }
            if (const json* format = FindMember(outputJson, "format");
                format != nullptr)
            {
                if (!format->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             output.Name,
                             "view output format must be a string");
                }
                else
                {
                    output.Format = format->get<std::string>();
                }
            }
            if (const json* required = FindMember(outputJson, "required");
                required != nullptr)
            {
                if (!required->is_boolean())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             output.Name,
                             "view output required flag must be boolean");
                }
                else
                {
                    output.Required = required->get<bool>();
                }
            }
            return output;
        }

        void ParseViewOutput(RenderRecipeConfigLoadResult& result,
                             ViewOutputRecipeDescriptor& viewOutput,
                             const json& viewJson)
        {
            if (!IsObjectWithOnly(viewJson,
                                  {
                                      "recipeId",
                                      "view",
                                      "viewport",
                                      "renderScale",
                                      "target",
                                      "captureRequested",
                                      "readbackRequested",
                                      "mode",
                                      "outputs",
                                  }))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                         "viewOutput",
                         "view/output recipe uses unknown fields or is not an object");
                return;
            }
            if (const json* recipeId = FindMember(viewJson, "recipeId");
                recipeId != nullptr)
            {
                if (!recipeId->is_string() || recipeId->get<std::string>().empty())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.recipeId",
                             "view/output recipeId must be a non-empty string");
                }
                else
                {
                    viewOutput.RecipeId = recipeId->get<std::string>();
                }
            }
            if (const auto view = ParseStringEnum(result,
                                                  viewJson,
                                                  "view",
                                                  ParseViewKind,
                                                  RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                                                  viewOutput.RecipeId);
                view.has_value())
            {
                viewOutput.View = *view;
            }
            if (const json* viewport = FindMember(viewJson, "viewport");
                viewport != nullptr)
            {
                if (!IsObjectWithOnly(*viewport, {"width", "height"}))
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.viewport",
                             "viewport must contain width and height only");
                }
                else
                {
                    const json* width = FindMember(*viewport, "width");
                    const json* height = FindMember(*viewport, "height");
                    if (width == nullptr || !width->is_number_unsigned() ||
                        height == nullptr || !height->is_number_unsigned())
                    {
                        AddError(result,
                                 RenderRecipeConfigState::Invalid,
                                 RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                                 "viewOutput.viewport",
                                 "viewport width and height must be unsigned integers");
                    }
                    else
                    {
                        viewOutput.ViewportWidth = width->get<std::uint32_t>();
                        viewOutput.ViewportHeight = height->get<std::uint32_t>();
                    }
                }
            }
            if (const json* renderScale = FindMember(viewJson, "renderScale");
                renderScale != nullptr)
            {
                if (!renderScale->is_number())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.renderScale",
                             "renderScale must be numeric");
                }
                else
                {
                    viewOutput.RenderScale = renderScale->get<float>();
                }
            }
            if (const auto target = ParseStringEnum(result,
                                                    viewJson,
                                                    "target",
                                                    ParseOutputTargetKind,
                                                    RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                                                    viewOutput.RecipeId);
                target.has_value())
            {
                viewOutput.Target = *target;
            }
            if (const json* capture = FindMember(viewJson, "captureRequested");
                capture != nullptr)
            {
                if (!capture->is_boolean())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.captureRequested",
                             "captureRequested must be boolean");
                }
                else
                {
                    viewOutput.CaptureRequested = capture->get<bool>();
                }
            }
            if (const json* readback = FindMember(viewJson, "readbackRequested");
                readback != nullptr)
            {
                if (!readback->is_boolean())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.readbackRequested",
                             "readbackRequested must be boolean");
                }
                else
                {
                    viewOutput.ReadbackRequested = readback->get<bool>();
                }
            }
            if (const auto mode = ParseStringEnum(result,
                                                  viewJson,
                                                  "mode",
                                                  ParseInteractionMode,
                                                  RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                                                  viewOutput.RecipeId);
                mode.has_value())
            {
                viewOutput.Mode = *mode;
            }
            if (const json* outputs = FindMember(viewJson, "outputs");
                outputs != nullptr)
            {
                if (!outputs->is_array())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidViewOutput,
                             "viewOutput.outputs",
                             "view outputs must be an array");
                }
                else
                {
                    viewOutput.Outputs.clear();
                    for (const json& outputJson : *outputs)
                    {
                        viewOutput.Outputs.push_back(ParseViewOutputDescriptor(result, outputJson));
                    }
                }
            }
        }

        void ParseBindingOverride(RenderRecipeConfigLoadResult& result,
                                  const RenderRecipeDescriptor& recipe,
                                  BindingSet& bindings,
                                  const json& overrideJson)
        {
            if (!IsObjectWithOnly(overrideJson,
                                  {
                                      "semanticName",
                                      "slot",
                                      "sourceDomain",
                                      "sourceIdentity",
                                      "sourceRevision",
                                      "valueType",
                                      "valueFormat",
                                      "fallbackPolicy",
                                      "colorSpace",
                                      "units",
                                      "range",
                                  }))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         "bindingOverrides[]",
                         "binding overrides use unknown fields or are not objects");
                return;
            }
            const json* nameJson = FindMember(overrideJson, "semanticName");
            if (nameJson == nullptr || !nameJson->is_string() || nameJson->get<std::string>().empty())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::UnknownBindingRole,
                         "bindingOverrides[].semanticName",
                         "binding overrides must target an existing binding semantic");
                return;
            }
            const std::string semanticName = nameJson->get<std::string>();
            BindingIntent* binding = FindBinding(bindings, semanticName);
            if (binding == nullptr)
            {
                AddError(result,
                         RenderRecipeConfigState::Unsupported,
                         RenderRecipeConfigDiagnosticCode::UnknownBindingRole,
                         semanticName,
                         "binding override targets an unknown renderer binding");
                return;
            }
            if (binding->Requirement == BindingRequirement::Required)
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::RequiredBindingOverride,
                         semanticName,
                         "loadable configs cannot override required fixed-core bindings");
                return;
            }
            if (const json* slotJson = FindMember(overrideJson, "slot");
                slotJson != nullptr)
            {
                if (!slotJson->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::DisallowedBindingRole,
                             semanticName,
                             "binding override slot must be a string");
                    return;
                }
                const RecipeExtensionSlotDescriptor* slot = FindRecipeSlot(recipe, slotJson->get<std::string>());
                if (slot == nullptr || slot->Kind != RecipeSlotKind::Extension ||
                    !ContainsString(slot->AllowedBindingRoles, semanticName))
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::DisallowedBindingRole,
                             semanticName,
                             "binding override is not allowed by the named extension slot");
                    return;
                }
            }
            else if (!BindingRoleIsDeclaredByExtensionSlot(recipe, semanticName))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::DisallowedBindingRole,
                         semanticName,
                         "binding override is not declared by any extension slot");
                return;
            }

            if (const auto sourceDomain = ParseStringEnum(result,
                                                          overrideJson,
                                                          "sourceDomain",
                                                          ParseBindingSourceDomain,
                                                          RenderRecipeConfigDiagnosticCode::UnsafeBindingDomain,
                                                          semanticName);
                sourceDomain.has_value())
            {
                if (!IsSafeConfigDomain(*sourceDomain))
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::UnsafeBindingDomain,
                             semanticName,
                             "loadable configs cannot claim runtime/generated/unknown binding ownership");
                }
                else
                {
                    binding->SourceDomain = *sourceDomain;
                }
            }
            if (const json* sourceIdentity = FindMember(overrideJson, "sourceIdentity");
                sourceIdentity != nullptr)
            {
                if (!sourceIdentity->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             semanticName,
                             "sourceIdentity must be a string");
                }
                else
                {
                    binding->SourceIdentity = sourceIdentity->get<std::string>();
                }
            }
            if (const json* sourceRevision = FindMember(overrideJson, "sourceRevision");
                sourceRevision != nullptr)
            {
                if (!sourceRevision->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             semanticName,
                             "sourceRevision must be a string");
                }
                else
                {
                    binding->SourceRevision = sourceRevision->get<std::string>();
                }
            }
            if (const auto valueType = ParseStringEnum(result,
                                                       overrideJson,
                                                       "valueType",
                                                       ParseBindingValueType,
                                                       RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                                       semanticName);
                valueType.has_value())
            {
                binding->ValueType = *valueType;
            }
            if (const json* valueFormat = FindMember(overrideJson, "valueFormat");
                valueFormat != nullptr)
            {
                if (!valueFormat->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             semanticName,
                             "valueFormat must be a string");
                }
                else
                {
                    binding->ValueFormat = valueFormat->get<std::string>();
                }
            }
            if (const auto fallback = ParseStringEnum(result,
                                                      overrideJson,
                                                      "fallbackPolicy",
                                                      ParseFallbackPolicy,
                                                      RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                                      semanticName);
                fallback.has_value())
            {
                binding->FallbackPolicy = *fallback;
            }
            if (const auto colorSpace = ParseStringEnum(result,
                                                        overrideJson,
                                                        "colorSpace",
                                                        ParseBindingColorSpace,
                                                        RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                                        semanticName);
                colorSpace.has_value())
            {
                binding->ColorSpace = *colorSpace;
            }
            if (const json* units = FindMember(overrideJson, "units");
                units != nullptr)
            {
                if (!units->is_string())
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             semanticName,
                             "units must be a string");
                }
                else
                {
                    binding->Units = units->get<std::string>();
                }
            }
            if (const json* range = FindMember(overrideJson, "range");
                range != nullptr)
            {
                if (!IsObjectWithOnly(*range, {"min", "max"}))
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             semanticName,
                             "range must contain min and max only");
                }
                else
                {
                    const json* min = FindMember(*range, "min");
                    const json* max = FindMember(*range, "max");
                    if (min == nullptr || max == nullptr || !min->is_number() || !max->is_number())
                    {
                        AddError(result,
                                 RenderRecipeConfigState::Invalid,
                                 RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                 semanticName,
                                 "range min and max must be numeric");
                    }
                    else
                    {
                        binding->HasRange = true;
                        binding->MinValue = min->get<double>();
                        binding->MaxValue = max->get<double>();
                    }
                }
            }
            ++result.Preview.ParsedBindingOverrideCount;
        }

        void ParseFallbackDiagnostics(RenderRecipeConfigLoadResult& result, const json& fallbacks)
        {
            if (!fallbacks.is_array())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         "fallbacks",
                         "fallback diagnostics must be an array");
                return;
            }
            for (const json& fallback : fallbacks)
            {
                if (!IsObjectWithOnly(fallback, {"subject", "state", "message"}))
                {
                    AddError(result,
                             RenderRecipeConfigState::Invalid,
                             RenderRecipeConfigDiagnosticCode::InvalidSchema,
                             "fallbacks[]",
                             "fallback diagnostics use unknown fields or are not objects");
                    continue;
                }
                const std::string subject =
                    JsonContains(fallback, "subject") && fallback["subject"].is_string()
                        ? fallback["subject"].get<std::string>()
                        : std::string{"fallbacks[]"};
                const std::string message =
                    JsonContains(fallback, "message") && fallback["message"].is_string()
                        ? fallback["message"].get<std::string>()
                        : std::string{"recipe config applied a declared fallback"};
                RenderRecipeConfigState state = RenderRecipeConfigState::FallbackApplied;
                if (const json* stateJson = FindMember(fallback, "state");
                    stateJson != nullptr)
                {
                    if (!stateJson->is_string())
                    {
                        AddError(result,
                                 RenderRecipeConfigState::Invalid,
                                 RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                 subject,
                                 "fallback state must be a string");
                        continue;
                    }
                    const std::optional<RenderRecipeConfigState> parsedState =
                        ParseConfigState(stateJson->get<std::string>());
                    if (!parsedState.has_value())
                    {
                        AddError(result,
                                 RenderRecipeConfigState::Invalid,
                                 RenderRecipeConfigDiagnosticCode::InvalidSchema,
                                 subject,
                                 "fallback state is not recognized");
                        continue;
                    }
                    state = *parsedState;
                }
                if (state == RenderRecipeConfigState::Degraded)
                {
                    AddWarning(result,
                               state,
                               RenderRecipeConfigDiagnosticCode::DegradedConfig,
                               subject,
                               message);
                }
                else if (state == RenderRecipeConfigState::Stale)
                {
                    AddError(result,
                             state,
                             RenderRecipeConfigDiagnosticCode::StaleConfig,
                             subject,
                             message);
                }
                else
                {
                    AddInfo(result,
                            RenderRecipeConfigState::FallbackApplied,
                            RenderRecipeConfigDiagnosticCode::FallbackApplied,
                            subject,
                            message);
                }
            }
        }

        [[nodiscard]] RenderRecipeConfigState DeriveState(const RenderRecipeConfigLoadResult& result) noexcept
        {
            const bool hasUnsupportedError =
                std::any_of(result.Diagnostics.begin(),
                            result.Diagnostics.end(),
                            [](const RenderRecipeConfigDiagnostic& diagnostic) {
                                return diagnostic.Severity == RenderingContractDiagnosticSeverity::Error &&
                                       diagnostic.State == RenderRecipeConfigState::Unsupported;
                            });
            if (hasUnsupportedError)
            {
                return RenderRecipeConfigState::Unsupported;
            }
            const bool hasStaleError =
                std::any_of(result.Diagnostics.begin(),
                            result.Diagnostics.end(),
                            [](const RenderRecipeConfigDiagnostic& diagnostic) {
                                return diagnostic.Severity == RenderingContractDiagnosticSeverity::Error &&
                                       diagnostic.State == RenderRecipeConfigState::Stale;
                            });
            if (hasStaleError)
            {
                return RenderRecipeConfigState::Stale;
            }
            if (HasErrors(result))
            {
                return RenderRecipeConfigState::Invalid;
            }
            if (std::any_of(result.Diagnostics.begin(),
                            result.Diagnostics.end(),
                            [](const RenderRecipeConfigDiagnostic& diagnostic) {
                                return diagnostic.State == RenderRecipeConfigState::Degraded;
                            }))
            {
                return RenderRecipeConfigState::Degraded;
            }
            if (std::any_of(result.Diagnostics.begin(),
                            result.Diagnostics.end(),
                            [](const RenderRecipeConfigDiagnostic& diagnostic) {
                                return diagnostic.State == RenderRecipeConfigState::FallbackApplied;
                            }))
            {
                return RenderRecipeConfigState::FallbackApplied;
            }
            return RenderRecipeConfigState::Valid;
        }

        void ValidatePreview(RenderRecipeConfigLoadResult& result,
                             const RenderRecipeConfigContext& context)
        {
            result.ContractDiagnostics = MergeDiagnostics({
                ValidateRenderRecipeDescriptor(context.Renderer, result.Preview.Recipe),
                ValidateViewOutputRecipe(context.Renderer, result.Preview.ViewOutput),
                ValidateBindingSet(context.Renderer, result.Preview.Bindings),
            });
            if (Extrinsic::Graphics::HasErrors(result.ContractDiagnostics))
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::ContractValidationFailed,
                         "rendering-contract",
                         "loaded recipe config does not satisfy the rendering contract");
            }
        }
    }

    [[nodiscard]] std::string_view ToString(const RenderRecipeConfigState value) noexcept
    {
        switch (value)
        {
        case RenderRecipeConfigState::Valid: return "Valid";
        case RenderRecipeConfigState::Invalid: return "Invalid";
        case RenderRecipeConfigState::Unsupported: return "Unsupported";
        case RenderRecipeConfigState::Stale: return "Stale";
        case RenderRecipeConfigState::Degraded: return "Degraded";
        case RenderRecipeConfigState::FallbackApplied: return "FallbackApplied";
        }
        return "Invalid";
    }

    [[nodiscard]] std::string_view ToString(const RenderRecipeConfigDiagnosticCode value) noexcept
    {
        switch (value)
        {
        case RenderRecipeConfigDiagnosticCode::None: return "None";
        case RenderRecipeConfigDiagnosticCode::EmptyDocument: return "EmptyDocument";
        case RenderRecipeConfigDiagnosticCode::LoadError: return "LoadError";
        case RenderRecipeConfigDiagnosticCode::ParseError: return "ParseError";
        case RenderRecipeConfigDiagnosticCode::InvalidSchema: return "InvalidSchema";
        case RenderRecipeConfigDiagnosticCode::UnsupportedVersion: return "UnsupportedVersion";
        case RenderRecipeConfigDiagnosticCode::RendererMismatch: return "RendererMismatch";
        case RenderRecipeConfigDiagnosticCode::FixedCoreMutation: return "FixedCoreMutation";
        case RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot: return "UnknownRecipeSlot";
        case RenderRecipeConfigDiagnosticCode::UnsupportedCapability: return "UnsupportedCapability";
        case RenderRecipeConfigDiagnosticCode::UnsafeBindingDomain: return "UnsafeBindingDomain";
        case RenderRecipeConfigDiagnosticCode::UnknownBindingRole: return "UnknownBindingRole";
        case RenderRecipeConfigDiagnosticCode::RequiredBindingOverride: return "RequiredBindingOverride";
        case RenderRecipeConfigDiagnosticCode::DisallowedBindingRole: return "DisallowedBindingRole";
        case RenderRecipeConfigDiagnosticCode::InvalidDefaults: return "InvalidDefaults";
        case RenderRecipeConfigDiagnosticCode::InvalidViewOutput: return "InvalidViewOutput";
        case RenderRecipeConfigDiagnosticCode::UnsupportedOutput: return "UnsupportedOutput";
        case RenderRecipeConfigDiagnosticCode::FallbackApplied: return "FallbackApplied";
        case RenderRecipeConfigDiagnosticCode::StaleConfig: return "StaleConfig";
        case RenderRecipeConfigDiagnosticCode::DegradedConfig: return "DegradedConfig";
        case RenderRecipeConfigDiagnosticCode::ContractValidationFailed: return "ContractValidationFailed";
        }
        return "None";
    }

    [[nodiscard]] bool HasErrors(const RenderRecipeConfigLoadResult& result) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [](const RenderRecipeConfigDiagnostic& diagnostic) {
                               return diagnostic.Severity == RenderingContractDiagnosticSeverity::Error;
                           });
    }

    [[nodiscard]] bool IsConfigUsable(const RenderRecipeConfigLoadResult& result) noexcept
    {
        return !HasErrors(result);
    }

    [[nodiscard]] bool HasDiagnostic(const RenderRecipeConfigLoadResult& result,
                                     const RenderRecipeConfigDiagnosticCode code) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [code](const RenderRecipeConfigDiagnostic& diagnostic) {
                               return diagnostic.Code == code;
                           });
    }

    [[nodiscard]] std::uint32_t CountByState(const RenderRecipeConfigLoadResult& result,
                                             const RenderRecipeConfigState state) noexcept
    {
        return static_cast<std::uint32_t>(
            std::count_if(result.Diagnostics.begin(),
                          result.Diagnostics.end(),
                          [state](const RenderRecipeConfigDiagnostic& diagnostic) {
                              return diagnostic.State == state;
                          }));
    }

    [[nodiscard]] RenderRecipeConfigLoadResult PreviewRenderRecipeConfig(
        const std::string_view document,
        const RenderRecipeConfigContext& context,
        const RenderRecipeConfigParseOptions& options)
    {
        RenderRecipeConfigLoadResult result{
            .SourceId = options.SourceId.empty() ? std::string{"<memory>"} : options.SourceId,
            .Preview = RenderRecipeConfigPreview{
                .Recipe = context.BaseRecipe,
                .ViewOutput = context.BaseViewOutput,
                .Bindings = context.BaseBindings,
            },
        };

        if (document.empty())
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::EmptyDocument,
                     result.SourceId,
                     "recipe config document is empty");
            result.State = DeriveState(result);
            return result;
        }

        const json root = json::parse(document.begin(), document.end(), nullptr, false);
        if (root.is_discarded())
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::ParseError,
                     result.SourceId,
                     "recipe config document is not valid JSON");
            result.State = DeriveState(result);
            return result;
        }
        if (!IsObjectWithOnly(root,
                              {
                                  "schema",
                                  "version",
                                  "rendererId",
                                  "revision",
                                  "stale",
                                  "degraded",
                                  "recipe",
                                  "viewOutput",
                                  "bindingOverrides",
                                  "fallbacks",
                                  "metadata",
                              }))
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::InvalidSchema,
                     result.SourceId,
                     "recipe config root uses unknown fields or is not an object");
            result.State = DeriveState(result);
            return result;
        }

        const json* schema = FindMember(root, "schema");
        if (schema == nullptr || !schema->is_string() ||
            schema->get<std::string>() != kRenderRecipeConfigSchemaId)
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::InvalidSchema,
                     result.SourceId,
                     "recipe config schema id is missing or unsupported");
        }

        const json* version = FindMember(root, "version");
        if (version == nullptr || !version->is_number_unsigned())
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::InvalidSchema,
                     result.SourceId,
                     "recipe config version must be an unsigned integer");
        }
        else
        {
            result.SchemaVersion = version->get<std::uint32_t>();
            if (result.SchemaVersion != kRenderRecipeConfigSchemaVersion)
            {
                AddError(result,
                         RenderRecipeConfigState::Unsupported,
                         RenderRecipeConfigDiagnosticCode::UnsupportedVersion,
                         std::to_string(result.SchemaVersion),
                         "recipe config version is not supported by this loader");
            }
        }

        const json* rendererId = FindMember(root, "rendererId");
        if (rendererId == nullptr || !rendererId->is_string() || rendererId->get<std::string>().empty())
        {
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::RendererMismatch,
                     result.SourceId,
                     "recipe config must name the target renderer");
        }
        else
        {
            result.RendererId = rendererId->get<std::string>();
            if (result.RendererId != context.Renderer.Id)
            {
                AddError(result,
                         RenderRecipeConfigState::Unsupported,
                         RenderRecipeConfigDiagnosticCode::RendererMismatch,
                         result.RendererId,
                         "recipe config renderer id does not match the validation context");
            }
        }

        if (const json* stale = FindMember(root, "stale");
            stale != nullptr)
        {
            if (!stale->is_boolean())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         result.SourceId,
                         "stale flag must be boolean");
            }
            else if (stale->get<bool>() && !options.AllowStaleConfig)
            {
                AddError(result,
                         RenderRecipeConfigState::Stale,
                         RenderRecipeConfigDiagnosticCode::StaleConfig,
                         result.SourceId,
                         "stale recipe configs fail closed unless explicitly allowed for inspection");
            }
            else if (stale->get<bool>())
            {
                AddWarning(result,
                           RenderRecipeConfigState::Stale,
                           RenderRecipeConfigDiagnosticCode::StaleConfig,
                           result.SourceId,
                           "stale recipe config is loaded for inspection only");
            }
        }
        if (const json* degraded = FindMember(root, "degraded");
            degraded != nullptr)
        {
            if (!degraded->is_boolean())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         result.SourceId,
                         "degraded flag must be boolean");
            }
            else if (degraded->get<bool>())
            {
                AddWarning(result,
                           RenderRecipeConfigState::Degraded,
                           RenderRecipeConfigDiagnosticCode::DegradedConfig,
                           result.SourceId,
                           "recipe config declares degraded operation");
            }
        }

        if (const json* recipe = FindMember(root, "recipe");
            recipe != nullptr)
        {
            ParseRecipe(result,
                        context.Renderer,
                        context.BaseRecipe,
                        result.Preview.Recipe,
                        result.Preview.DisabledExtensionSlots,
                        *recipe);
        }
        if (const json* viewOutput = FindMember(root, "viewOutput");
            viewOutput != nullptr)
        {
            ParseViewOutput(result, result.Preview.ViewOutput, *viewOutput);
        }
        if (const json* bindingOverrides = FindMember(root, "bindingOverrides");
            bindingOverrides != nullptr)
        {
            if (!bindingOverrides->is_array())
            {
                AddError(result,
                         RenderRecipeConfigState::Invalid,
                         RenderRecipeConfigDiagnosticCode::InvalidSchema,
                         "bindingOverrides",
                         "bindingOverrides must be an array");
            }
            else
            {
                for (const json& overrideJson : *bindingOverrides)
                {
                    ParseBindingOverride(result,
                                         result.Preview.Recipe,
                                         result.Preview.Bindings,
                                         overrideJson);
                }
            }
        }
        if (const json* fallbacks = FindMember(root, "fallbacks");
            fallbacks != nullptr)
        {
            ParseFallbackDiagnostics(result, *fallbacks);
        }

        ValidatePreview(result, context);
        result.State = DeriveState(result);
        return result;
    }

    [[nodiscard]] RenderRecipeConfigLoadResult LoadRenderRecipeConfigFile(
        const std::string_view path,
        const RenderRecipeConfigContext& context,
        const RenderRecipeConfigParseOptions& options)
    {
        if (path.empty())
        {
            RenderRecipeConfigLoadResult result{
                .SourceId = options.SourceId.empty() ? std::string{"<empty-path>"} : options.SourceId,
                .Preview = RenderRecipeConfigPreview{
                    .Recipe = context.BaseRecipe,
                    .ViewOutput = context.BaseViewOutput,
                    .Bindings = context.BaseBindings,
                },
            };
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::EmptyDocument,
                     result.SourceId,
                     "recipe config file path is empty");
            result.State = DeriveState(result);
            return result;
        }

        std::ifstream stream{std::string{path}};
        if (!stream)
        {
            RenderRecipeConfigLoadResult result{
                .SourceId = options.SourceId.empty() ? std::string{path} : options.SourceId,
                .Preview = RenderRecipeConfigPreview{
                    .Recipe = context.BaseRecipe,
                    .ViewOutput = context.BaseViewOutput,
                    .Bindings = context.BaseBindings,
                },
            };
            AddError(result,
                     RenderRecipeConfigState::Invalid,
                     RenderRecipeConfigDiagnosticCode::LoadError,
                     result.SourceId,
                     "recipe config file could not be opened");
            result.State = DeriveState(result);
            return result;
        }

        const std::string contents{std::istreambuf_iterator<char>{stream},
                                   std::istreambuf_iterator<char>{}};
        RenderRecipeConfigParseOptions parseOptions = options;
        if (parseOptions.SourceId.empty() || parseOptions.SourceId == "<memory>")
        {
            parseOptions.SourceId = std::string{path};
        }
        return PreviewRenderRecipeConfig(contents, context, parseOptions);
    }
}
