module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.Runtime.GizmoInteraction;

import Extrinsic.ECS.Component.Transform;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;

        [[nodiscard]] bool IsFinite(const glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // Project a world point to pixel coordinates. Returns false when the
        // point is behind the camera (clip w <= 0) or non-finite.
        [[nodiscard]] bool ProjectToPixels(const glm::mat4& viewProjection,
                                           const Core::Extent2D viewport,
                                           const glm::vec3 world,
                                           glm::vec2& outPixel) noexcept
        {
            const glm::vec4 clip = viewProjection * glm::vec4{world, 1.f};
            if (!(std::abs(clip.w) > kEpsilon) || clip.w <= 0.f)
                return false;
            const glm::vec3 ndc = glm::vec3{clip} / clip.w;
            if (!IsFinite(ndc))
                return false;
            const float w = static_cast<float>(viewport.Width);
            const float h = static_cast<float>(viewport.Height);
            outPixel.x = (ndc.x * 0.5f + 0.5f) * w;
            // NDC +Y is up; pixel +Y is down.
            outPixel.y = (1.f - (ndc.y * 0.5f + 0.5f)) * h;
            return true;
        }

        // 2D distance from point p to segment [a, b].
        [[nodiscard]] float DistancePointToSegment2D(const glm::vec2 p,
                                                     const glm::vec2 a,
                                                     const glm::vec2 b) noexcept
        {
            const glm::vec2 ab = b - a;
            const float lenSq = glm::dot(ab, ab);
            if (lenSq <= kEpsilon)
                return glm::length(p - a);
            float t = glm::dot(p - a, ab) / lenSq;
            t = glm::clamp(t, 0.f, 1.f);
            const glm::vec2 closest = a + ab * t;
            return glm::length(p - closest);
        }

        // Signed parameter along the axis line (origin + dir * t) of the point on
        // that line closest to the given ray. `dir` must be unit length. Returns
        // false for a degenerate / parallel configuration.
        [[nodiscard]] bool ClosestAxisParam(const glm::vec3 rayOrigin,
                                            const glm::vec3 rayDir,
                                            const glm::vec3 axisOrigin,
                                            const glm::vec3 axisDir,
                                            float& outParam) noexcept
        {
            // Lines: P1 = rayOrigin + s*rayDir ; P2 = axisOrigin + t*axisDir.
            const glm::vec3 r = rayOrigin - axisOrigin;
            const float b = glm::dot(rayDir, axisDir);
            const float d = glm::dot(rayDir, r);
            const float e = glm::dot(axisDir, r);
            const float denom = 1.f - b * b; // rayDir, axisDir assumed unit.
            if (!(std::abs(denom) > kEpsilon))
                return false; // parallel
            outParam = (e - b * d) / denom;
            return std::isfinite(outParam);
        }

        [[nodiscard]] bool ReadPosition(const Extrinsic::ECS::Scene::Registry& registry,
                                        const Extrinsic::ECS::EntityHandle entity,
                                        glm::vec3& outPosition) noexcept
        {
            if (!registry.IsValid(entity))
                return false;
            const auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(entity);
            if (transform == nullptr)
                return false;
            outPosition = transform->Position;
            return true;
        }

        [[nodiscard]] bool ReadTransform(const Extrinsic::ECS::Scene::Registry& registry,
                                         const Extrinsic::ECS::EntityHandle entity,
                                         glm::vec3& outPosition,
                                         glm::quat& outRotation,
                                         glm::vec3& outScale) noexcept
        {
            if (!registry.IsValid(entity))
                return false;
            const auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(entity);
            if (transform == nullptr)
                return false;
            outPosition = transform->Position;
            outRotation = transform->Rotation;
            outScale = transform->Scale;
            return true;
        }

        [[nodiscard]] glm::quat NormalizeOrIdentity(const glm::quat value) noexcept
        {
            const float lenSquared = glm::dot(value, value);
            if (!(std::isfinite(lenSquared) && lenSquared > kEpsilon))
                return glm::quat{1.f, 0.f, 0.f, 0.f};

            const float invLen = 1.0f / std::sqrt(lenSquared);
            return glm::quat{
                value.w * invLen,
                value.x * invLen,
                value.y * invLen,
                value.z * invLen,
            };
        }

        [[nodiscard]] bool MeaningfullyDifferent(const glm::vec3 beforePosition,
                                                 const glm::quat beforeRotation,
                                                 const glm::vec3 beforeScale,
                                                 const glm::vec3 afterPosition,
                                                 const glm::quat afterRotation,
                                                 const glm::vec3 afterScale) noexcept
        {
            const float rotationDelta = std::abs(glm::dot(NormalizeOrIdentity(beforeRotation),
                                                          NormalizeOrIdentity(afterRotation)));
            return glm::length(afterPosition - beforePosition) > kEpsilon ||
                   glm::length(afterScale - beforeScale) > kEpsilon ||
                   rotationDelta < 1.0f - kEpsilon;
        }
    }

    void GizmoUndoStack::Push(const GizmoTransformEdit& edit)
    {
        m_Records.push_back(edit);
    }

    GizmoInteraction::GizmoInteraction(const GizmoConfig& config) noexcept
        : m_Config(config)
    {
    }

    glm::vec3 GizmoInteraction::AxisDirection(const Registry& registry,
                                              const EntityHandle primary,
                                              const GizmoAxis axis) const
    {
        glm::vec3 base{0.f};
        switch (axis)
        {
            case GizmoAxis::X: base = {1.f, 0.f, 0.f}; break;
            case GizmoAxis::Y: base = {0.f, 1.f, 0.f}; break;
            case GizmoAxis::Z: base = {0.f, 0.f, 1.f}; break;
            case GizmoAxis::None: return glm::vec3{0.f};
        }

        if (m_Orientation == GizmoOrientation::Local && registry.IsValid(primary))
        {
            const auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(primary);
            if (transform != nullptr)
            {
                const glm::vec3 rotated = transform->Rotation * base;
                const float len = glm::length(rotated);
                if (std::isfinite(len) && len > kEpsilon)
                    return rotated / len;
            }
        }
        return base;
    }

    bool GizmoInteraction::ComputePivot(const Registry& registry,
                                        std::span<const EntityHandle> selected,
                                        glm::vec3& outPivot) const
    {
        glm::vec3 sum{0.f};
        std::uint32_t count = 0u;
        for (const EntityHandle entity : selected)
        {
            glm::vec3 position{0.f};
            if (ReadPosition(registry, entity, position))
            {
                sum += position;
                ++count;
            }
        }
        if (count == 0u)
            return false;
        outPivot = sum / static_cast<float>(count);
        return true;
    }

    GizmoHitResult GizmoInteraction::HitTest(const Registry& registry,
                                             const Extrinsic::Graphics::CameraViewSnapshot& camera,
                                             const glm::vec2 cursorPixel,
                                             const Core::Extent2D viewport,
                                             std::span<const EntityHandle> selected)
    {
        ++m_Diagnostics.HitTests;

        GizmoHitResult result{};
        if (!camera.Valid || Core::IsEmpty(viewport) || selected.empty())
            return result;

        glm::vec3 pivot{0.f};
        if (!ComputePivot(registry, selected, pivot))
            return result;

        // Primary entity = the first selected entity with a live transform.
        EntityHandle primary = Extrinsic::ECS::InvalidEntityHandle;
        for (const EntityHandle entity : selected)
        {
            glm::vec3 scratch{0.f};
            if (ReadPosition(registry, entity, scratch))
            {
                primary = entity;
                break;
            }
        }

        const float axisLength = m_Config.AxisLength > kEpsilon ? m_Config.AxisLength : 1.f;
        float bestDistance = m_Config.HandlePickRadiusPixels;
        GizmoAxis bestAxis = GizmoAxis::None;

        const GizmoAxis candidates[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
        for (const GizmoAxis axis : candidates)
        {
            if (m_AxisLock != GizmoAxis::None && m_AxisLock != axis)
                continue;

            const glm::vec3 dir = AxisDirection(registry, primary, axis);
            const glm::vec3 worldEnd = pivot + dir * axisLength;

            glm::vec2 pixelStart{0.f};
            glm::vec2 pixelEnd{0.f};
            if (!ProjectToPixels(camera.ViewProjection, viewport, pivot, pixelStart))
                continue;
            if (!ProjectToPixels(camera.ViewProjection, viewport, worldEnd, pixelEnd))
                continue;

            const float distance = DistancePointToSegment2D(cursorPixel, pixelStart, pixelEnd);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                bestAxis = axis;
            }
        }

        if (bestAxis == GizmoAxis::None)
            return result;

        result.Hit = true;
        result.Axis = bestAxis;
        result.Entity = primary;
        result.PixelDistance = bestDistance;
        ++m_Diagnostics.HitsResolved;
        return result;
    }

    bool GizmoInteraction::BeginDrag(const Registry& registry,
                                     const GizmoHitResult& hit,
                                     const PickRay& ray,
                                     std::span<const EntityHandle> selected)
    {
        if (!hit.Hit || hit.Axis == GizmoAxis::None || selected.empty())
            return false;

        const float dirLen = glm::length(ray.Direction);
        if (!(std::isfinite(dirLen) && dirLen > kEpsilon) || !IsFinite(ray.Origin))
            return false;
        const glm::vec3 rayDir = ray.Direction / dirLen;

        glm::vec3 pivot{0.f};
        if (!ComputePivot(registry, selected, pivot))
            return false;

        const glm::vec3 axisDir = AxisDirection(registry, hit.Entity, hit.Axis);
        float startParam = 0.f;
        if (!ClosestAxisParam(ray.Origin, rayDir, pivot, axisDir, startParam))
            return false;

        m_DragEntries.clear();
        for (const EntityHandle entity : selected)
        {
            glm::vec3 position{0.f};
            glm::quat rotation{1.f, 0.f, 0.f, 0.f};
            glm::vec3 scale{1.f};
            if (ReadTransform(registry, entity, position, rotation, scale))
            {
                m_DragEntries.push_back(DragEntry{
                    .Entity = entity,
                    .BeforePosition = position,
                    .BeforeRotation = rotation,
                    .BeforeScale = scale,
                });
            }
        }
        if (m_DragEntries.empty())
            return false;

        m_Dragging = true;
        m_DragMode = m_Mode;
        m_DragAxis = hit.Axis;
        m_DragOrigin = pivot;
        m_DragAxisDir = axisDir;
        m_DragStartParam = startParam;
        m_Pivot = pivot;
        ++m_Diagnostics.DragsStarted;
        return true;
    }

    bool GizmoInteraction::DragTick(Registry& registry, const PickRay& ray)
    {
        if (!m_Dragging)
            return false;

        const float dirLen = glm::length(ray.Direction);
        if (!(std::isfinite(dirLen) && dirLen > kEpsilon) || !IsFinite(ray.Origin))
            return false;
        const glm::vec3 rayDir = ray.Direction / dirLen;

        float currentParam = 0.f;
        if (!ClosestAxisParam(ray.Origin, rayDir, m_DragOrigin, m_DragAxisDir, currentParam))
            return false;

        const float rawDeltaScalar = currentParam - m_DragStartParam;
        float deltaScalar = rawDeltaScalar;
        const bool snap = HasModifier(m_ModifierMask, GizmoModifier::Snap);
        if (snap && m_DragMode == GizmoMode::Translate && m_Config.TranslateSnapStep > kEpsilon)
        {
            deltaScalar = std::round(deltaScalar / m_Config.TranslateSnapStep) * m_Config.TranslateSnapStep;
            ++m_Diagnostics.SnappedTicks;
        }
        else if (snap && m_DragMode == GizmoMode::Rotate && m_Config.RotateSnapStepRadians > kEpsilon)
        {
            float radians = rawDeltaScalar * m_Config.RotateRadiansPerWorldUnit;
            radians = std::round(radians / m_Config.RotateSnapStepRadians) * m_Config.RotateSnapStepRadians;
            deltaScalar = m_Config.RotateRadiansPerWorldUnit > kEpsilon
                ? radians / m_Config.RotateRadiansPerWorldUnit
                : rawDeltaScalar;
            ++m_Diagnostics.SnappedTicks;
        }
        else if (snap && m_DragMode == GizmoMode::Scale && m_Config.ScaleSnapStep > kEpsilon)
        {
            float factor = 1.0f + rawDeltaScalar * m_Config.ScaleFactorPerWorldUnit;
            factor = std::round(factor / m_Config.ScaleSnapStep) * m_Config.ScaleSnapStep;
            deltaScalar = m_Config.ScaleFactorPerWorldUnit > kEpsilon
                ? (factor - 1.0f) / m_Config.ScaleFactorPerWorldUnit
                : rawDeltaScalar;
            ++m_Diagnostics.SnappedTicks;
        }

        const glm::vec3 worldDelta = m_DragAxisDir * deltaScalar;
        for (const DragEntry& entry : m_DragEntries)
        {
            if (!registry.IsValid(entry.Entity))
                continue;
            auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(entry.Entity);
            if (transform == nullptr)
                continue;
            switch (m_DragMode)
            {
            case GizmoMode::Translate:
                transform->Position = entry.BeforePosition + worldDelta;
                break;
            case GizmoMode::Rotate:
            {
                const float radians = deltaScalar * m_Config.RotateRadiansPerWorldUnit;
                transform->Rotation = glm::normalize(glm::angleAxis(radians, m_DragAxisDir) *
                                                       entry.BeforeRotation);
                break;
            }
            case GizmoMode::Scale:
            {
                const float minScale = m_Config.MinScale > kEpsilon ? m_Config.MinScale : kEpsilon;
                const float factor = std::max(minScale,
                                              1.0f + deltaScalar * m_Config.ScaleFactorPerWorldUnit);
                glm::vec3 scale = entry.BeforeScale;
                switch (m_DragAxis)
                {
                case GizmoAxis::X: scale.x = std::max(minScale, entry.BeforeScale.x * factor); break;
                case GizmoAxis::Y: scale.y = std::max(minScale, entry.BeforeScale.y * factor); break;
                case GizmoAxis::Z: scale.z = std::max(minScale, entry.BeforeScale.z * factor); break;
                case GizmoAxis::None: break;
                }
                transform->Scale = scale;
                break;
            }
            }
            registry.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::IsDirtyTag>(entry.Entity);
        }

        ++m_Diagnostics.DragTicks;
        return true;
    }

    std::size_t GizmoInteraction::DragCommit(const Registry& registry, GizmoUndoStack& undo)
    {
        if (!m_Dragging)
            return 0u;

        std::size_t emitted = 0u;
        for (const DragEntry& entry : m_DragEntries)
        {
            glm::vec3 after{0.f};
            glm::quat afterRotation{1.f, 0.f, 0.f, 0.f};
            glm::vec3 afterScale{1.f};
            if (!ReadTransform(registry, entry.Entity, after, afterRotation, afterScale))
                continue;
            if (!MeaningfullyDifferent(entry.BeforePosition,
                                       entry.BeforeRotation,
                                       entry.BeforeScale,
                                       after,
                                       afterRotation,
                                       afterScale))
                continue;
            undo.Push(GizmoTransformEdit{
                .Entity = entry.Entity,
                .BeforePosition = entry.BeforePosition,
                .AfterPosition = after,
                .BeforeRotation = entry.BeforeRotation,
                .AfterRotation = afterRotation,
                .BeforeScale = entry.BeforeScale,
                .AfterScale = afterScale,
            });
            ++emitted;
        }

        m_Dragging = false;
        m_DragMode = GizmoMode::Translate;
        m_DragAxis = GizmoAxis::None;
        m_DragEntries.clear();
        ++m_Diagnostics.DragsCommitted;
        m_Diagnostics.EditsEmitted += static_cast<std::uint32_t>(emitted);
        return emitted;
    }

    void GizmoInteraction::DragCancel(Registry& registry)
    {
        if (!m_Dragging)
            return;
        for (const DragEntry& entry : m_DragEntries)
        {
            if (!registry.IsValid(entry.Entity))
                continue;
            auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(entry.Entity);
            if (transform == nullptr)
                continue;
            transform->Position = entry.BeforePosition;
            transform->Rotation = entry.BeforeRotation;
            transform->Scale = entry.BeforeScale;
            registry.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::IsDirtyTag>(entry.Entity);
        }
        m_Dragging = false;
        m_DragMode = GizmoMode::Translate;
        m_DragAxis = GizmoAxis::None;
        m_DragEntries.clear();
        ++m_Diagnostics.DragsCancelled;
    }

    std::span<const Extrinsic::Graphics::TransformGizmoRenderPacket>
    TransformGizmoRenderPacketBuilder::Build(const Registry& registry,
                                             std::span<const EntityHandle> selected,
                                             const GizmoMode mode,
                                             const GizmoOrientation orientation,
                                             const float axisLength)
    {
        m_Packets.clear();
        const float length = axisLength > kEpsilon ? axisLength : 1.f;

        for (const EntityHandle entity : selected)
        {
            if (!registry.IsValid(entity))
                continue;
            const auto* transform =
                registry.Raw().try_get<Extrinsic::ECS::Components::Transform::Component>(entity);
            if (transform == nullptr)
                continue;

            glm::mat4 gizmoTransform{1.f};
            gizmoTransform[3] = glm::vec4{transform->Position, 1.f};
            if (orientation == GizmoOrientation::Local)
            {
                const glm::mat4 rotation = glm::mat4_cast(transform->Rotation);
                gizmoTransform[0] = rotation[0];
                gizmoTransform[1] = rotation[1];
                gizmoTransform[2] = rotation[2];
            }

            Extrinsic::Graphics::TransformGizmoRenderPacket packet{};
            packet.StableId = static_cast<std::uint32_t>(entity);
            packet.Transform = gizmoTransform;
            packet.AxisLength = length;
            packet.ShowTranslate = (mode == GizmoMode::Translate);
            packet.ShowRotate = (mode == GizmoMode::Rotate);
            packet.ShowScale = (mode == GizmoMode::Scale);
            m_Packets.push_back(packet);
        }

        return m_Packets;
    }
}
