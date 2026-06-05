module;

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.PhysicsBridge;

import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Physics.World;

export namespace Extrinsic::Runtime
{
    struct PhysicsBridgeFixedStepConfig
    {
        float FixedDeltaSeconds{1.0f / 60.0f};
        float MaxAccumulatedSeconds{0.25f};
        glm::vec3 Gravity{0.0f, -9.80665f, 0.0f};
    };

    struct PhysicsBridgeDiagnostics
    {
        std::uint32_t SidecarCount{0u};
        std::uint32_t SyncPasses{0u};
        std::uint32_t BodiesCreated{0u};
        std::uint32_t BodiesUpdated{0u};
        std::uint32_t BodiesRemoved{0u};
        std::uint32_t InvalidDescriptors{0u};
        std::uint32_t MissingStableIds{0u};
        std::uint32_t MissingAuthoringComponents{0u};
        std::uint32_t MissingTransforms{0u};
        std::uint32_t StaleHandles{0u};
        std::uint32_t FixedSteps{0u};
        std::uint32_t InvalidFixedStepConfigs{0u};
        std::uint32_t DynamicWritebacks{0u};
        std::uint32_t StaticWritebacksSkipped{0u};
        std::uint32_t KinematicWritebacksSkipped{0u};
        std::uint32_t LastSyncOrder{0u};
        std::uint32_t LastStepOrder{0u};
        std::uint32_t LastWritebackOrder{0u};
        float AccumulatorSeconds{0.0f};
    };

    class PhysicsBridge
    {
    public:
        using Registry = Extrinsic::ECS::Scene::Registry;
        using StableId = Extrinsic::ECS::Components::StableId;
        using BodyHandle = Extrinsic::Physics::BodyHandle;

        PhysicsBridge() = default;

        [[nodiscard]] Extrinsic::Physics::World& GetWorld() noexcept { return m_World; }
        [[nodiscard]] const Extrinsic::Physics::World& GetWorld() const noexcept { return m_World; }

        [[nodiscard]] const PhysicsBridgeDiagnostics& GetDiagnostics() const noexcept { return m_Diagnostics; }
        [[nodiscard]] float GetAccumulatorSeconds() const noexcept { return m_AccumulatorSeconds; }
        [[nodiscard]] std::optional<BodyHandle> ResolveBody(StableId id) const;

        const PhysicsBridgeDiagnostics& SyncAuthoring(Registry& registry);
        const PhysicsBridgeDiagnostics& TickFixedStep(Registry& registry,
                                                      float frameDeltaSeconds,
                                                      const PhysicsBridgeFixedStepConfig& config = {});
        void Clear() noexcept;

    private:
        struct Binding
        {
            BodyHandle Handle{};
            Extrinsic::ECS::EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
            std::uint32_t LastSeenGeneration{0u};
            Extrinsic::Physics::MotionType Motion{Extrinsic::Physics::MotionType::Static};
        };

        [[nodiscard]] std::uint32_t NextOrder() noexcept { return ++m_OrderCounter; }
        void RemoveBinding(StableId id, Binding& binding);
        void RemoveUntouchedBindings();
        void WritebackDynamicTransforms(Registry& registry);

        Extrinsic::Physics::World m_World{};
        std::unordered_map<StableId, Binding, Extrinsic::ECS::Components::StableIdHash> m_Bindings{};
        PhysicsBridgeDiagnostics m_Diagnostics{};
        float m_AccumulatorSeconds{0.0f};
        std::uint32_t m_SyncGeneration{0u};
        std::uint32_t m_OrderCounter{0u};
    };
}
