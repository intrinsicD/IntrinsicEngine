module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.PhysicsModule;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.System.TransformHierarchy;
import Extrinsic.Physics.World;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using json = nlohmann::json;
        namespace Collider = ECS::Components::Collider;
        namespace RigidBody = ECS::Components::RigidBody;
        namespace Transform = ECS::Components::Transform;
        namespace Physics = Extrinsic::Physics;
        namespace EcsComponents = ECS::Components;

        constexpr float kMinimumFixedDeltaSeconds = 1.0e-6f;
        constexpr float kMaximumFixedDeltaSeconds = 1.0f;
        constexpr float kMaximumAccumulatedSeconds = 10.0f;
        constexpr float kMaximumGravityMagnitude = 100'000.0f;
        constexpr std::uint32_t kMaximumStepsPerFrame = 1'024u;

        constexpr ConfigFieldSpec kGravityItem{
            .Type = ConfigFieldType::Float,
            .Min = -kMaximumGravityMagnitude,
            .Max = kMaximumGravityMagnitude,
        };
        constexpr std::array<ConfigFieldSpec, 5u> kPhysicsConfigFields{{
            {.Name = "enabled", .Type = ConfigFieldType::Bool,
             .Description = "Enable fixed-step physics simulation."},
            {.Name = "fixed_delta_seconds", .Type = ConfigFieldType::Float,
             .Description = "Duration of each physics step in seconds.",
             .Min = kMinimumFixedDeltaSeconds, .Max = kMaximumFixedDeltaSeconds},
            {.Name = "max_accumulated_seconds", .Type = ConfigFieldType::Float,
             .Description = "Maximum accumulated simulation time; raised to one fixed step when smaller.",
             .Min = kMinimumFixedDeltaSeconds, .Max = kMaximumAccumulatedSeconds},
            {.Name = "max_steps_per_frame", .Type = ConfigFieldType::UInt,
             .Description = "Maximum physics steps processed in one frame.",
             .Min = 1.0, .Max = kMaximumStepsPerFrame},
            {.Name = "gravity", .Type = ConfigFieldType::Array,
             .Description = "World-space acceleration components in meters per second squared.",
             .Item = &kGravityItem, .MinItems = 3u, .MaxItems = 3u},
        }};

        [[nodiscard]] bool IsFiniteBoundedGravity(const glm::vec3 value) noexcept
        {
            return Physics::IsFinite(value) &&
                std::abs(value.x) <= kMaximumGravityMagnitude &&
                std::abs(value.y) <= kMaximumGravityMagnitude &&
                std::abs(value.z) <= kMaximumGravityMagnitude;
        }

        [[nodiscard]] bool IsValidConfig(const PhysicsModuleConfig& config) noexcept
        {
            return std::isfinite(config.FixedDeltaSeconds) &&
                config.FixedDeltaSeconds >= kMinimumFixedDeltaSeconds &&
                config.FixedDeltaSeconds <= kMaximumFixedDeltaSeconds &&
                std::isfinite(config.MaxAccumulatedSeconds) &&
                config.MaxAccumulatedSeconds >= config.FixedDeltaSeconds &&
                config.MaxAccumulatedSeconds <= kMaximumAccumulatedSeconds &&
                config.MaxStepsPerFrame > 0u &&
                config.MaxStepsPerFrame <= kMaximumStepsPerFrame &&
                IsFiniteBoundedGravity(config.Gravity);
        }

        [[nodiscard]] bool ConfigEquals(const PhysicsModuleConfig& lhs,
                                        const PhysicsModuleConfig& rhs) noexcept
        {
            return lhs.Enabled == rhs.Enabled &&
                lhs.FixedDeltaSeconds == rhs.FixedDeltaSeconds &&
                lhs.MaxAccumulatedSeconds == rhs.MaxAccumulatedSeconds &&
                lhs.MaxStepsPerFrame == rhs.MaxStepsPerFrame &&
                lhs.Gravity.x == rhs.Gravity.x &&
                lhs.Gravity.y == rhs.Gravity.y &&
                lhs.Gravity.z == rhs.Gravity.z;
        }

        void AddConfigWarning(
            Core::Config::EngineConfigSectionValidationResult* const result,
            const Core::Config::EngineConfigDiagnosticCode code,
            std::string subject,
            std::string message)
        {
            if (result == nullptr)
                return;
            result->State = Core::Config::EngineConfigState::FallbackApplied;
            result->Diagnostics.push_back(Core::Config::EngineConfigDiagnostic{
                .State = Core::Config::EngineConfigState::FallbackApplied,
                .Severity = Core::Config::EngineConfigDiagnosticSeverity::Warning,
                .Code = code,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        [[nodiscard]] std::string ConfigFieldSubject(
            const std::string_view subject,
            const std::string_view field)
        {
            if (subject.empty())
                return std::string{field};
            return std::string{subject} + "." + std::string{field};
        }

        [[nodiscard]] PhysicsModuleConfig ParsePhysicsModuleConfig(
            const std::string_view payload,
            const PhysicsModuleConfig& reference,
            Core::Config::EngineConfigSectionValidationResult* const result,
            const std::string_view subject)
        {
            PhysicsModuleConfig config = reference;
            const json object = json::parse(payload, nullptr, false);
            if (object.is_discarded())
            {
                AddConfigWarning(
                    result,
                    Core::Config::EngineConfigDiagnosticCode::ParseError,
                    std::string{subject},
                    "Invalid JSON payload; reference physics config retained.");
                return config;
            }
            if (!object.is_object())
            {
                AddConfigWarning(
                    result,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    std::string{subject},
                    "Expected a JSON object; reference physics config retained.");
                return config;
            }

            for (const auto& [key, value] : object.items())
            {
                (void)value;
                if (FindConfigFieldSpec(kPhysicsConfigFields, key) == nullptr)
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::UnknownField,
                        ConfigFieldSubject(subject, key),
                        "Unknown physics config field; reference defaults remain authoritative.");
                }
            }

            const auto countParsed = [result]()
            {
                if (result != nullptr)
                    ++result->ParsedFieldCount;
            };
            const auto find = [&object](const std::string_view key) -> const json*
            {
                const auto it = object.find(std::string{key});
                return it == object.end() ? nullptr : &*it;
            };
            const auto readNumber = [&](const std::string_view key)
                -> std::optional<double>
            {
                const json* const value = find(key);
                if (value == nullptr)
                    return std::nullopt;
                if (!value->is_number())
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        ConfigFieldSubject(subject, key),
                        "Expected a numeric value; reference value retained.");
                    return std::nullopt;
                }
                const double number = value->get<double>();
                if (!AcceptsConfigFieldNumber(*FindConfigFieldSpec(kPhysicsConfigFields, key), number))
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        ConfigFieldSubject(subject, key),
                        "Numeric value is outside the supported range; reference value retained.");
                    return std::nullopt;
                }
                countParsed();
                return number;
            };

            if (const json* const enabled = find("enabled"); enabled != nullptr)
            {
                if (enabled->is_boolean())
                {
                    config.Enabled = enabled->get<bool>();
                    countParsed();
                }
                else
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        ConfigFieldSubject(subject, "enabled"),
                        "Expected a boolean value; reference value retained.");
                }
            }

            if (const std::optional<double> value = readNumber("fixed_delta_seconds"))
            {
                config.FixedDeltaSeconds = static_cast<float>(*value);
            }
            if (const std::optional<double> value = readNumber("max_accumulated_seconds"))
            {
                config.MaxAccumulatedSeconds = static_cast<float>(*value);
            }

            if (const json* const steps = find("max_steps_per_frame");
                steps != nullptr)
            {
                std::optional<std::uint64_t> parsed{};
                if (steps->is_number_unsigned())
                {
                    parsed = steps->get<std::uint64_t>();
                }
                else if (steps->is_number_integer())
                {
                    const std::int64_t signedValue = steps->get<std::int64_t>();
                    if (signedValue >= 0)
                        parsed = static_cast<std::uint64_t>(signedValue);
                }
                if (!parsed.has_value() || !AcceptsConfigFieldNumber(
                        *FindConfigFieldSpec(kPhysicsConfigFields, "max_steps_per_frame"),
                        static_cast<double>(*parsed)))
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        ConfigFieldSubject(subject, "max_steps_per_frame"),
                        "Expected an integer in [1, 1024]; reference value retained.");
                }
                else
                {
                    config.MaxStepsPerFrame = static_cast<std::uint32_t>(*parsed);
                    countParsed();
                }
            }

            if (const json* const gravity = find("gravity"); gravity != nullptr)
            {
                const auto& field = *FindConfigFieldSpec(kPhysicsConfigFields, "gravity");
                bool valid = gravity->is_array() && gravity->size() >= *field.MinItems &&
                    gravity->size() <= *field.MaxItems;
                glm::vec3 parsed{0.0f};
                if (valid)
                {
                    for (std::size_t index = 0u; index < gravity->size(); ++index)
                    {
                        if (!(*gravity)[index].is_number())
                        {
                            valid = false;
                            break;
                        }
                        const double component = (*gravity)[index].get<double>();
                        if (!AcceptsConfigFieldNumber(*field.Item, component))
                        {
                            valid = false;
                            break;
                        }
                        parsed[static_cast<glm::length_t>(index)] =
                            static_cast<float>(component);
                    }
                }
                if (valid)
                {
                    config.Gravity = parsed;
                    countParsed();
                }
                else
                {
                    AddConfigWarning(
                        result,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        ConfigFieldSubject(subject, "gravity"),
                        "Expected three finite gravity components in the supported range; reference value retained.");
                }
            }

            if (config.MaxAccumulatedSeconds < config.FixedDeltaSeconds)
            {
                AddConfigWarning(
                    result,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    ConfigFieldSubject(subject, "max_accumulated_seconds"),
                    "Accumulation limit must cover at least one fixed step; the smallest valid limit was applied.");
                config.MaxAccumulatedSeconds = config.FixedDeltaSeconds;
            }
            return config;
        }

        [[nodiscard]] Physics::MotionType ConvertMotion(
            const RigidBody::MotionType motion) noexcept
        {
            switch (motion)
            {
            case RigidBody::MotionType::Static:
                return Physics::MotionType::Static;
            case RigidBody::MotionType::Kinematic:
                return Physics::MotionType::Kinematic;
            case RigidBody::MotionType::Dynamic:
                return Physics::MotionType::Dynamic;
            }
            return Physics::MotionType::Static;
        }

        [[nodiscard]] Physics::Transform ConvertTransform(
            const Transform::Component& transform) noexcept
        {
            return Physics::Transform{
                .Position = transform.Position,
                .Rotation = transform.Rotation,
                .Scale = transform.Scale,
            };
        }

        [[nodiscard]] Physics::Transform ConvertLocalPose(
            const Collider::LocalPose& pose) noexcept
        {
            return Physics::Transform{
                .Position = pose.Position,
                .Rotation = pose.Rotation,
            };
        }

        [[nodiscard]] Physics::ShapeDescriptor ConvertShape(
            const Collider::ShapeDescriptor& shape)
        {
            Physics::ShapeDescriptor result{};
            result.Enabled = shape.Enabled;
            result.IsTrigger = shape.IsTrigger;
            result.Local = ConvertLocalPose(shape.Local);
            if (const auto* const sphere =
                    std::get_if<Collider::SphereShape>(&shape.Shape))
            {
                result.Kind = Physics::ShapeKind::Sphere;
                result.Radius = sphere->Radius;
            }
            else if (const auto* const capsule =
                         std::get_if<Collider::CapsuleShape>(&shape.Shape))
            {
                result.Kind = Physics::ShapeKind::Capsule;
                result.Radius = capsule->Radius;
                result.CapsuleHalfHeight = capsule->HalfHeight;
            }
            else
            {
                result.Kind = Physics::ShapeKind::Box;
                result.HalfExtents =
                    std::get<Collider::BoxShape>(shape.Shape).HalfExtents;
            }
            return result;
        }

        void HashWord(std::uint64_t& hash, const std::uint64_t word) noexcept
        {
            constexpr std::uint64_t kPrime = 1'099'511'628'211ull;
            for (std::uint32_t shift = 0u; shift < 64u; shift += 8u)
            {
                hash ^= (word >> shift) & 0xFFu;
                hash *= kPrime;
            }
        }

        void HashFloat(std::uint64_t& hash, const float value) noexcept
        {
            HashWord(hash, std::bit_cast<std::uint32_t>(value));
        }

        void HashVec3(std::uint64_t& hash, const glm::vec3 value) noexcept
        {
            HashFloat(hash, value.x);
            HashFloat(hash, value.y);
            HashFloat(hash, value.z);
        }

        void HashQuat(std::uint64_t& hash, const glm::quat value) noexcept
        {
            HashFloat(hash, value.w);
            HashFloat(hash, value.x);
            HashFloat(hash, value.y);
            HashFloat(hash, value.z);
        }

        void HashTransform(std::uint64_t& hash,
                           const Physics::Transform& value) noexcept
        {
            HashVec3(hash, value.Position);
            HashQuat(hash, value.Rotation);
            HashVec3(hash, value.Scale);
        }

        [[nodiscard]] std::uint64_t Fingerprint(
            const Physics::BodyDescriptor& descriptor) noexcept
        {
            std::uint64_t hash = 14'695'981'039'346'656'037ull;
            HashWord(hash, static_cast<std::uint64_t>(descriptor.Motion));
            HashTransform(hash, descriptor.Pose);
            HashVec3(hash, descriptor.LinearVelocity);
            HashVec3(hash, descriptor.AngularVelocity);
            HashFloat(hash, descriptor.Mass);
            HashFloat(hash, descriptor.LinearDamping);
            HashFloat(hash, descriptor.AngularDamping);
            HashFloat(hash, descriptor.GravityScale);
            HashWord(hash, descriptor.Enabled ? 1u : 0u);
            HashWord(hash, descriptor.ParticipatesInContacts ? 1u : 0u);
            HashWord(hash, descriptor.Shapes.size());
            for (const Physics::ShapeDescriptor& shape : descriptor.Shapes)
            {
                HashWord(hash, static_cast<std::uint64_t>(shape.Kind));
                HashTransform(hash, shape.Local);
                HashVec3(hash, shape.HalfExtents);
                HashFloat(hash, shape.Radius);
                HashFloat(hash, shape.CapsuleHalfHeight);
                HashWord(hash, shape.IsTrigger ? 1u : 0u);
                HashWord(hash, shape.Enabled ? 1u : 0u);
            }
            return hash;
        }

        [[nodiscard]] std::optional<glm::mat4> ParentWorldMatrix(
            const entt::registry& raw, const ECS::EntityHandle entity)
        {
            const auto* hierarchy = raw.try_get<EcsComponents::Hierarchy::Component>(entity);
            return ECS::Systems::TransformHierarchy::TryComposeWorldMatrix(
                raw, hierarchy ? hierarchy->Parent : entt::null);
        }

        [[nodiscard]] bool PoseEquals(const Transform::Component& transform,
                                      const Physics::Transform& pose) noexcept
        {
            return transform.Position.x == pose.Position.x &&
                transform.Position.y == pose.Position.y &&
                transform.Position.z == pose.Position.z &&
                transform.Rotation.w == pose.Rotation.w &&
                transform.Rotation.x == pose.Rotation.x &&
                transform.Rotation.y == pose.Rotation.y &&
                transform.Rotation.z == pose.Rotation.z &&
                transform.Scale == pose.Scale;
        }
    }

    std::string SerializePhysicsModuleConfig(const PhysicsModuleConfig& config)
    {
        return json::object({
            {"enabled", config.Enabled},
            {"fixed_delta_seconds", config.FixedDeltaSeconds},
            {"max_accumulated_seconds", config.MaxAccumulatedSeconds},
            {"max_steps_per_frame", config.MaxStepsPerFrame},
            {"gravity", json::array({config.Gravity.x,
                                      config.Gravity.y,
                                      config.Gravity.z})},
        }).dump();
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidatePhysicsModuleConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const PhysicsModuleConfig reference = ParsePhysicsModuleConfig(
            referencePayloadJson,
            PhysicsModuleConfig{},
            nullptr,
            {});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const PhysicsModuleConfig config = ParsePhysicsModuleConfig(
            documentPayloadJson,
            reference,
            &result,
            diagnosticSubject);
        result.CanonicalPayloadJson = SerializePhysicsModuleConfig(config);
        return result;
    }

    std::optional<PhysicsModuleConfig> GetPhysicsModuleConfig(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* const section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kPhysicsModuleConfigSectionName);
        if (section == nullptr ||
            section->SchemaId != kPhysicsModuleConfigSectionSchemaId ||
            section->SchemaVersion != kPhysicsModuleConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated = ValidatePhysicsModuleConfigSection(
            section->PayloadJson,
            SerializePhysicsModuleConfig(PhysicsModuleConfig{}),
            kPhysicsModuleConfigSectionName);
        if (!validated.Usable())
            return std::nullopt;
        const PhysicsModuleConfig decoded = ParsePhysicsModuleConfig(
            validated.CanonicalPayloadJson,
            PhysicsModuleConfig{},
            nullptr,
            {});
        return IsValidConfig(decoded)
            ? std::optional<PhysicsModuleConfig>{decoded}
            : std::nullopt;
    }

    void SetPhysicsModuleConfig(Core::Config::EngineConfig& config,
                                const PhysicsModuleConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{kPhysicsModuleConfigSectionName},
                .SchemaId = std::string{kPhysicsModuleConfigSectionSchemaId},
                .SchemaVersion = kPhysicsModuleConfigSectionSchemaVersion,
                .PayloadJson = SerializePhysicsModuleConfig(value),
            });
    }

    Core::Config::EngineConfigSectionRegistration
    MakePhysicsModuleConfigSectionRegistration(
        PhysicsModuleConfigChangedCallback onChanged)
    {
        Core::Config::EngineConfigSectionChangedCallback callback{};
        if (onChanged)
        {
            callback = [typed = std::move(onChanged)](
                           const Core::Config::EngineConfigSection& previous,
                           const Core::Config::EngineConfigSection& current)
            {
                (void)previous;
                Core::Config::EngineConfig config{};
                config.AppSections.push_back(current);
                if (const auto value = GetPhysicsModuleConfig(config))
                    typed(*value);
            };
        }
        auto schema = json::parse(ConfigDetail::BuildSectionSchemaJson(
            kPhysicsModuleConfigSectionSchemaId, "Physics",
            "Fixed-step physics settings; invalid fields retain their reference values.",
            kPhysicsConfigFields,
            json::parse(SerializePhysicsModuleConfig(PhysicsModuleConfig{}))));
        schema["x-fallback"] = true;
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection = Core::Config::EngineConfigSection{
                .Name = std::string{kPhysicsModuleConfigSectionName},
                .SchemaId = std::string{kPhysicsModuleConfigSectionSchemaId},
                .SchemaVersion = kPhysicsModuleConfigSectionSchemaVersion,
                .PayloadJson = SerializePhysicsModuleConfig(
                    PhysicsModuleConfig{}),
            },
            .Validate = ValidatePhysicsModuleConfigSection,
            .OnChanged = std::move(callback),
            .SchemaJson = schema.dump(),
        };
    }

    struct PhysicsModule::Impl
    {
        using StableId = EcsComponents::StableId;
        using BodyHandle = Physics::BodyHandle;

        struct Binding
        {
            BodyHandle Handle{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            std::uint64_t LastSeenGeneration{0u};
            std::uint64_t AuthoringFingerprint{0u};
            glm::vec3 AuthoredLinearVelocity{0.0f};
            glm::vec3 AuthoredAngularVelocity{0.0f};
        };

        struct WorldState
        {
            using BindingMap =
                std::unordered_map<StableId,
                                   Binding,
                                   EcsComponents::StableIdHash>;

            Physics::World World{};
            BindingMap Bindings{};
            float AccumulatorSeconds{0.0f};
            std::uint64_t SyncGeneration{0u};
        };

        using WorldStateMap = std::unordered_map<
            WorldHandle,
            WorldState,
            Core::StrongHandleHash<WorldHandleTag>>;

        PhysicsModuleConfig Config{};
        PhysicsModuleDiagnostics Diagnostics{};
        WorldStateMap WorldStates{};
        WorldRegistry* Worlds{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        std::uint64_t OrderCounter{0u};
        bool Registered{false};

        [[nodiscard]] std::uint64_t NextOrder() noexcept
        {
            return ++OrderCounter;
        }

        void RefreshDerivedDiagnostics() noexcept
        {
            Diagnostics.Enabled = Registered && Config.Enabled;
            Diagnostics.OwnedPhysicsWorlds =
                static_cast<std::uint32_t>(WorldStates.size());
            std::uint64_t sidecars = 0u;
            for (const auto& [world, state] : WorldStates)
            {
                (void)world;
                sidecars += state.Bindings.size();
            }
            Diagnostics.SidecarCount = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    sidecars,
                    std::numeric_limits<std::uint32_t>::max()));
            Diagnostics.ActiveWorldBodyCount = 0u;
            Diagnostics.ActiveWorldSidecarCount = 0u;
            Diagnostics.ActiveWorldAccumulatorSeconds = 0.0f;
            if (Worlds == nullptr)
                return;
            const auto active = WorldStates.find(Worlds->ActiveWorld());
            if (active == WorldStates.end())
                return;
            Diagnostics.ActiveWorldBodyCount = static_cast<std::uint32_t>(
                active->second.World.BodyCount());
            Diagnostics.ActiveWorldSidecarCount = static_cast<std::uint32_t>(
                active->second.Bindings.size());
            Diagnostics.ActiveWorldAccumulatorSeconds =
                active->second.AccumulatorSeconds;
        }

        void ClearWorldStates() noexcept
        {
            Diagnostics.WorldStatesRemoved += WorldStates.size();
            WorldStates.clear();
            Diagnostics.LastProcessedWorld = {};
            RefreshDerivedDiagnostics();
        }

        void RemoveWorldState(const WorldHandle world) noexcept
        {
            if (WorldStates.erase(world) > 0u)
                ++Diagnostics.WorldStatesRemoved;
            if (Diagnostics.LastProcessedWorld == world)
                Diagnostics.LastProcessedWorld = {};
            RefreshDerivedDiagnostics();
        }

        [[nodiscard]] bool RejectUnsupported(PhysicsUnsupportedReason reason, bool recordDiagnostics)
        {
            if (recordDiagnostics)
            {
                ++Diagnostics.UnsupportedAuthoring;
                Diagnostics.LastUnsupportedReason = reason;
            }
            return false;
        }

        [[nodiscard]] bool TryBuildDescriptor(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            Physics::BodyDescriptor& descriptor,
            const bool recordDiagnostics)
        {
            descriptor = {};
            if (!raw.all_of<Transform::Component>(entity))
            {
                if (recordDiagnostics)
                    ++Diagnostics.MissingTransforms;
                return false;
            }
            const auto world = ECS::Systems::TransformHierarchy::TryComposeWorldMatrix(raw, entity);
            Transform::Component worldTransform;
            if (!world || !Transform::TryComputeLocalTransform(*world, glm::mat4(1.0f), worldTransform))
                return RejectUnsupported(PhysicsUnsupportedReason::WorldTransform, recordDiagnostics);
            descriptor.Pose = ConvertTransform(worldTransform);

            const auto* rigidBody = raw.try_get<RigidBody::Component>(entity);
            if (rigidBody != nullptr)
            {
                if (rigidBody->Mass.Policy == RigidBody::MassPolicy::Density)
                    return RejectUnsupported(PhysicsUnsupportedReason::MassPolicy, recordDiagnostics);
                if (rigidBody->Mass.LocalCenterOfMass != glm::vec3(0.0f))
                    return RejectUnsupported(PhysicsUnsupportedReason::CenterOfMass, recordDiagnostics);
                if (!rigidBody->AllowSleep || !rigidBody->StartsAwake)
                    return RejectUnsupported(PhysicsUnsupportedReason::SleepPolicy, recordDiagnostics);
                if (rigidBody->ContinuousCollisionDetection)
                    return RejectUnsupported(PhysicsUnsupportedReason::ContinuousCollisionDetection, recordDiagnostics);
                descriptor.Motion = ConvertMotion(rigidBody->Motion);
                descriptor.LinearVelocity = rigidBody->LinearVelocity;
                descriptor.AngularVelocity = rigidBody->AngularVelocity;
                descriptor.Mass = rigidBody->Mass.Mass;
                descriptor.LinearDamping = rigidBody->LinearDamping;
                descriptor.AngularDamping = rigidBody->AngularDamping;
                descriptor.GravityScale = rigidBody->GravityScale;
                descriptor.ParticipatesInContacts = rigidBody->ParticipatesInContacts;
                if (descriptor.Motion == Physics::MotionType::Dynamic)
                {
                    const auto parent = ParentWorldMatrix(raw, entity);
                    Transform::Component parentTransform;
                    if (!parent || !Transform::TryComputeLocalTransform(*parent, glm::mat4(1.0f), parentTransform))
                        return RejectUnsupported(PhysicsUnsupportedReason::DynamicParentTransform, recordDiagnostics);
                    const auto scale = parentTransform.Scale;
                    const float largest = std::max({scale.x, scale.y, scale.z});
                    const float smallest = std::min({scale.x, scale.y, scale.z});
                    // Uniform positive parent TRS keeps future world rotations
                    // representable as local TRS (same relative tolerance as ECS).
                    if (smallest <= 0.0f || largest - smallest > 1.0e-4f * largest)
                        return RejectUnsupported(PhysicsUnsupportedReason::DynamicParentTransform, recordDiagnostics);
                }
            }
            else
            {
                descriptor.Motion = Physics::MotionType::Static;
                descriptor.Mass = 0.0f;
                descriptor.GravityScale = 0.0f;
                descriptor.ParticipatesInContacts = true;
            }

            bool omittedByFilter = false;
            if (const auto* collider = raw.try_get<Collider::Component>(entity))
            {
                descriptor.Shapes.reserve(collider->Shapes.size());
                for (const Collider::ShapeDescriptor& shape : collider->Shapes)
                {
                    if (!shape.Enabled)
                        continue;
                    if (shape.Filter.GroupIndex == 0 &&
                        (shape.Filter.CategoryBits == 0u || shape.Filter.CollidesWithBits == 0u))
                    {
                        omittedByFilter = true;
                        continue;
                    }
                    if (shape.Filter.GroupIndex != 0 || shape.Filter.CategoryBits != 1u ||
                        shape.Filter.CollidesWithBits != 0xFFFF'FFFFu)
                        return RejectUnsupported(PhysicsUnsupportedReason::CollisionFilter, recordDiagnostics);
                    if (shape.Surface.Id != 0u || shape.Surface.StaticFriction != 0.5f ||
                        shape.Surface.DynamicFriction != 0.5f || shape.Surface.Restitution != 0.0f)
                        return RejectUnsupported(PhysicsUnsupportedReason::Material, recordDiagnostics);
                    if (shape.Offsets.ContactOffset != 0.02f || shape.Offsets.RestOffset != 0.0f)
                        return RejectUnsupported(PhysicsUnsupportedReason::ContactOffsets, recordDiagnostics);
                    descriptor.Shapes.push_back(ConvertShape(shape));
                }
            }
            if (omittedByFilter && descriptor.Shapes.empty())
                descriptor.ParticipatesInContacts = false;
            const auto status = Physics::Validate(descriptor);
            if (status == Physics::ValidationStatus::UnsupportedShapeTransform)
                return RejectUnsupported(PhysicsUnsupportedReason::ShapeTransform, recordDiagnostics);
            return status == Physics::ValidationStatus::Valid;
        }

        void RemoveBinding(WorldState& state,
                           const WorldState::BindingMap::iterator it)
        {
            if (it->second.Handle.IsValid() &&
                !state.World.DestroyBody(it->second.Handle))
            {
                ++Diagnostics.StaleHandles;
            }
            state.Bindings.erase(it);
            ++Diagnostics.BodiesRemoved;
        }

        void SyncAuthoring(WorldState& state, ECS::Scene::Registry& scene)
        {
            ++Diagnostics.SyncPasses;
            Diagnostics.LastSyncOrder = NextOrder();
            ++state.SyncGeneration;

            entt::registry& raw = scene.Raw();
            std::vector<ECS::EntityHandle> entities{};
            std::unordered_set<ECS::EntityHandle> uniqueEntities{};
            for (const ECS::EntityHandle entity : raw.view<Collider::Component>())
            {
                if (uniqueEntities.insert(entity).second)
                    entities.push_back(entity);
            }
            for (const ECS::EntityHandle entity : raw.view<RigidBody::Component>())
            {
                if (uniqueEntities.insert(entity).second)
                    entities.push_back(entity);
            }
            std::sort(
                entities.begin(),
                entities.end(),
                [](const ECS::EntityHandle lhs, const ECS::EntityHandle rhs)
                {
                    return static_cast<std::uint32_t>(lhs) <
                        static_cast<std::uint32_t>(rhs);
                });

            std::unordered_set<StableId, EcsComponents::StableIdHash>
                seenStableIds{};
            for (const ECS::EntityHandle entity : entities)
            {
                const auto* const stableId = raw.try_get<StableId>(entity);
                if (stableId == nullptr || !EcsComponents::IsValid(*stableId))
                {
                    ++Diagnostics.MissingStableIds;
                    continue;
                }
                if (!seenStableIds.insert(*stableId).second)
                {
                    ++Diagnostics.DuplicateStableIds;
                    continue;
                }

                const auto* const collider =
                    raw.try_get<Collider::Component>(entity);
                const auto* const rigidBody =
                    raw.try_get<RigidBody::Component>(entity);
                const RigidBody::AuthoringCombination authoring =
                    RigidBody::ClassifyAuthoringCombination(
                        collider != nullptr,
                        rigidBody);
                if (authoring ==
                    RigidBody::AuthoringCombination::NoPhysicsAuthoring)
                {
                    continue;
                }
                if (authoring == RigidBody::AuthoringCombination::
                        MissingColliderForContactingBody)
                {
                    ++Diagnostics.MissingAuthoringComponents;
                    if (const auto existing = state.Bindings.find(*stableId);
                        existing != state.Bindings.end())
                    {
                        RemoveBinding(state, existing);
                    }
                    continue;
                }
                if ((rigidBody != nullptr &&
                     (!rigidBody->Enabled ||
                      RigidBody::Validate(*rigidBody) !=
                          RigidBody::ValidationStatus::Valid)) ||
                    (collider != nullptr &&
                     (!collider->Enabled ||
                      Collider::Validate(*collider) !=
                          Collider::ValidationStatus::Valid)))
                {
                    ++Diagnostics.InvalidDescriptors;
                    if (const auto existing = state.Bindings.find(*stableId);
                        existing != state.Bindings.end())
                    {
                        RemoveBinding(state, existing);
                    }
                    continue;
                }

                Physics::BodyDescriptor descriptor{};
                if (!TryBuildDescriptor(raw, entity, descriptor, true))
                {
                    ++Diagnostics.InvalidDescriptors;
                    if (const auto existing = state.Bindings.find(*stableId);
                        existing != state.Bindings.end())
                    {
                        RemoveBinding(state, existing);
                    }
                    continue;
                }

                const std::uint64_t fingerprint = Fingerprint(descriptor);
                const glm::vec3 authoredLinearVelocity = descriptor.LinearVelocity;
                const glm::vec3 authoredAngularVelocity = descriptor.AngularVelocity;
                auto [bindingIt, inserted] = state.Bindings.try_emplace(*stableId);
                Binding& binding = bindingIt->second;
                binding.Entity = entity;
                binding.LastSeenGeneration = state.SyncGeneration;

                if (inserted || !state.World.Contains(binding.Handle))
                {
                    if (!inserted)
                        ++Diagnostics.StaleHandles;
                    binding.Handle = state.World.AddBody(descriptor);
                    if (!binding.Handle.IsValid())
                    {
                        ++Diagnostics.InvalidDescriptors;
                        state.Bindings.erase(bindingIt);
                        continue;
                    }
                    binding.AuthoringFingerprint = fingerprint;
                    ++Diagnostics.BodiesCreated;
                }
                else if (binding.AuthoringFingerprint != fingerprint)
                {
                    const auto* current = state.World.GetBody(binding.Handle);
                    if (current != nullptr)
                    {
                        if (authoredLinearVelocity == binding.AuthoredLinearVelocity)
                            descriptor.LinearVelocity = current->LinearVelocity;
                        if (authoredAngularVelocity == binding.AuthoredAngularVelocity)
                            descriptor.AngularVelocity = current->AngularVelocity;
                    }
                    if (state.World.UpdateBody(binding.Handle, descriptor))
                    {
                        binding.AuthoringFingerprint = fingerprint;
                        ++Diagnostics.BodiesUpdated;
                    }
                    else
                    {
                        ++Diagnostics.StaleHandles;
                        binding.Handle = state.World.AddBody(descriptor);
                        if (!binding.Handle.IsValid())
                        {
                            ++Diagnostics.InvalidDescriptors;
                            state.Bindings.erase(bindingIt);
                            continue;
                        }
                        binding.AuthoringFingerprint = fingerprint;
                        ++Diagnostics.BodiesCreated;
                    }
                }
                binding.AuthoredLinearVelocity = authoredLinearVelocity;
                binding.AuthoredAngularVelocity = authoredAngularVelocity;
            }

            for (auto it = state.Bindings.begin(); it != state.Bindings.end();)
            {
                if (it->second.LastSeenGeneration == state.SyncGeneration)
                {
                    ++it;
                    continue;
                }
                const auto remove = it++;
                RemoveBinding(state, remove);
            }
        }

        void WritebackDynamicTransforms(WorldState& state,
                                        ECS::Scene::Registry& scene)
        {
            Diagnostics.LastWritebackOrder = NextOrder();
            entt::registry& raw = scene.Raw();
            std::vector<std::pair<std::size_t, Binding*>> bindings;
            bindings.reserve(state.Bindings.size());
            for (auto& [id, binding] : state.Bindings)
            {
                (void)id;
                std::size_t depth = 0;
                auto entity = binding.Entity;
                while (raw.valid(entity) && depth < 4096)
                {
                    const auto* hierarchy = raw.try_get<EcsComponents::Hierarchy::Component>(entity);
                    if (hierarchy == nullptr || hierarchy->Parent == entt::null)
                        break;
                    entity = hierarchy->Parent;
                    ++depth;
                }
                bindings.emplace_back(depth, &binding);
            }
            std::sort(bindings.begin(), bindings.end(), [](const auto& lhs, const auto& rhs)
            {
                if (lhs.first != rhs.first)
                    return lhs.first < rhs.first;
                return static_cast<std::uint32_t>(lhs.second->Entity) <
                    static_cast<std::uint32_t>(rhs.second->Entity);
            });

            for (const auto& [depth, binding] : bindings)
            {
                (void)depth;
                const Physics::BodyDescriptor* const body =
                    state.World.GetBody(binding->Handle);
                if (body == nullptr || !raw.valid(binding->Entity) ||
                    !raw.all_of<Transform::Component>(binding->Entity))
                {
                    continue;
                }
                if (body->Motion == Physics::MotionType::Static)
                {
                    ++Diagnostics.StaticWritebacksSkipped;
                    continue;
                }
                if (body->Motion == Physics::MotionType::Kinematic)
                {
                    ++Diagnostics.KinematicWritebacksSkipped;
                    continue;
                }

                const Transform::Component worldPose{
                    .Position = body->Pose.Position,
                    .Rotation = body->Pose.Rotation,
                    .Scale = body->Pose.Scale,
                };
                const auto parent = ParentWorldMatrix(raw, binding->Entity);
                Transform::Component local;
                if (!parent || !Transform::TryComputeLocalTransform(Transform::GetMatrix(worldPose), *parent, local))
                {
                    ++Diagnostics.WritebackFailures;
                    continue;
                }
                auto& transform = raw.get<Transform::Component>(binding->Entity);
                if (PoseEquals(transform, ConvertTransform(local)))
                    continue;
                transform = local;
                raw.emplace_or_replace<Transform::IsDirtyTag>(binding->Entity);
                raw.emplace_or_replace<Transform::WorldUpdatedTag>(binding->Entity);
                ++Diagnostics.DynamicWritebacks;
            }
            // All ancestors have published before fingerprints are composed.
            // Engine owns the dirty transform-cache flush after Simulation.
            for (const auto& [depth, binding] : bindings)
            {
                (void)depth;
                Physics::BodyDescriptor authored;
                const auto* body = state.World.GetBody(binding->Handle);
                if (body != nullptr && body->Motion == Physics::MotionType::Dynamic &&
                    raw.valid(binding->Entity) && TryBuildDescriptor(raw, binding->Entity, authored, false))
                {
                    binding->AuthoringFingerprint = Fingerprint(authored);
                    binding->AuthoredLinearVelocity = authored.LinearVelocity;
                    binding->AuthoredAngularVelocity = authored.AngularVelocity;
                }
            }
        }

        void ProcessFrame(RuntimeFrameHookContext& context)
        {
            if (!Config.Enabled || !context.ActiveWorldHandle.IsValid())
                return;

            auto [stateIt, inserted] =
                WorldStates.try_emplace(context.ActiveWorldHandle);
            if (inserted)
                ++Diagnostics.WorldStatesCreated;
            WorldState& state = stateIt->second;
            Diagnostics.LastProcessedWorld = context.ActiveWorldHandle;
            ++Diagnostics.FramesProcessed;

            SyncAuthoring(state, context.ActiveWorld);
            if (!std::isfinite(context.FrameDeltaSeconds) ||
                context.FrameDeltaSeconds < 0.0)
            {
                ++Diagnostics.InvalidFrameDeltas;
                RefreshDerivedDiagnostics();
                return;
            }

            const float frameDelta = static_cast<float>(std::min<double>(
                context.FrameDeltaSeconds,
                Config.MaxAccumulatedSeconds));
            state.AccumulatorSeconds = std::min(
                state.AccumulatorSeconds + frameDelta,
                Config.MaxAccumulatedSeconds);

            std::uint32_t stepsThisFrame = 0u;
            constexpr float kAccumulatorEpsilon = 1.0e-7f;
            while (state.AccumulatorSeconds + kAccumulatorEpsilon >=
                       Config.FixedDeltaSeconds &&
                   stepsThisFrame < Config.MaxStepsPerFrame)
            {
                Diagnostics.LastStepOrder = NextOrder();
                const Physics::SolveStepDiagnostics step =
                    state.World.SolveStep(Physics::StepInput{
                        .DeltaSeconds = Config.FixedDeltaSeconds,
                        .Gravity = Config.Gravity,
                        .IntegrateKinematicPoses = false,
                    });
                Diagnostics.LastContactsSolved = step.ContactsSolved;
                Diagnostics.LastNonConvergedIslands =
                    step.NonConvergedIslands;
                Diagnostics.LastMaxPenetrationBefore =
                    step.MaxPenetrationBefore;
                Diagnostics.LastMaxPenetrationAfter =
                    step.MaxPenetrationAfter;
                Diagnostics.LastEnergyDrift = step.EnergyDrift;
                if (step.Status != Physics::ValidationStatus::Valid)
                {
                    ++Diagnostics.SolverFailures;
                    break;
                }
                if (step.Solve == Physics::SolveStatus::Degraded)
                    ++Diagnostics.SolverFailures;
                if (step.Solve == Physics::SolveStatus::MaxIterationsReached)
                    ++Diagnostics.NonConvergedSteps;
                ++Diagnostics.FixedSteps;
                ++stepsThisFrame;
                state.AccumulatorSeconds = std::max(
                    0.0f,
                    state.AccumulatorSeconds - Config.FixedDeltaSeconds);
            }
            if (stepsThisFrame == Config.MaxStepsPerFrame &&
                state.AccumulatorSeconds + kAccumulatorEpsilon >=
                    Config.FixedDeltaSeconds)
            {
                ++Diagnostics.StepBudgetExhaustions;
            }
            if (stepsThisFrame > 0u)
                WritebackDynamicTransforms(state, context.ActiveWorld);
            RefreshDerivedDiagnostics();
        }

        void Unsubscribe(KernelEventBus& events) noexcept
        {
            if (WorldDestroyedSubscription.IsValid())
                events.Unsubscribe(WorldDestroyedSubscription);
            if (ShutdownSubscription.IsValid())
                events.Unsubscribe(ShutdownSubscription);
            WorldDestroyedSubscription = {};
            ShutdownSubscription = {};
        }
    };

    PhysicsModule::PhysicsModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    PhysicsModule::~PhysicsModule() = default;

    std::string_view PhysicsModule::Name() const noexcept
    {
        return "Runtime.PhysicsModule";
    }

    Core::Result PhysicsModule::OnRegister(EngineSetup& setup)
    {
        if (!m_Impl || m_Impl->Registered ||
            setup.Services().Phase() != ServiceRegistryPhase::Registration)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Config = {};
        m_Impl->Diagnostics = {};
        m_Impl->WorldStates.clear();
        m_Impl->Worlds = &setup.Worlds();
        m_Impl->OrderCounter = 0u;

        m_Impl->WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [this](const WorldWillBeDestroyed& event)
                {
                    m_Impl->RemoveWorldState(event.World);
                });
        m_Impl->ShutdownSubscription =
            setup.Subscribe<RuntimeShutdownAnnounced>(
                [this](const RuntimeShutdownAnnounced&)
                {
                    m_Impl->ClearWorldStates();
                });
        if (!m_Impl->WorldDestroyedSubscription.IsValid() ||
            !m_Impl->ShutdownSubscription.IsValid())
        {
            m_Impl->Unsubscribe(setup.Events());
            m_Impl->Worlds = nullptr;
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const Core::Result hook = setup.RegisterFrameHook(
            FramePhase::Simulation,
            [this](RuntimeFrameHookContext& context)
            {
                m_Impl->ProcessFrame(context);
            });
        if (!hook.has_value())
        {
            m_Impl->Unsubscribe(setup.Events());
            m_Impl->Worlds = nullptr;
            return hook;
        }
        m_Impl->Registered = true;
        m_Impl->RefreshDerivedDiagnostics();
        return Core::Ok();
    }

    Core::Result PhysicsModule::OnResolve(EngineSetup& setup)
    {
        if (!m_Impl || !m_Impl->Registered)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        auto configControl =
            setup.Services().Require<EngineConfigControl>(Name());
        if (!configControl.has_value())
            return Core::Err(configControl.error());
        if (configControl->get().SectionRegistry().Find(
                kPhysicsModuleConfigSectionName) == nullptr)
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        const auto config = GetPhysicsModuleConfig(
            configControl->get().GetEngineConfigControlState().ActiveConfig);
        if (!config.has_value())
            return Core::Err(Core::ErrorCode::InvalidState);
        return ApplyConfig(*config);
    }

    void PhysicsModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;
        m_Impl->Unsubscribe(context.Events);
        m_Impl->ClearWorldStates();
        m_Impl->Registered = false;
        m_Impl->Worlds = nullptr;
        m_Impl->RefreshDerivedDiagnostics();
    }

    Core::Result PhysicsModule::ApplyConfig(const PhysicsModuleConfig& config)
    {
        if (!m_Impl || !IsValidConfig(config))
        {
            if (m_Impl)
                ++m_Impl->Diagnostics.ConfigRejects;
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const PhysicsModuleConfig previous = m_Impl->Config;
        const bool changed = !ConfigEquals(previous, config);
        m_Impl->Config = config;
        ++m_Impl->Diagnostics.ConfigApplies;
        if (previous.Enabled && !config.Enabled)
        {
            m_Impl->ClearWorldStates();
        }
        else if (changed)
        {
            for (auto& [world, state] : m_Impl->WorldStates)
            {
                (void)world;
                state.AccumulatorSeconds = 0.0f;
            }
        }
        m_Impl->RefreshDerivedDiagnostics();
        return Core::Ok();
    }

    PhysicsModuleSnapshot PhysicsModule::GetSnapshot() const noexcept
    {
        if (!m_Impl)
            return {};
        return PhysicsModuleSnapshot{
            .Config = m_Impl->Config,
            .Diagnostics = m_Impl->Diagnostics,
        };
    }
}
