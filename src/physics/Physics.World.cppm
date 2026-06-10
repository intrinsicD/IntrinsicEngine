module;

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Physics.World;

import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Physics
{
    struct BodyTag
    {
    };

    using BodyHandle = Extrinsic::Core::StrongHandle<BodyTag>;

    enum class MotionType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class ShapeKind : std::uint8_t
    {
        Sphere,
        Capsule,
        Box,
    };

    enum class ValidationStatus : std::uint8_t
    {
        Valid,
        InvalidPose,
        EmptyShapeList,
        InvalidShape,
        InvalidMass,
        InvalidVelocity,
        InvalidDamping,
        InvalidGravityScale,
        InvalidStep,
        InvalidSolverSettings,
    };

    struct Transform
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f};
    };

    struct ShapeDescriptor
    {
        ShapeKind Kind{ShapeKind::Sphere};
        Transform Local{};
        glm::vec3 HalfExtents{0.5f};
        float Radius{0.5f};
        float CapsuleHalfHeight{0.5f};
        bool IsTrigger{false};
        bool Enabled{true};
    };

    struct BodyDescriptor
    {
        MotionType Motion{MotionType::Static};
        Transform Pose{};
        glm::vec3 LinearVelocity{0.0f};
        glm::vec3 AngularVelocity{0.0f};
        float Mass{1.0f};
        float LinearDamping{0.0f};
        float AngularDamping{0.0f};
        float GravityScale{1.0f};
        bool Enabled{true};
        bool ParticipatesInContacts{true};
        std::vector<ShapeDescriptor> Shapes{};
    };

    struct StepInput
    {
        float DeltaSeconds{1.0f / 60.0f};
        glm::vec3 Gravity{0.0f, -9.80665f, 0.0f};
    };

    struct StepDiagnostics
    {
        ValidationStatus Status{ValidationStatus::Valid};
        std::uint32_t StepIndex{0u};
        std::uint32_t BodiesVisited{0u};
        std::uint32_t DynamicBodiesIntegrated{0u};
        std::uint32_t KinematicBodiesIntegrated{0u};
        std::uint32_t StaticBodiesSkipped{0u};
        std::uint32_t DisabledBodiesSkipped{0u};
    };

    enum class CollisionRejectReason : std::uint8_t
    {
        None,
        InvalidBody,
        InvalidShape,
        DisabledBody,
        DisabledShape,
        FilteredBody,
        NonUniformDynamicScale,
        UnsupportedPair,
    };

    struct ShapeReference
    {
        BodyHandle Body{};
        std::uint32_t ShapeIndex{0u};
    };

    struct CollisionCandidatePair
    {
        ShapeReference A{};
        ShapeReference B{};
    };

    struct ContactRecord
    {
        ShapeReference A{};
        ShapeReference B{};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float PenetrationDepth{0.0f};
        glm::vec3 ContactPointA{0.0f};
        glm::vec3 ContactPointB{0.0f};
        bool IsTrigger{false};
    };

    struct CollisionDiagnostics
    {
        ValidationStatus Status{ValidationStatus::Valid};
        CollisionRejectReason LastRejectReason{CollisionRejectReason::None};
        std::uint32_t BodiesVisited{0u};
        std::uint32_t ShapesVisited{0u};
        std::uint32_t DisabledBodiesSkipped{0u};
        std::uint32_t FilteredBodiesSkipped{0u};
        std::uint32_t DisabledShapesSkipped{0u};
        std::uint32_t InvalidBodiesRejected{0u};
        std::uint32_t InvalidShapesRejected{0u};
        std::uint32_t DynamicNonUniformScaleRejects{0u};
        std::uint32_t BroadphasePairs{0u};
        std::uint32_t ContactsGenerated{0u};
        std::uint32_t TriggerContacts{0u};
        std::uint32_t UnsupportedPairs{0u};
    };

    struct CollisionResult
    {
        std::vector<CollisionCandidatePair> Candidates{};
        std::vector<ContactRecord> Contacts{};
        CollisionDiagnostics Diagnostics{};
    };

    // ── PHYSICS-003: constraints, islands, sleep, and solver diagnostics ──
    //
    // All records below are physics-owned. Solver state, island membership,
    // and sleep timers are world internals; runtime consumes these records
    // through the diagnostics surface and never stores them in ECS.

    // Tuning knobs for the CPU contact solver and the sleep policy. Mirrors
    // the `physics.rigid_body_reference` (`METHOD-001`) StepParams shape so
    // parity fixtures can share values.
    struct SolverSettings
    {
        std::uint32_t MaxIterations{8u};
        float Restitution{0.0f};
        float PenetrationSlop{0.001f};
        float PositionCorrectionPercent{0.8f};
        // Sleep policy: a dynamic body accumulates low-motion time while both
        // velocity magnitudes stay below the thresholds; it sleeps when the
        // accumulated time reaches TimeToSleepSeconds. Whole-island wake: a
        // contact island containing any awake member wakes all members.
        bool EnableSleep{true};
        float SleepLinearVelocityThreshold{0.05f};
        float SleepAngularVelocityThreshold{0.05f};
        float TimeToSleepSeconds{0.5f};
    };

    // One contact island: dynamic bodies transitively connected through
    // non-trigger contacts. Static/kinematic bodies anchor islands but never
    // merge them. Deterministic ordering contract: `Bodies` is sorted by
    // (Index, Generation) ascending, `ContactIndices` (indices into the
    // CollisionResult::Contacts the islands were built from) is ascending,
    // and islands are ordered by their smallest body index.
    struct IslandRecord
    {
        std::vector<BodyHandle> Bodies{};
        std::vector<std::uint32_t> ContactIndices{};
    };

    struct IslandDiagnostics
    {
        std::uint32_t IslandCount{0u};
        std::uint32_t IslandedDynamicBodies{0u};
        std::uint32_t ContactsConsidered{0u};
        std::uint32_t TriggerContactsExcluded{0u};
        std::uint32_t StaticAnchoredContacts{0u};
        std::uint32_t NonDynamicContactsIgnored{0u};
    };

    struct IslandBuildResult
    {
        std::vector<IslandRecord> Islands{};
        IslandDiagnostics Diagnostics{};
    };

    struct SleepDiagnostics
    {
        std::uint32_t AwakeDynamicBodies{0u};
        std::uint32_t SleepingDynamicBodies{0u};
        std::uint32_t SleepTransitions{0u};
        std::uint32_t WakeTransitions{0u};
    };

    enum class SolveStatus : std::uint8_t
    {
        // Residual penetration reached max(PenetrationSlop, epsilon) within
        // MaxIterations passes.
        Converged,
        // Iteration budget exhausted with residual penetration above the
        // tolerance; NonConvergedIslands counts the islands that still carry
        // residual contacts.
        MaxIterationsReached,
        // A non-finite body state was produced or observed during the solve;
        // the offending island is left at its last finite state and the step
        // reports fail-closed degradation.
        Degraded,
    };

    struct SolveStepDiagnostics
    {
        ValidationStatus Status{ValidationStatus::Valid};
        SolveStatus Solve{SolveStatus::Converged};
        std::uint32_t IterationsUsed{0u};
        std::uint32_t ContactsSolved{0u};
        std::uint32_t NonConvergedIslands{0u};
        float MaxPenetrationBefore{0.0f};
        // Residual penetration recomputed from live shapes after the solve.
        float MaxPenetrationAfter{0.0f};
        // Largest approaching normal speed remaining across solved contacts
        // at the end of the final iteration pass.
        float MaxNormalVelocityResidual{0.0f};
        // Linear kinetic energy of awake dynamic bodies (the world models no
        // angular inertia yet; angular energy is intentionally excluded and
        // documented in docs/architecture/physics.md).
        float KineticEnergyBefore{0.0f};
        float KineticEnergyAfter{0.0f};
        float EnergyDrift{0.0f};
        IslandDiagnostics Islands{};
        SleepDiagnostics Sleep{};
        StepDiagnostics Integration{};
    };

    struct WorldDiagnostics
    {
        std::uint32_t BodyCount{0u};
        std::uint32_t BodiesCreated{0u};
        std::uint32_t BodiesDestroyed{0u};
        std::uint32_t DescriptorUpdates{0u};
        std::uint32_t InvalidDescriptorsRejected{0u};
        std::uint32_t StaleHandleRejects{0u};
        std::uint32_t StepsExecuted{0u};
        StepDiagnostics LastStep{};
        std::uint32_t SolveStepsExecuted{0u};
        SolveStepDiagnostics LastSolveStep{};
    };

    [[nodiscard]] ShapeDescriptor MakeSphere(float radius, const Transform& local = {});
    [[nodiscard]] ShapeDescriptor MakeCapsule(float radius, float halfHeight, const Transform& local = {});
    [[nodiscard]] ShapeDescriptor MakeBox(const glm::vec3& halfExtents, const Transform& local = {});
    [[nodiscard]] BodyDescriptor MakeStaticBody(const Transform& pose = {});
    [[nodiscard]] BodyDescriptor MakeKinematicBody(const Transform& pose = {});
    [[nodiscard]] BodyDescriptor MakeDynamicBody(float mass = 1.0f, const Transform& pose = {});

    [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept;
    [[nodiscard]] bool IsFinite(const glm::quat& value) noexcept;
    [[nodiscard]] bool IsValidUnitRotation(const glm::quat& value) noexcept;
    [[nodiscard]] ValidationStatus Validate(const Transform& transform) noexcept;
    [[nodiscard]] ValidationStatus Validate(const ShapeDescriptor& descriptor) noexcept;
    [[nodiscard]] ValidationStatus Validate(const BodyDescriptor& descriptor) noexcept;
    [[nodiscard]] ValidationStatus Validate(const StepInput& input) noexcept;
    [[nodiscard]] ValidationStatus Validate(const SolverSettings& settings) noexcept;

    class World
    {
    public:
        World() = default;

        [[nodiscard]] BodyHandle AddBody(const BodyDescriptor& descriptor);
        [[nodiscard]] bool DestroyBody(BodyHandle handle);
        [[nodiscard]] bool UpdateBody(BodyHandle handle, const BodyDescriptor& descriptor);
        [[nodiscard]] BodyDescriptor* GetBody(BodyHandle handle) noexcept;
        [[nodiscard]] const BodyDescriptor* GetBody(BodyHandle handle) const noexcept;
        [[nodiscard]] bool Contains(BodyHandle handle) const noexcept;

        [[nodiscard]] StepDiagnostics Step(const StepInput& input = {});
        [[nodiscard]] CollisionResult ComputeCollisionContacts() const;

        // ── PHYSICS-003 ───────────────────────────────────────────────────
        // Sleep-aware integrate + contact solve. Integrates awake bodies
        // (gravity, damping), computes contacts, builds deterministic
        // islands, runs the iterative linear contact solver per awake
        // island, and applies the sleep policy. `Step()` remains the raw
        // PHYSICS-001 integrator and ignores sleep state.
        [[nodiscard]] SolveStepDiagnostics SolveStep(const StepInput& input = {},
                                                     const SolverSettings& settings = {});

        // Group the supplied contacts into deterministic dynamic-body
        // islands (see IslandRecord ordering contract). Pure with respect
        // to body state; trigger contacts are excluded.
        [[nodiscard]] IslandBuildResult BuildIslands(const CollisionResult& collision) const;

        // Sleep state queries/mutation. Sleep state is world-internal;
        // UpdateBody() wakes the body because its descriptor changed.
        [[nodiscard]] bool IsBodyAsleep(BodyHandle handle) const noexcept;
        bool WakeBody(BodyHandle handle) noexcept;

        void Clear() noexcept;

        [[nodiscard]] std::size_t BodyCount() const noexcept { return m_Diagnostics.BodyCount; }
        [[nodiscard]] const WorldDiagnostics& GetDiagnostics() const noexcept { return m_Diagnostics; }

    private:
        struct SleepState
        {
            bool Asleep{false};
            float LowMotionSeconds{0.0f};
        };

        struct Slot
        {
            std::uint32_t Generation{1u};
            bool Occupied{false};
            BodyDescriptor Body{};
            SleepState Sleep{};
        };

        [[nodiscard]] Slot* ResolveSlot(BodyHandle handle) noexcept;
        [[nodiscard]] const Slot* ResolveSlot(BodyHandle handle) const noexcept;

        std::vector<Slot> m_Slots{};
        std::vector<std::uint32_t> m_FreeList{};
        WorldDiagnostics m_Diagnostics{};
    };
}
