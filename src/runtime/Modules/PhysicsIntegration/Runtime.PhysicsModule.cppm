module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.PhysicsModule;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kPhysicsModuleConfigSectionName =
        "sandbox.physics";
    inline constexpr std::string_view kPhysicsModuleConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.physics";
    inline constexpr std::uint32_t kPhysicsModuleConfigSectionSchemaVersion = 1u;

    struct PhysicsModuleConfig
    {
        bool Enabled{false};
        float FixedDeltaSeconds{1.0f / 60.0f};
        float MaxAccumulatedSeconds{0.25f};
        std::uint32_t MaxStepsPerFrame{8u};
        glm::vec3 Gravity{0.0f, -9.80665f, 0.0f};
    };

    using PhysicsModuleConfigChangedCallback =
        std::function<void(const PhysicsModuleConfig&)>;

    [[nodiscard]] std::string SerializePhysicsModuleConfig(
        const PhysicsModuleConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidatePhysicsModuleConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<PhysicsModuleConfig>
    GetPhysicsModuleConfig(const Core::Config::EngineConfig& config);

    void SetPhysicsModuleConfig(
        Core::Config::EngineConfig& config,
        const PhysicsModuleConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakePhysicsModuleConfigSectionRegistration(
        PhysicsModuleConfigChangedCallback onChanged = {});

    struct PhysicsModuleDiagnostics
    {
        bool Enabled{false};
        WorldHandle LastProcessedWorld{};
        std::uint32_t OwnedPhysicsWorlds{0u};
        std::uint32_t SidecarCount{0u};
        std::uint32_t ActiveWorldBodyCount{0u};
        std::uint32_t ActiveWorldSidecarCount{0u};
        std::uint64_t ConfigApplies{0u};
        std::uint64_t ConfigRejects{0u};
        std::uint64_t WorldStatesCreated{0u};
        std::uint64_t WorldStatesRemoved{0u};
        std::uint64_t FramesProcessed{0u};
        std::uint64_t SyncPasses{0u};
        std::uint64_t BodiesCreated{0u};
        std::uint64_t BodiesUpdated{0u};
        std::uint64_t BodiesRemoved{0u};
        std::uint64_t InvalidDescriptors{0u};
        std::uint64_t MissingStableIds{0u};
        std::uint64_t DuplicateStableIds{0u};
        std::uint64_t MissingAuthoringComponents{0u};
        std::uint64_t MissingTransforms{0u};
        std::uint64_t StaleHandles{0u};
        std::uint64_t FixedSteps{0u};
        std::uint64_t StepBudgetExhaustions{0u};
        std::uint64_t InvalidFrameDeltas{0u};
        std::uint64_t SolverFailures{0u};
        std::uint64_t NonConvergedSteps{0u};
        std::uint64_t DynamicWritebacks{0u};
        std::uint64_t StaticWritebacksSkipped{0u};
        std::uint64_t KinematicWritebacksSkipped{0u};
        std::uint64_t LastSyncOrder{0u};
        std::uint64_t LastStepOrder{0u};
        std::uint64_t LastWritebackOrder{0u};
        std::uint32_t LastContactsSolved{0u};
        std::uint32_t LastNonConvergedIslands{0u};
        float LastMaxPenetrationBefore{0.0f};
        float LastMaxPenetrationAfter{0.0f};
        float LastEnergyDrift{0.0f};
        float ActiveWorldAccumulatorSeconds{0.0f};
    };

    struct PhysicsModuleSnapshot
    {
        PhysicsModuleConfig Config{};
        PhysicsModuleDiagnostics Diagnostics{};
    };

    class PhysicsModule final : public IRuntimeModule
    {
    public:
        PhysicsModule();
        ~PhysicsModule() override;

        PhysicsModule(const PhysicsModule&) = delete;
        PhysicsModule& operator=(const PhysicsModule&) = delete;
        PhysicsModule(PhysicsModule&&) = delete;
        PhysicsModule& operator=(PhysicsModule&&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        // EngineConfigControl is the external mutation lane. Sandbox wires its
        // post-commit section callback to this validated module-owned apply.
        [[nodiscard]] Core::Result ApplyConfig(
            const PhysicsModuleConfig& config);
        [[nodiscard]] PhysicsModuleSnapshot GetSnapshot() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
