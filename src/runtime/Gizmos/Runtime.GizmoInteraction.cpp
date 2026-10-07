module;
#include <string_view>
#include <optional>
#include <functional>
#include <chrono>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.Runtime.GizmoInteraction;

import Geometry.AABB;
import Geometry.RotationAveraging;
import Geometry.Validation;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Geometry::Validation::IsFinite;

        struct GizmoTransformMutationIdentity
        {
            GizmoInteraction::Registry* Scene{nullptr};
            WorldHandle World{};
        };

        struct GizmoTransformState
        {
            GizmoInteraction::EntityHandle Entity{
                Extrinsic::ECS::InvalidEntityHandle};
            Extrinsic::ECS::Components::Transform::Component Transform{};
        };

        struct GizmoTransformBatch
        {
            std::vector<GizmoTransformState> Transforms{};
        };

        [[nodiscard]] bool SameTransform(
            const Extrinsic::ECS::Components::Transform::Component& lhs,
            const Extrinsic::ECS::Components::Transform::Component& rhs)
            noexcept
        {
            return lhs.Position == rhs.Position &&
                   lhs.Rotation == rhs.Rotation &&
                   lhs.Scale == rhs.Scale;
        }

        [[nodiscard]] EditorCommandHistoryStatus ValidateGizmoTransformBatch(
            const GizmoTransformMutationIdentity& identity,
            const GizmoTransformBatch& expected)
        {
            if (identity.Scene == nullptr || !identity.World.IsValid())
                return EditorCommandHistoryStatus::MissingScene;
            if (expected.Transforms.empty())
                return EditorCommandHistoryStatus::NoChange;

            for (const GizmoTransformState& state : expected.Transforms)
            {
                if (!identity.Scene->IsValid(state.Entity))
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto* transform =
                    identity.Scene->Raw().try_get<
                        Extrinsic::ECS::Components::Transform::Component>(
                        state.Entity);
                if (transform == nullptr)
                    return EditorCommandHistoryStatus::MissingTransform;
                if (!SameTransform(*transform, state.Transform))
                    return EditorCommandHistoryStatus::StaleEntity;
            }
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyGizmoTransformBatch(
            const GizmoTransformMutationIdentity& identity,
            const GizmoTransformBatch& target)
        {
            if (target.Transforms.empty())
                return EditorCommandHistoryStatus::NoChange;

            for (const GizmoTransformState& state : target.Transforms)
            {
                if (!identity.Scene->IsValid(state.Entity))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (identity.Scene->Raw().try_get<
                        Extrinsic::ECS::Components::Transform::Component>(
                        state.Entity) == nullptr)
                {
                    return EditorCommandHistoryStatus::MissingTransform;
                }
            }
            for (const GizmoTransformState& state : target.Transforms)
            {
                identity.Scene->Raw().get<
                    Extrinsic::ECS::Components::Transform::Component>(
                    state.Entity) = state.Transform;
            }
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] GizmoTransformBatch StampGizmoTransformBatch(
            const GizmoTransformMutationIdentity& identity,
            const GizmoTransformBatch& target)
        {
            for (const GizmoTransformState& state : target.Transforms)
            {
                identity.Scene->Raw().emplace_or_replace<
                    Extrinsic::ECS::Components::Transform::IsDirtyTag>(
                    state.Entity);
            }
            return target;
        }

        namespace Tf = Extrinsic::ECS::Components::Transform;
        using Extrinsic::ECS::EntityHandle;
        using Extrinsic::ECS::InvalidEntityHandle;
        using Extrinsic::ECS::Scene::Registry;

        [[nodiscard]] bool IsFiniteMatrix(const glm::mat4& m) noexcept
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(m[c][r]))
                        return false;
            return true;
        }

        [[nodiscard]] EntityHandle ParentOf(const Registry& registry, const EntityHandle entity) noexcept
        {
            const auto* hierarchy =
                registry.Raw().try_get<Extrinsic::ECS::Components::Hierarchy::Component>(entity);
            return hierarchy != nullptr ? hierarchy->Parent : InvalidEntityHandle;
        }

        // Current authoring world matrix: local TRS composed up the parent
        // chain. The cached WorldMatrix is not used because the transform flush
        // runs after the gizmo drive. Dead or transform-less ancestors and
        // cycles fail. `InvalidEntityHandle` composes to identity (root).
        // Shared ancestors are recomposed per chain.
        [[nodiscard]] GizmoResult ComposeWorld(const Registry& registry,
                                               EntityHandle entity,
                                               glm::mat4& outWorld)
        {
            glm::mat4 world{1.f};
            std::vector<EntityHandle> visited{};
            while (entity != InvalidEntityHandle)
            {
                if (!registry.IsValid(entity) || std::ranges::find(visited, entity) != visited.end())
                    return {GizmoStatus::BrokenHierarchy, entity};
                const auto* transform = registry.Raw().try_get<Tf::Component>(entity);
                if (transform == nullptr)
                    return {GizmoStatus::BrokenHierarchy, entity};
                visited.push_back(entity);
                world = Tf::GetMatrix(*transform) * world;
                entity = ParentOf(registry, entity);
            }
            if (!IsFiniteMatrix(world))
                return {GizmoStatus::NonFiniteMatrix, visited.empty() ? InvalidEntityHandle : visited.front()};
            outWorld = world;
            return {};
        }

        struct Participant
        {
            EntityHandle Entity{InvalidEntityHandle};
            EntityHandle Parent{InvalidEntityHandle};
            Tf::Component Local{};
            glm::mat4 ParentWorld{1.f};
            glm::mat4 World{1.f};
            bool Target{false};
        };

        struct SelectionAnalysis
        {
            GizmoFrame Frame{};
            std::vector<Participant> Participants{};
        };

        [[nodiscard]] glm::mat3 BasisFor(std::span<const Participant> participants,
                                         GizmoFrame& frame)
        {
            if (frame.RequestedOrientation == GizmoOrientation::Global)
                return glm::mat3{1.f};

            std::vector<glm::mat3> rotations{};
            rotations.reserve(participants.size());
            for (const Participant& p : participants)
            {
                Tf::Component world{};
                if (!Tf::TryDecomposeMatrix(p.World, world))
                {
                    frame.BasisFallback = GizmoBasisFallback::RotationUnavailable;
                    return glm::mat3{1.f};
                }
                rotations.push_back(glm::mat3_cast(world.Rotation));
            }

            glm::mat3 basis = rotations.front();
            if (rotations.size() > 1u)
            {
                const Geometry::Rotation::RotationAverageResult mean =
                    Geometry::Rotation::ChordalMean(rotations);
                bool finite = true;
                for (int c = 0; c < 3; ++c)
                    finite = finite && IsFinite(mean.Rotation[c]);
                if (!mean.Succeeded() || !mean.Valid || !finite)
                {
                    frame.BasisFallback = mean.Status == Geometry::Rotation::RotationAverageStatus::DegenerateInput
                        ? GizmoBasisFallback::MeanDegenerate
                        : GizmoBasisFallback::MeanFailed;
                    return glm::mat3{1.f};
                }
                basis = mean.Rotation;
            }
            frame.ActualOrientation = GizmoOrientation::Local;
            return basis;
        }

        [[nodiscard]] SelectionAnalysis AnalyzeSelection(const Registry& registry,
                                                         std::span<const EntityHandle> selected,
                                                         const GizmoOrientation orientation,
                                                         const GizmoPivotMode pivotMode)
        {
            SelectionAnalysis analysis{};
            GizmoFrame& frame = analysis.Frame;
            frame.PivotMode = pivotMode;
            frame.RequestedOrientation = orientation;

            std::vector<EntityHandle> unique(selected.begin(), selected.end());
            std::ranges::sort(unique);
            unique.erase(std::ranges::unique(unique).begin(), unique.end());
            if (unique.empty())
            {
                frame.Result = {GizmoStatus::EmptySelection};
                return analysis;
            }

            glm::vec3 pivotSum{0.f};
            for (const EntityHandle entity : unique)
            {
                const auto* local = registry.IsValid(entity)
                    ? registry.Raw().try_get<Tf::Component>(entity)
                    : nullptr;
                if (local == nullptr)
                {
                    frame.Result = {GizmoStatus::InvalidEntity, entity};
                    return analysis;
                }
                Participant p{.Entity = entity, .Parent = ParentOf(registry, entity), .Local = *local};
                if (const GizmoResult composed = ComposeWorld(registry, p.Parent, p.ParentWorld);
                    !composed.Succeeded())
                {
                    frame.Result = composed;
                    return analysis;
                }
                p.World = p.ParentWorld * Tf::GetMatrix(p.Local);
                if (!IsFiniteMatrix(p.World))
                {
                    frame.Result = {GizmoStatus::NonFiniteMatrix, entity};
                    return analysis;
                }
                // ComposeWorld succeeded, so the parent chain is acyclic.
                p.Target = true;
                for (EntityHandle a = p.Parent; a != InvalidEntityHandle; a = ParentOf(registry, a))
                {
                    if (std::ranges::binary_search(unique, a))
                    {
                        p.Target = false;
                        break;
                    }
                }

                glm::vec3 point{p.World[3]};
                if (pivotMode == GizmoPivotMode::BoundsCenters)
                {
                    const auto* bounds =
                        registry.Raw().try_get<Extrinsic::ECS::Components::Culling::Local::Bounds>(entity);
                    if (bounds != nullptr && bounds->LocalBoundingAABB.IsValid() &&
                        IsFinite(bounds->LocalBoundingAABB.Min) && IsFinite(bounds->LocalBoundingAABB.Max))
                    {
                        // Center of TransformAABB(box, World) == World * center for affine World.
                        point = glm::vec3{p.World * glm::vec4{bounds->LocalBoundingAABB.GetCenter(), 1.f}};
                    }
                }
                pivotSum += point;
                analysis.Participants.push_back(p);
            }

            frame.Primary = unique.front();
            frame.Pivot = pivotSum / static_cast<float>(analysis.Participants.size());
            frame.Basis = BasisFor(analysis.Participants, frame);
            frame.Matrix = glm::translate(glm::mat4{1.f}, frame.Pivot) * glm::mat4{frame.Basis};
            if (!IsFiniteMatrix(frame.Matrix))
            {
                frame.Result = {GizmoStatus::NonFiniteMatrix, frame.Primary};
                return analysis;
            }
            frame.Result = {};
            return analysis;
        }

        [[nodiscard]] bool IsAffine(const glm::mat4& m) noexcept
        {
            constexpr float kAffineRowTolerance = 1.0e-6f;
            for (int c = 0; c < 4; ++c)
                if (!(std::abs(m[c][3] - (c == 3 ? 1.f : 0.f)) <= kAffineRowTolerance))
                    return false;
            return true;
        }

        // Element-wise reconstruction check. The affine last row must be
        // exact (up to float noise) independent of magnitudes, so perspective
        // never hides behind a large scale or translation. Each 3x3 column
        // and the translation column get a tolerance relative to their own
        // magnitude, so large translations do not hide 3x3 shear.
        [[nodiscard]] bool MatchesTrs(const glm::mat4& reconstructed, const glm::mat4& expected) noexcept
        {
            constexpr float kAbsTolerance = 1.0e-5f;
            constexpr float kRelTolerance = 1.0e-4f;
            if (!IsAffine(expected))
                return false;
            for (int c = 0; c < 4; ++c)
            {
                float magnitude = 0.f;
                for (int r = 0; r < 3; ++r)
                    magnitude = std::max(magnitude, std::abs(expected[c][r]));
                const float tolerance = kAbsTolerance + kRelTolerance * magnitude;
                for (int r = 0; r < 3; ++r)
                    if (!(std::abs(reconstructed[c][r] - expected[c][r]) <= tolerance))
                        return false;
            }
            return true;
        }

        // No-op test. The 3x3 part compares composed local matrices, so
        // equivalent TRS (a mirror stored as scale (-1,1,1), or decomposed as
        // (-1,-1,-1) with a 180-degree turn) compare equal. Translation gets
        // a tight absolute / few-ulp tolerance so a real move at a large
        // position is never mistaken for float residue.
        [[nodiscard]] bool SameLocalMatrix(const Tf::Component& lhs, const Tf::Component& rhs) noexcept
        {
            constexpr float kLinearTolerance = 1.0e-5f;
            constexpr float kTranslationAbsTolerance = 1.0e-5f;
            constexpr float kTranslationUlps = 4.f;
            const glm::mat4 a = Tf::GetMatrix(lhs);
            const glm::mat4 b = Tf::GetMatrix(rhs);
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    if (!(std::abs(a[c][r] - b[c][r]) <= kLinearTolerance * std::max(1.f, std::abs(b[c][r]))))
                        return false;
            for (int r = 0; r < 3; ++r)
            {
                // Exact ULP width at this magnitude, measured toward zero so it stays finite
                // at the float maximum; epsilon * magnitude can be up to twice as wide.
                const float magnitude = std::max(std::abs(lhs.Position[r]), std::abs(rhs.Position[r]));
                const float ulp = magnitude - std::nextafter(magnitude, 0.f);
                const float tolerance = std::max(kTranslationAbsTolerance, kTranslationUlps * ulp);
                if (!(std::abs(lhs.Position[r] - rhs.Position[r]) <= tolerance))
                    return false;
            }
            return true;
        }

        [[nodiscard]] Tf::Component OriginalOf(const auto& target) noexcept
        {
            return {.Position = target.OriginalPosition, .Rotation = target.OriginalRotation,
                    .Scale = target.OriginalScale};
        }

        [[nodiscard]] Tf::Component AcceptedOf(const auto& target) noexcept
        {
            return {.Position = target.AcceptedPosition, .Rotation = target.AcceptedRotation,
                    .Scale = target.AcceptedScale};
        }
    }

    glm::mat4 SnapGizmoRotation(const GizmoFrame& frame, const glm::mat4& gizmoMatrix, const float stepRadians)
    {
        const glm::quat delta = glm::quat_cast(glm::mat3{gizmoMatrix} * glm::transpose(frame.Basis));
        const glm::vec3 vector{delta.x, delta.y, delta.z};
        const float sine = glm::length(vector);
        // The axis comes from the vector part: glm::axis falls back to Z once w rounds to 1.
        // sin(theta/2) <= 1e-7 is float noise, far below half the smallest step (0.001 degrees).
        if (!(sine > 1.0e-7f))
            return frame.Matrix;
        const float theta = 2.f * std::atan2(sine, delta.w); // [0, 2pi) about `axis`
        const float turn = 2.f * glm::pi<float>();
        const float ka = std::round(theta / stepRadians);
        const float kb = std::round((theta - turn) / stepRadians);
        const float steps = std::abs(theta - ka * stepRadians) <= std::abs(theta - turn - kb * stepRadians) ? ka : kb;
        const float angle = steps * stepRadians;
        // G0 for zero steps, or for a whole turn within the float error of steps * step
        // (bounded by half a step, so the smallest real step and a 270-degree step stay turns).
        const float wholeTurnTolerance =
            std::min(8.f * std::numeric_limits<float>::epsilon() * turn, 0.5f * stepRadians);
        if (steps == 0.f || std::abs(std::remainder(angle, turn)) < wholeTurnTolerance)
            return frame.Matrix;
        return glm::translate(glm::mat4{1.f}, frame.Pivot) * glm::mat4_cast(glm::angleAxis(angle, vector / sine)) *
               glm::mat4{frame.Basis};
    }

    void GizmoInteraction::EndSession() noexcept
    {
        if (m_Dragging)
            ++m_SessionGeneration;
        m_Dragging = false;
        m_DragMode = GizmoMode::Translate;
        m_SessionRegistry = nullptr;
        m_SessionFrame = {};
        m_AcceptedGizmo = glm::mat4{1.f};
        m_Targets.clear();
        m_Links.clear();
    }

    GizmoFrame GizmoInteraction::ComputeFrame(const Registry& registry,
                                              std::span<const EntityHandle> selected,
                                              const GizmoOrientation orientation,
                                              const GizmoPivotMode pivotMode) const
    {
        return AnalyzeSelection(registry, selected, orientation, pivotMode).Frame;
    }

    GizmoResult GizmoInteraction::Begin(const Registry& registry,
                                        std::span<const EntityHandle> selected,
                                        const GizmoMode mode,
                                        const GizmoOrientation orientation,
                                        const GizmoPivotMode pivotMode)
    {
        if (m_Dragging)
            return {GizmoStatus::SessionActive};

        SelectionAnalysis analysis = AnalyzeSelection(registry, selected, orientation, pivotMode);
        if (!analysis.Frame.Available())
            return analysis.Frame.Result;

        std::vector<SessionTarget> targets{};
        for (const Participant& p : analysis.Participants)
        {
            if (!p.Target)
                continue;
            const float det = glm::determinant(p.ParentWorld);
            if (!(std::isfinite(det) && std::abs(det) >= 1.0e-8f))
                return {GizmoStatus::SingularParent, p.Entity};
            targets.push_back(SessionTarget{
                .Entity = p.Entity,
                .Parent = p.Parent,
                .OriginalPosition = p.Local.Position,
                .OriginalRotation = p.Local.Rotation,
                .OriginalScale = p.Local.Scale,
                .AcceptedPosition = p.Local.Position,
                .AcceptedRotation = p.Local.Rotation,
                .AcceptedScale = p.Local.Scale,
                .World0 = p.World,
                .ParentWorld = p.ParentWorld,
            });
        }

        // Frozen selection for conflict detection: every selected entity
        // (write target or not) and its parent chain to the root.
        // ponytail: linear membership scan, O(selection * depth); index by
        // entity if huge selections make Begin show up in profiles.
        std::vector<SessionLink> links{};
        for (const Participant& p : analysis.Participants)
        {
            for (EntityHandle e = p.Entity; e != InvalidEntityHandle; e = ParentOf(registry, e))
            {
                if (std::ranges::any_of(links, [e](const SessionLink& l) { return l.Entity == e; }))
                    break; // The rest of this chain is already recorded.
                // ComposeWorld succeeded, so every chain entity has a Transform.
                const Tf::Component& local = registry.Raw().get<Tf::Component>(e);
                links.push_back({
                    .Entity = e,
                    .Parent = ParentOf(registry, e),
                    .Target = std::ranges::any_of(targets, [e](const SessionTarget& t) { return t.Entity == e; }),
                    .Position = local.Position,
                    .Rotation = local.Rotation,
                    .Scale = local.Scale,
                });
            }
        }

        m_Dragging = true;
        m_DragMode = mode;
        m_SessionRegistry = &registry;
        m_SessionFrame = analysis.Frame;
        m_AcceptedGizmo = analysis.Frame.Matrix;
        m_Targets = std::move(targets);
        m_Links = std::move(links);
        ++m_SessionGeneration;
        ++m_Diagnostics.DragsStarted;
        return {};
    }

    GizmoResult GizmoInteraction::ValidateSession(const Registry& registry) const
    {
        if (!m_Dragging)
            return {GizmoStatus::NoSession};
        if (m_SessionRegistry != &registry)
            return {GizmoStatus::StaleSession};
        for (const SessionTarget& target : m_Targets)
        {
            const auto* current = registry.IsValid(target.Entity)
                ? registry.Raw().try_get<Tf::Component>(target.Entity)
                : nullptr;
            glm::mat4 parentWorld{1.f};
            if (current == nullptr || !SameTransform(*current, AcceptedOf(target)) ||
                ParentOf(registry, target.Entity) != target.Parent ||
                !ComposeWorld(registry, target.Parent, parentWorld).Succeeded() ||
                parentWorld != target.ParentWorld)
            {
                return {GizmoStatus::StaleSession, target.Entity};
            }
        }
        for (const SessionLink& link : m_Links)
        {
            if (!registry.IsValid(link.Entity) || ParentOf(registry, link.Entity) != link.Parent)
                return {GizmoStatus::StaleSession, link.Entity};
            if (link.Target)
                continue;
            const auto* local = registry.Raw().try_get<Tf::Component>(link.Entity);
            if (local == nullptr || local->Position != link.Position || local->Rotation != link.Rotation ||
                local->Scale != link.Scale)
            {
                return {GizmoStatus::StaleSession, link.Entity};
            }
        }
        return {};
    }

    void GizmoInteraction::RestoreOwnedTargets(Registry& registry)
    {
        for (const SessionTarget& target : m_Targets)
        {
            auto* current = registry.IsValid(target.Entity)
                ? registry.Raw().try_get<Tf::Component>(target.Entity)
                : nullptr;
            const Tf::Component original = OriginalOf(target);
            // A foreign write since the last accepted preview wins.
            if (current == nullptr || !SameTransform(*current, AcceptedOf(target)) ||
                SameTransform(*current, original))
            {
                continue;
            }
            *current = original;
            registry.Raw().emplace_or_replace<Tf::IsDirtyTag>(target.Entity);
        }
    }

    GizmoResult GizmoInteraction::Preview(Registry& registry, const glm::mat4& gizmoMatrix)
    {
        if (const GizmoResult valid = ValidateSession(registry); !valid.Succeeded())
            return valid;
        if (!IsFiniteMatrix(gizmoMatrix))
            return {GizmoStatus::NonFiniteMatrix};
        // Perspective in Gt is rejected before entity scales can shrink it.
        if (!IsAffine(gizmoMatrix))
            return {GizmoStatus::NonTrsResult};

        // Exactly G0 restores the original TRS bit-for-bit (no round-trip noise).
        const bool atStart = gizmoMatrix == m_SessionFrame.Matrix;
        // G0 is rigid: invert as [R^T | -R^T p] instead of a general inverse.
        const glm::mat3 basisT = glm::transpose(m_SessionFrame.Basis);
        glm::mat4 startInverse{basisT};
        startInverse[3] = glm::vec4{-(basisT * m_SessionFrame.Pivot), 1.f};
        const glm::mat4 delta = gizmoMatrix * startInverse;

        std::vector<Tf::Component> candidates{};
        candidates.reserve(m_Targets.size());
        for (const SessionTarget& target : m_Targets)
        {
            if (atStart)
            {
                candidates.push_back(OriginalOf(target));
                continue;
            }

            const glm::mat4 world = delta * target.World0;
            const glm::mat4 local = glm::inverse(target.ParentWorld) * world;
            if (!IsFiniteMatrix(local))
                return {GizmoStatus::NonFiniteMatrix, target.Entity};
            Tf::Component candidate{};
            if (!Tf::TryComputeLocalTransform(world, target.ParentWorld, candidate) ||
                !MatchesTrs(Tf::GetMatrix(candidate), local))
            {
                return {GizmoStatus::NonTrsResult, target.Entity};
            }
            candidates.push_back(candidate);
        }

        for (std::size_t i = 0; i < m_Targets.size(); ++i)
        {
            SessionTarget& target = m_Targets[i];
            registry.Raw().get<Tf::Component>(target.Entity) = candidates[i];
            registry.Raw().emplace_or_replace<Tf::IsDirtyTag>(target.Entity);
            target.AcceptedPosition = candidates[i].Position;
            target.AcceptedRotation = candidates[i].Rotation;
            target.AcceptedScale = candidates[i].Scale;
        }
        m_AcceptedGizmo = gizmoMatrix;
        return {};
    }

    EditorCommandHistoryResult GizmoInteraction::DragCommit(
        Registry& registry,
        const WorldHandle world,
        EditorCommandHistory& history)
    {
        if (!m_Dragging)
            return {};
        // Foreign registry: write nothing, keep the session for a real cancel.
        if (m_SessionRegistry != &registry)
            return {.Status = EditorCommandHistoryStatus::StaleEntity};
        // Conflict since the last accepted preview: record nothing and roll
        // back the previews this session still owns.
        if (!ValidateSession(registry).Succeeded())
        {
            RestoreOwnedTargets(registry);
            EndSession();
            ++m_Diagnostics.DragsCancelled;
            return {.Status = EditorCommandHistoryStatus::StaleEntity};
        }

        GizmoTransformBatch before{};
        GizmoTransformBatch after{};
        before.Transforms.reserve(m_Targets.size());
        after.Transforms.reserve(m_Targets.size());
        for (const SessionTarget& target : m_Targets)
        {
            const Tf::Component original = OriginalOf(target);
            const Tf::Component accepted = AcceptedOf(target);
            if (!SameLocalMatrix(original, accepted))
            {
                before.Transforms.push_back({.Entity = target.Entity, .Transform = original});
                after.Transforms.push_back({.Entity = target.Entity, .Transform = accepted});
                continue;
            }
            // No-op target (validated: current == accepted): drop float
            // residue and equivalent-TRS rewrites by restoring the original.
            if (!SameTransform(accepted, original))
            {
                registry.Raw().get<Tf::Component>(target.Entity) = original;
                registry.Raw().emplace_or_replace<Tf::IsDirtyTag>(target.Entity);
            }
        }

        const std::size_t editCount = after.Transforms.size();
        GizmoTransformBatch expected = after;
        EditorCommandHistoryResult result =
            Internal::ExecuteUndoableEntityMutation(
                history,
                "Manipulate Transform",
                GizmoTransformMutationIdentity{
                    .Scene = &registry,
                    .World = world,
                },
                std::move(expected),
                std::move(before),
                std::move(after),
                [](
                    const GizmoTransformMutationIdentity& identity,
                    const GizmoTransformBatch& expected,
                    const GizmoTransformBatch&)
                {
                    return ValidateGizmoTransformBatch(
                        identity, expected);
                },
                [](
                    const GizmoTransformMutationIdentity& identity,
                    const GizmoTransformBatch& target)
                {
                    return ApplyGizmoTransformBatch(
                        identity, target);
                },
                [](
                    const GizmoTransformMutationIdentity& identity,
                    const GizmoTransformBatch&,
                    const GizmoTransformBatch& target)
                {
                    return StampGizmoTransformBatch(
                        identity, target);
                },
                true,
                Internal::InitialMutationState::
                    TargetAlreadyApplied);

        // Not recorded (e.g. invalid world): previews must not outlive the
        // session without an undo entry.
        if (!result.Succeeded())
            RestoreOwnedTargets(registry);
        EndSession();
        ++m_Diagnostics.DragsCommitted;
        if (result.Succeeded())
        {
            m_Diagnostics.EditsEmitted +=
                static_cast<std::uint32_t>(editCount);
        }
        return result;
    }

    GizmoResult GizmoInteraction::DragCancel(Registry& registry)
    {
        if (!m_Dragging)
            return {GizmoStatus::NoSession};
        if (m_SessionRegistry != &registry)
            return {GizmoStatus::StaleSession};
        const GizmoResult validity = ValidateSession(registry);
        RestoreOwnedTargets(registry);
        EndSession();
        ++m_Diagnostics.DragsCancelled;
        return validity;
    }
}
