#include "RigidBodyReference.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Intrinsic::Methods::Physics::RigidBodyReference
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-9;

        [[nodiscard]] auto Clamp(double value, double lo, double hi) -> double
        {
            return std::max(lo, std::min(value, hi));
        }

        [[nodiscard]] auto LengthSquared(Vec3 value) -> double
        {
            return Dot(value, value);
        }

        [[nodiscard]] auto QuaternionLengthSquared(Quaternion value) -> double
        {
            return value.W * value.W + value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        }

        [[nodiscard]] auto Multiply(Quaternion lhs, Quaternion rhs) -> Quaternion
        {
            return Quaternion{
                lhs.W * rhs.W - lhs.X * rhs.X - lhs.Y * rhs.Y - lhs.Z * rhs.Z,
                lhs.W * rhs.X + lhs.X * rhs.W + lhs.Y * rhs.Z - lhs.Z * rhs.Y,
                lhs.W * rhs.Y - lhs.X * rhs.Z + lhs.Y * rhs.W + lhs.Z * rhs.X,
                lhs.W * rhs.Z + lhs.X * rhs.Y - lhs.Y * rhs.X + lhs.Z * rhs.W,
            };
        }

        [[nodiscard]] auto Conjugate(Quaternion value) -> Quaternion
        {
            return Quaternion{value.W, -value.X, -value.Y, -value.Z};
        }

        [[nodiscard]] auto AngleAxis(double angle, Vec3 axis) -> Quaternion
        {
            const Vec3   n = Normalize(axis);
            const double h = angle * 0.5;
            const double s = std::sin(h);
            return Normalize(Quaternion{std::cos(h), n.X * s, n.Y * s, n.Z * s});
        }

        [[nodiscard]] auto IntegrateOrientation(Quaternion orientation, Vec3 angularVelocity, double dt) -> Quaternion
        {
            const double speed = Length(angularVelocity);
            if (speed <= kEpsilon)
            {
                return Normalize(orientation);
            }
            return Normalize(Multiply(AngleAxis(speed * dt, angularVelocity / speed), orientation));
        }

        [[nodiscard]] auto DampingFactor(double damping, double dt) -> double
        {
            if (!std::isfinite(damping) || damping <= 0.0)
            {
                return 1.0;
            }
            return std::max(0.0, 1.0 - damping * dt);
        }

        [[nodiscard]] auto InverseMass(const BodyState& body) -> double
        {
            if (body.Motion != MotionType::Dynamic || body.Mass <= 0.0)
            {
                return 0.0;
            }
            return 1.0 / body.Mass;
        }

        struct WorldShape
        {
            ShapeKind   Kind{ShapeKind::Sphere};
            std::size_t Body{0};
            std::size_t Shape{0};
            Vec3        Center{};
            Quaternion  Rotation{};
            double      Radius{0.0};
            double      HalfHeight{0.0};
            Vec3        HalfExtents{};
        };

        [[nodiscard]] auto ToWorldShape(const BodyState& body, std::size_t bodyIndex, const ShapeDescriptor& shape,
                                        std::size_t shapeIndex) -> WorldShape
        {
            const Quaternion bodyRotation = Normalize(body.Orientation);
            WorldShape world{};
            world.Kind        = shape.Kind;
            world.Body        = bodyIndex;
            world.Shape       = shapeIndex;
            world.Center      = body.Position + Rotate(bodyRotation, shape.LocalPosition);
            world.Rotation    = Normalize(Multiply(bodyRotation, Normalize(shape.LocalRotation)));
            world.Radius      = shape.Radius;
            world.HalfHeight  = shape.HalfHeight;
            world.HalfExtents = shape.HalfExtents;
            return world;
        }

        [[nodiscard]] auto CapsuleEndpointA(const WorldShape& shape) -> Vec3
        {
            return shape.Center - Rotate(shape.Rotation, Vec3{0.0, 1.0, 0.0}) * shape.HalfHeight;
        }

        [[nodiscard]] auto CapsuleEndpointB(const WorldShape& shape) -> Vec3
        {
            return shape.Center + Rotate(shape.Rotation, Vec3{0.0, 1.0, 0.0}) * shape.HalfHeight;
        }

        [[nodiscard]] auto ClosestPointOnSegment(Vec3 point, Vec3 a, Vec3 b) -> Vec3
        {
            const Vec3   ab    = b - a;
            const double denom = Dot(ab, ab);
            if (denom <= kEpsilon)
            {
                return a;
            }
            const double t = Clamp(Dot(point - a, ab) / denom, 0.0, 1.0);
            return a + ab * t;
        }

        [[nodiscard]] auto MaybeSphereSphere(const WorldShape& a, const WorldShape& b) -> Contact
        {
            const Vec3   delta       = b.Center - a.Center;
            const double distance    = Length(delta);
            const double radiusTotal = a.Radius + b.Radius;
            if (distance > radiusTotal)
            {
                return Contact{.BodyA = a.Body, .BodyB = b.Body, .ShapeA = a.Shape, .ShapeB = b.Shape};
            }

            const Vec3 normal      = Normalize(delta);
            const double penetrate = radiusTotal - distance;
            return Contact{
                .BodyA       = a.Body,
                .BodyB       = b.Body,
                .ShapeA      = a.Shape,
                .ShapeB      = b.Shape,
                .Normal      = normal,
                .Point       = a.Center + normal * (a.Radius - penetrate * 0.5),
                .Penetration = penetrate,
                .Supported   = true,
            };
        }

        [[nodiscard]] auto MaybeSphereCapsule(const WorldShape& sphere, const WorldShape& capsule) -> Contact
        {
            const Vec3 closest     = ClosestPointOnSegment(sphere.Center, CapsuleEndpointA(capsule), CapsuleEndpointB(capsule));
            const Vec3 delta       = closest - sphere.Center;
            const double distance  = Length(delta);
            const double threshold = sphere.Radius + capsule.Radius;
            if (distance > threshold)
            {
                return Contact{.BodyA = sphere.Body, .BodyB = capsule.Body, .ShapeA = sphere.Shape, .ShapeB = capsule.Shape};
            }

            const Vec3 normal      = Normalize(delta);
            const double penetrate = threshold - distance;
            return Contact{
                .BodyA       = sphere.Body,
                .BodyB       = capsule.Body,
                .ShapeA      = sphere.Shape,
                .ShapeB      = capsule.Shape,
                .Normal      = normal,
                .Point       = sphere.Center + normal * (sphere.Radius - penetrate * 0.5),
                .Penetration = penetrate,
                .Supported   = true,
            };
        }

        [[nodiscard]] auto MaybeSphereBox(const WorldShape& sphere, const WorldShape& box) -> Contact
        {
            const Quaternion invBoxRotation = Conjugate(Normalize(box.Rotation));
            const Vec3       localCenter    = Rotate(invBoxRotation, sphere.Center - box.Center);
            const Vec3       closestLocal{
                Clamp(localCenter.X, -box.HalfExtents.X, box.HalfExtents.X),
                Clamp(localCenter.Y, -box.HalfExtents.Y, box.HalfExtents.Y),
                Clamp(localCenter.Z, -box.HalfExtents.Z, box.HalfExtents.Z),
            };

            const Vec3   localDelta = closestLocal - localCenter;
            const double distance   = Length(localDelta);
            if (distance > sphere.Radius)
            {
                return Contact{.BodyA = sphere.Body, .BodyB = box.Body, .ShapeA = sphere.Shape, .ShapeB = box.Shape};
            }

            Vec3   normalLocal = Normalize(localDelta);
            double penetrate   = sphere.Radius - distance;
            Vec3   pointLocal  = closestLocal;
            if (distance <= kEpsilon)
            {
                const double dx = box.HalfExtents.X - std::abs(localCenter.X);
                const double dy = box.HalfExtents.Y - std::abs(localCenter.Y);
                const double dz = box.HalfExtents.Z - std::abs(localCenter.Z);
                if (dx <= dy && dx <= dz)
                {
                    normalLocal = Vec3{localCenter.X >= 0.0 ? 1.0 : -1.0, 0.0, 0.0};
                    pointLocal  = Vec3{normalLocal.X * box.HalfExtents.X, localCenter.Y, localCenter.Z};
                    penetrate   = sphere.Radius + dx;
                }
                else if (dy <= dz)
                {
                    normalLocal = Vec3{0.0, localCenter.Y >= 0.0 ? 1.0 : -1.0, 0.0};
                    pointLocal  = Vec3{localCenter.X, normalLocal.Y * box.HalfExtents.Y, localCenter.Z};
                    penetrate   = sphere.Radius + dy;
                }
                else
                {
                    normalLocal = Vec3{0.0, 0.0, localCenter.Z >= 0.0 ? 1.0 : -1.0};
                    pointLocal  = Vec3{localCenter.X, localCenter.Y, normalLocal.Z * box.HalfExtents.Z};
                    penetrate   = sphere.Radius + dz;
                }
            }

            return Contact{
                .BodyA       = sphere.Body,
                .BodyB       = box.Body,
                .ShapeA      = sphere.Shape,
                .ShapeB      = box.Shape,
                .Normal      = Rotate(box.Rotation, normalLocal),
                .Point       = box.Center + Rotate(box.Rotation, pointLocal),
                .Penetration = penetrate,
                .Supported   = true,
            };
        }

        [[nodiscard]] auto Reverse(Contact contact) -> Contact
        {
            std::swap(contact.BodyA, contact.BodyB);
            std::swap(contact.ShapeA, contact.ShapeB);
            contact.Normal = -contact.Normal;
            return contact;
        }

        [[nodiscard]] auto GeneratePairContact(const WorldShape& a, const WorldShape& b, std::size_t& unsupportedCount) -> Contact
        {
            if (a.Kind == ShapeKind::Sphere && b.Kind == ShapeKind::Sphere)
            {
                return MaybeSphereSphere(a, b);
            }
            if (a.Kind == ShapeKind::Sphere && b.Kind == ShapeKind::Capsule)
            {
                return MaybeSphereCapsule(a, b);
            }
            if (a.Kind == ShapeKind::Capsule && b.Kind == ShapeKind::Sphere)
            {
                return Reverse(MaybeSphereCapsule(b, a));
            }
            if (a.Kind == ShapeKind::Sphere && b.Kind == ShapeKind::Box)
            {
                return MaybeSphereBox(a, b);
            }
            if (a.Kind == ShapeKind::Box && b.Kind == ShapeKind::Sphere)
            {
                return Reverse(MaybeSphereBox(b, a));
            }

            ++unsupportedCount;
            return Contact{.BodyA = a.Body, .BodyB = b.Body, .ShapeA = a.Shape, .ShapeB = b.Shape};
        }

        [[nodiscard]] auto GenerateContacts(const std::vector<BodyState>& bodies, std::size_t& unsupportedCount) -> std::vector<Contact>
        {
            std::vector<WorldShape> shapes;
            for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex)
            {
                for (std::size_t shapeIndex = 0; shapeIndex < bodies[bodyIndex].Shapes.size(); ++shapeIndex)
                {
                    const ShapeDescriptor& shape = bodies[bodyIndex].Shapes[shapeIndex];
                    if (shape.Enabled)
                    {
                        shapes.push_back(ToWorldShape(bodies[bodyIndex], bodyIndex, shape, shapeIndex));
                    }
                }
            }

            std::vector<Contact> contacts;
            for (std::size_t i = 0; i < shapes.size(); ++i)
            {
                for (std::size_t j = i + 1; j < shapes.size(); ++j)
                {
                    if (shapes[i].Body == shapes[j].Body)
                    {
                        continue;
                    }
                    Contact contact = GeneratePairContact(shapes[i], shapes[j], unsupportedCount);
                    if (contact.Supported)
                    {
                        contacts.push_back(contact);
                    }
                }
            }
            return contacts;
        }

        void ResolveContact(std::vector<BodyState>& bodies, const Contact& contact, const StepParams& params)
        {
            BodyState& a = bodies[contact.BodyA];
            BodyState& b = bodies[contact.BodyB];

            const double invA   = InverseMass(a);
            const double invB   = InverseMass(b);
            const double invSum = invA + invB;
            if (invSum <= kEpsilon)
            {
                return;
            }

            const Vec3   relativeVelocity = b.LinearVelocity - a.LinearVelocity;
            const double normalSpeed      = Dot(relativeVelocity, contact.Normal);
            if (normalSpeed < 0.0)
            {
                const double restitution = Clamp(params.Restitution, 0.0, 1.0);
                const double impulse     = (-(1.0 + restitution) * normalSpeed) / invSum;
                a.LinearVelocity         = a.LinearVelocity - contact.Normal * (impulse * invA);
                b.LinearVelocity         = b.LinearVelocity + contact.Normal * (impulse * invB);
            }

            const double correctionDepth = std::max(contact.Penetration - params.PenetrationSlop, 0.0);
            if (correctionDepth > 0.0)
            {
                const Vec3 correction = contact.Normal * ((Clamp(params.PositionCorrectionPercent, 0.0, 1.0) * correctionDepth) / invSum);
                a.Position            = a.Position - correction * invA;
                b.Position            = b.Position + correction * invB;
            }
        }
    } // namespace

    auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3
    {
        return Vec3{lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z};
    }

    auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3
    {
        return Vec3{lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z};
    }

    auto operator-(Vec3 value) -> Vec3
    {
        return Vec3{-value.X, -value.Y, -value.Z};
    }

    auto operator*(Vec3 lhs, double rhs) -> Vec3
    {
        return Vec3{lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs};
    }

    auto operator*(double lhs, Vec3 rhs) -> Vec3
    {
        return rhs * lhs;
    }

    auto operator/(Vec3 lhs, double rhs) -> Vec3
    {
        return Vec3{lhs.X / rhs, lhs.Y / rhs, lhs.Z / rhs};
    }

    auto Dot(Vec3 lhs, Vec3 rhs) -> double
    {
        return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;
    }

    auto Length(Vec3 value) -> double
    {
        return std::sqrt(LengthSquared(value));
    }

    auto Normalize(Vec3 value, Vec3 fallback) -> Vec3
    {
        const double len = Length(value);
        if (len <= kEpsilon || !std::isfinite(len))
        {
            return fallback;
        }
        return value / len;
    }

    auto Normalize(Quaternion value) -> Quaternion
    {
        const double lenSq = QuaternionLengthSquared(value);
        if (lenSq <= kEpsilon || !std::isfinite(lenSq))
        {
            return Quaternion{};
        }
        const double invLen = 1.0 / std::sqrt(lenSq);
        return Quaternion{value.W * invLen, value.X * invLen, value.Y * invLen, value.Z * invLen};
    }

    auto Rotate(Quaternion rotation, Vec3 value) -> Vec3
    {
        const Quaternion q = Normalize(rotation);
        const Quaternion v{0.0, value.X, value.Y, value.Z};
        const Quaternion r = Multiply(Multiply(q, v), Conjugate(q));
        return Vec3{r.X, r.Y, r.Z};
    }

    auto IsFinite(Vec3 value) -> bool
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    auto IsFinite(Quaternion value) -> bool
    {
        return std::isfinite(value.W) && std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    auto MakeSphere(double radius, Vec3 localPosition) -> ShapeDescriptor
    {
        return ShapeDescriptor{.Kind = ShapeKind::Sphere, .LocalPosition = localPosition, .Radius = radius};
    }

    auto MakeCapsule(double radius, double halfHeight, Vec3 localPosition) -> ShapeDescriptor
    {
        return ShapeDescriptor{.Kind = ShapeKind::Capsule, .LocalPosition = localPosition, .Radius = radius, .HalfHeight = halfHeight};
    }

    auto MakeBox(Vec3 halfExtents, Vec3 localPosition) -> ShapeDescriptor
    {
        return ShapeDescriptor{.Kind = ShapeKind::Box, .LocalPosition = localPosition, .HalfExtents = halfExtents};
    }

    auto MakeStaticBody(Vec3 position, std::vector<ShapeDescriptor> shapes) -> BodyState
    {
        return BodyState{
            .Motion   = MotionType::Static,
            .Position = position,
            .Mass     = 0.0,
            .Inertia  = Vec3{0.0, 0.0, 0.0},
            .Shapes   = std::move(shapes),
        };
    }

    auto MakeKinematicBody(Vec3 position, std::vector<ShapeDescriptor> shapes) -> BodyState
    {
        return BodyState{
            .Motion   = MotionType::Kinematic,
            .Position = position,
            .Mass     = 0.0,
            .Inertia  = Vec3{0.0, 0.0, 0.0},
            .Shapes   = std::move(shapes),
        };
    }

    auto MakeDynamicBody(Vec3 position, double mass, std::vector<ShapeDescriptor> shapes) -> BodyState
    {
        const double inertia = mass > 0.0 ? mass : 0.0;
        return BodyState{
            .Motion   = MotionType::Dynamic,
            .Position = position,
            .Mass     = mass,
            .Inertia  = Vec3{inertia, inertia, inertia},
            .Shapes   = std::move(shapes),
        };
    }

    auto Validate(const ShapeDescriptor& shape) -> ValidationCode
    {
        if (!shape.Enabled)
        {
            return ValidationCode::Valid;
        }
        if (!IsFinite(shape.LocalPosition) || !IsFinite(shape.LocalRotation) || QuaternionLengthSquared(shape.LocalRotation) <= kEpsilon)
        {
            return ValidationCode::NonFiniteState;
        }
        switch (shape.Kind)
        {
            case ShapeKind::Sphere:
                return std::isfinite(shape.Radius) && shape.Radius > 0.0 ? ValidationCode::Valid : ValidationCode::InvalidShape;
            case ShapeKind::Capsule:
                return std::isfinite(shape.Radius) && shape.Radius > 0.0 && std::isfinite(shape.HalfHeight) && shape.HalfHeight > 0.0
                           ? ValidationCode::Valid
                           : ValidationCode::InvalidShape;
            case ShapeKind::Box:
                return IsFinite(shape.HalfExtents) && shape.HalfExtents.X > 0.0 && shape.HalfExtents.Y > 0.0 && shape.HalfExtents.Z > 0.0
                           ? ValidationCode::Valid
                           : ValidationCode::InvalidShape;
        }
        return ValidationCode::InvalidShape;
    }

    auto Validate(const BodyState& body) -> ValidationCode
    {
        if (!IsFinite(body.Position) || !IsFinite(body.Orientation) || !IsFinite(body.LinearVelocity) || !IsFinite(body.AngularVelocity) ||
            QuaternionLengthSquared(body.Orientation) <= kEpsilon || !std::isfinite(body.LinearDamping) || !std::isfinite(body.AngularDamping))
        {
            return ValidationCode::NonFiniteState;
        }

        if (!std::isfinite(body.Mass) || body.Mass < 0.0)
        {
            return ValidationCode::InvalidMass;
        }
        if (body.Motion == MotionType::Dynamic && body.Mass <= 0.0)
        {
            return ValidationCode::InvalidMass;
        }
        if (!IsFinite(body.Inertia) || body.Inertia.X < 0.0 || body.Inertia.Y < 0.0 || body.Inertia.Z < 0.0)
        {
            return ValidationCode::InvalidInertia;
        }
        if (body.Motion == MotionType::Dynamic && (body.Inertia.X <= 0.0 || body.Inertia.Y <= 0.0 || body.Inertia.Z <= 0.0))
        {
            return ValidationCode::InvalidInertia;
        }

        for (const ShapeDescriptor& shape : body.Shapes)
        {
            const ValidationCode code = Validate(shape);
            if (code != ValidationCode::Valid)
            {
                return code == ValidationCode::NonFiniteState ? code : ValidationCode::InvalidShape;
            }
        }
        return ValidationCode::Valid;
    }

    auto Validate(const std::vector<BodyState>& bodies, const StepParams& params) -> ValidationCode
    {
        if (!std::isfinite(params.DeltaTime) || params.DeltaTime <= 0.0)
        {
            return ValidationCode::InvalidTimeStep;
        }
        if (params.SolverIterations <= 0)
        {
            return ValidationCode::InvalidSolverIterations;
        }
        if (!IsFinite(params.Gravity) || !std::isfinite(params.Restitution) || !std::isfinite(params.PenetrationSlop) ||
            !std::isfinite(params.PositionCorrectionPercent))
        {
            return ValidationCode::NonFiniteState;
        }
        for (const BodyState& body : bodies)
        {
            const ValidationCode code = Validate(body);
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        return ValidationCode::Valid;
    }

    auto ComputeKineticEnergy(const std::vector<BodyState>& bodies) -> double
    {
        double energy = 0.0;
        for (const BodyState& body : bodies)
        {
            if (body.Motion != MotionType::Dynamic || body.Mass <= 0.0)
            {
                continue;
            }
            energy += 0.5 * body.Mass * LengthSquared(body.LinearVelocity);
            energy += 0.5 * (body.Inertia.X * body.AngularVelocity.X * body.AngularVelocity.X +
                             body.Inertia.Y * body.AngularVelocity.Y * body.AngularVelocity.Y +
                             body.Inertia.Z * body.AngularVelocity.Z * body.AngularVelocity.Z);
        }
        return energy;
    }

    auto Step(const std::vector<BodyState>& bodies, const StepParams& params) -> StepResult
    {
        StepResult result{};
        result.Bodies = bodies;
        result.Diagnostics.KineticEnergyBefore = ComputeKineticEnergy(result.Bodies);

        const ValidationCode validation = Validate(result.Bodies, params);
        if (validation != ValidationCode::Valid)
        {
            result.Diagnostics.Code      = validation;
            result.Diagnostics.Converged = false;
            return result;
        }

        for (BodyState& body : result.Bodies)
        {
            if (!body.Awake)
            {
                continue;
            }
            if (body.Motion == MotionType::Dynamic)
            {
                body.LinearVelocity = body.LinearVelocity + params.Gravity * params.DeltaTime;
                body.LinearVelocity = body.LinearVelocity * DampingFactor(body.LinearDamping, params.DeltaTime);
                body.AngularVelocity = body.AngularVelocity * DampingFactor(body.AngularDamping, params.DeltaTime);
                body.Position = body.Position + body.LinearVelocity * params.DeltaTime;
                body.Orientation = IntegrateOrientation(body.Orientation, body.AngularVelocity, params.DeltaTime);
            }
            else if (body.Motion == MotionType::Kinematic)
            {
                body.Position = body.Position + body.LinearVelocity * params.DeltaTime;
                body.Orientation = IntegrateOrientation(body.Orientation, body.AngularVelocity, params.DeltaTime);
            }
        }

        result.Contacts = GenerateContacts(result.Bodies, result.Diagnostics.UnsupportedPairCount);
        result.Diagnostics.ContactCount = result.Contacts.size();
        for (const Contact& contact : result.Contacts)
        {
            result.Diagnostics.MaxPenetration = std::max(result.Diagnostics.MaxPenetration, contact.Penetration);
        }

        for (int iteration = 0; iteration < params.SolverIterations; ++iteration)
        {
            for (const Contact& contact : result.Contacts)
            {
                ResolveContact(result.Bodies, contact, params);
            }
        }

        std::size_t residualUnsupported = 0;
        const auto residualContacts = GenerateContacts(result.Bodies, residualUnsupported);
        for (const Contact& contact : residualContacts)
        {
            result.Diagnostics.ResidualPenetration = std::max(result.Diagnostics.ResidualPenetration, contact.Penetration);
        }
        result.Diagnostics.KineticEnergyAfter = ComputeKineticEnergy(result.Bodies);
        result.Diagnostics.EnergyDrift = result.Diagnostics.KineticEnergyAfter - result.Diagnostics.KineticEnergyBefore;
        result.Diagnostics.Converged = result.Diagnostics.ResidualPenetration <= std::max(params.PenetrationSlop, 1.0e-6);
        return result;
    }
} // namespace Intrinsic::Methods::Physics::RigidBodyReference
