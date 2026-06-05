#pragma once

#include <cstddef>
#include <vector>

namespace Intrinsic::Methods::Physics::RigidBodyReference
{
    inline constexpr const char* kMethodId = "physics.rigid_body_reference";
    inline constexpr const char* kBackendId = "cpu_reference";

    struct Vec3
    {
        double X{0.0};
        double Y{0.0};
        double Z{0.0};
    };

    struct Quaternion
    {
        double W{1.0};
        double X{0.0};
        double Y{0.0};
        double Z{0.0};
    };

    enum class MotionType
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class ShapeKind
    {
        Sphere,
        Capsule,
        Box,
    };

    enum class ValidationCode
    {
        Valid,
        InvalidTimeStep,
        InvalidSolverIterations,
        NonFiniteState,
        InvalidMass,
        InvalidInertia,
        InvalidShape,
    };

    struct ShapeDescriptor
    {
        ShapeKind  Kind{ShapeKind::Sphere};
        Vec3       LocalPosition{};
        Quaternion LocalRotation{};
        double     Radius{0.5};
        double     HalfHeight{0.5};
        Vec3       HalfExtents{0.5, 0.5, 0.5};
        bool       Enabled{true};
    };

    struct BodyState
    {
        MotionType Motion{MotionType::Static};
        Vec3       Position{};
        Quaternion Orientation{};
        Vec3       LinearVelocity{};
        Vec3       AngularVelocity{};
        double     Mass{0.0};
        Vec3       Inertia{0.0, 0.0, 0.0};
        double     LinearDamping{0.0};
        double     AngularDamping{0.0};
        bool       AllowSleep{true};
        bool       Awake{true};
        std::vector<ShapeDescriptor> Shapes{};
    };

    struct StepParams
    {
        double DeltaTime{1.0 / 60.0};
        Vec3   Gravity{0.0, -9.80665, 0.0};
        double Restitution{0.0};
        int    SolverIterations{8};
        double PenetrationSlop{0.001};
        double PositionCorrectionPercent{0.8};
    };

    struct Contact
    {
        std::size_t BodyA{0};
        std::size_t BodyB{0};
        std::size_t ShapeA{0};
        std::size_t ShapeB{0};
        Vec3        Normal{1.0, 0.0, 0.0};
        Vec3        Point{};
        double      Penetration{0.0};
        bool        Supported{false};
    };

    struct Diagnostics
    {
        ValidationCode Code{ValidationCode::Valid};
        std::size_t    ContactCount{0};
        std::size_t    UnsupportedPairCount{0};
        double         MaxPenetration{0.0};
        double         ResidualPenetration{0.0};
        double         KineticEnergyBefore{0.0};
        double         KineticEnergyAfter{0.0};
        double         EnergyDrift{0.0};
        bool           Converged{true};
    };

    struct StepResult
    {
        std::vector<BodyState> Bodies{};
        std::vector<Contact>   Contacts{};
        Diagnostics            Diagnostics{};
    };

    [[nodiscard]] auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 value) -> Vec3;
    [[nodiscard]] auto operator*(Vec3 lhs, double rhs) -> Vec3;
    [[nodiscard]] auto operator*(double lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator/(Vec3 lhs, double rhs) -> Vec3;

    [[nodiscard]] auto Dot(Vec3 lhs, Vec3 rhs) -> double;
    [[nodiscard]] auto Length(Vec3 value) -> double;
    [[nodiscard]] auto Normalize(Vec3 value, Vec3 fallback = Vec3{1.0, 0.0, 0.0}) -> Vec3;
    [[nodiscard]] auto Normalize(Quaternion value) -> Quaternion;
    [[nodiscard]] auto Rotate(Quaternion rotation, Vec3 value) -> Vec3;
    [[nodiscard]] auto IsFinite(Vec3 value) -> bool;
    [[nodiscard]] auto IsFinite(Quaternion value) -> bool;

    [[nodiscard]] auto MakeSphere(double radius, Vec3 localPosition = Vec3{}) -> ShapeDescriptor;
    [[nodiscard]] auto MakeCapsule(double radius, double halfHeight, Vec3 localPosition = Vec3{}) -> ShapeDescriptor;
    [[nodiscard]] auto MakeBox(Vec3 halfExtents, Vec3 localPosition = Vec3{}) -> ShapeDescriptor;
    [[nodiscard]] auto MakeStaticBody(Vec3 position, std::vector<ShapeDescriptor> shapes = {}) -> BodyState;
    [[nodiscard]] auto MakeKinematicBody(Vec3 position, std::vector<ShapeDescriptor> shapes = {}) -> BodyState;
    [[nodiscard]] auto MakeDynamicBody(Vec3 position, double mass, std::vector<ShapeDescriptor> shapes = {}) -> BodyState;

    [[nodiscard]] auto Validate(const ShapeDescriptor& shape) -> ValidationCode;
    [[nodiscard]] auto Validate(const BodyState& body) -> ValidationCode;
    [[nodiscard]] auto Validate(const std::vector<BodyState>& bodies, const StepParams& params) -> ValidationCode;
    [[nodiscard]] auto ComputeKineticEnergy(const std::vector<BodyState>& bodies) -> double;
    [[nodiscard]] auto Step(const std::vector<BodyState>& bodies, const StepParams& params) -> StepResult;
} // namespace Intrinsic::Methods::Physics::RigidBodyReference
