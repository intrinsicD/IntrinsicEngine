module;

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <entt/entity/registry.hpp>

module Graphics:TransformGizmo.Impl;

import :TransformGizmo;
import :DebugDraw;
import :Camera;
import ECS;
import Core.Input;

namespace
{
    // Ray from camera through NDC point.
    struct Ray
    {
        glm::vec3 Origin;
        glm::vec3 Direction;
    };

    Ray RayFromNDC(const Graphics::CameraComponent& camera, const glm::vec2& ndc)
    {
        const glm::mat4 invProj = glm::inverse(camera.ProjectionMatrix);
        const glm::mat4 invView = glm::inverse(camera.ViewMatrix);

        glm::vec4 nearClip = invProj * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
        nearClip /= nearClip.w;
        glm::vec4 farClip = invProj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
        farClip /= farClip.w;

        glm::vec3 origin = glm::vec3(invView * nearClip);
        glm::vec3 far3   = glm::vec3(invView * farClip);
        glm::vec3 dir    = glm::normalize(far3 - origin);

        return {origin, dir};
    }

    // Closest point on a ray to a line segment, returns parameter t along the ray.
    float ClosestPointOnRayToSegment(const Ray& ray, const glm::vec3& a, const glm::vec3& b, float& distSq)
    {
        const glm::vec3 d1 = ray.Direction;
        const glm::vec3 d2 = b - a;
        const glm::vec3 r  = ray.Origin - a;

        const float a11 = glm::dot(d1, d1);
        const float a12 = -glm::dot(d1, d2);
        const float a22 = glm::dot(d2, d2);
        const float b1  = glm::dot(d1, r);
        const float b2  = -glm::dot(d2, r);

        const float det = a11 * a22 - a12 * a12;
        if (std::abs(det) < 1e-10f)
        {
            distSq = std::numeric_limits<float>::max();
            return 0.0f;
        }

        float t1 = (a22 * (-b1) - a12 * (-b2)) / det;
        float t2 = (a11 * (-b2) - a12 * (-b1)) / det;

        t2 = std::clamp(t2, 0.0f, 1.0f);
        t1 = std::max(t1, 0.0f);

        const glm::vec3 p1 = ray.Origin + d1 * t1;
        const glm::vec3 p2 = a + d2 * t2;
        distSq = glm::dot(p1 - p2, p1 - p2);

        return t1;
    }

    // Ray-plane intersection. Returns distance along ray or -1 if parallel.
    float RayPlaneIntersect(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal)
    {
        const float denom = glm::dot(ray.Direction, planeNormal);
        if (std::abs(denom) < 1e-6f) return -1.0f;
        return glm::dot(planePoint - ray.Origin, planeNormal) / denom;
    }

    [[nodiscard]] glm::vec3 BuildAxisDragPlaneNormal(const Graphics::CameraComponent& camera,
                                                     const glm::vec3& axisDir)
    {
        glm::vec3 planeNormal = camera.GetForward() - axisDir * glm::dot(camera.GetForward(), axisDir);

        if (glm::dot(planeNormal, planeNormal) < 1e-6f)
        {
            planeNormal = camera.GetUp() - axisDir * glm::dot(camera.GetUp(), axisDir);
        }
        if (glm::dot(planeNormal, planeNormal) < 1e-6f)
        {
            planeNormal = camera.GetRight() - axisDir * glm::dot(camera.GetRight(), axisDir);
        }
        if (glm::dot(planeNormal, planeNormal) < 1e-6f)
        {
            planeNormal = glm::cross(axisDir, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (glm::dot(planeNormal, planeNormal) < 1e-6f)
        {
            planeNormal = glm::cross(axisDir, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        return glm::normalize(planeNormal);
    }

    [[nodiscard]] float WrapAngleDelta(float radians)
    {
        return std::atan2(std::sin(radians), std::cos(radians));
    }

    [[nodiscard]] glm::vec2 WorldToNDC(const Graphics::CameraComponent& camera, const glm::vec3& p)
    {
        const glm::vec4 clip = camera.ProjectionMatrix * camera.ViewMatrix * glm::vec4(p, 1.0f);
        if (std::abs(clip.w) < 1e-6f)
            return {0.0f, 0.0f};
        return {clip.x / clip.w, clip.y / clip.w};
    }

    [[nodiscard]] bool ProjectWorldVectorToScreen(const Graphics::CameraComponent& camera,
                                                  const glm::vec3& pivot,
                                                  const glm::vec3& worldVector,
                                                  glm::vec2& outVector)
    {
        const glm::vec2 pivotNDC = WorldToNDC(camera, pivot);
        const glm::vec2 endNDC = WorldToNDC(camera, pivot + worldVector);
        outVector = endNDC - pivotNDC;
        return glm::dot(outVector, outVector) > 1e-8f;
    }

    [[nodiscard]] float ProjectMouseDeltaOntoHandle(const glm::vec2& mouseDeltaNDC,
                                                    const glm::vec2& handleVectorNDC)
    {
        const float denom = glm::dot(handleVectorNDC, handleVectorNDC);
        if (denom < 1e-8f)
            return 0.0f;
        return glm::dot(mouseDeltaNDC, handleVectorNDC) / denom;
    }

    [[nodiscard]] bool SolvePlaneDragDelta(const glm::vec2& mouseDeltaNDC,
                                           const glm::vec2& uNDC,
                                           const glm::vec2& vNDC,
                                           glm::vec2& outCoefficients)
    {
        const float det = uNDC.x * vNDC.y - uNDC.y * vNDC.x;
        if (std::abs(det) < 1e-8f)
            return false;

        outCoefficients.x = ( mouseDeltaNDC.x * vNDC.y - mouseDeltaNDC.y * vNDC.x) / det;
        outCoefficients.y = (-mouseDeltaNDC.x * uNDC.y + mouseDeltaNDC.y * uNDC.x) / det;
        return true;
    }

    [[nodiscard]] float ComputeScreenRotationDelta(const Graphics::CameraComponent& camera,
                                                   const glm::vec3& pivot,
                                                   const glm::vec3& axisDir,
                                                   const glm::vec2& startMouseNDC,
                                                   const glm::vec2& currentMouseNDC)
    {
        const glm::vec2 pivotNDC = WorldToNDC(camera, pivot);
        const glm::vec2 startVec = startMouseNDC - pivotNDC;
        const glm::vec2 currentVec = currentMouseNDC - pivotNDC;

        const float startLenSq = glm::dot(startVec, startVec);
        const float currentLenSq = glm::dot(currentVec, currentVec);
        if (startLenSq < 1e-8f || currentLenSq < 1e-8f)
            return std::numeric_limits<float>::quiet_NaN();

        const float angle = std::atan2(startVec.x * currentVec.y - startVec.y * currentVec.x,
                                       glm::dot(startVec, currentVec));
        const float orientation = (glm::dot(glm::normalize(axisDir), camera.GetForward()) <= 0.0f) ? 1.0f : -1.0f;
        return angle * orientation;
    }

    [[nodiscard]] ECS::Components::Transform::Component ResolveWorldTransform(entt::registry& registry, entt::entity entity)
    {
        ECS::Components::Transform::Component worldTransform{};
        if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
            world && ECS::Components::Transform::TryDecomposeMatrix(world->Matrix, worldTransform))
        {
            return worldTransform;
        }

        if (const auto* local = registry.try_get<ECS::Components::Transform::Component>(entity))
            return *local;

        return worldTransform;
    }

    [[nodiscard]] glm::mat4 ResolveWorldMatrix(entt::registry& registry, entt::entity entity)
    {
        if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
            return world->Matrix;
        if (const auto* local = registry.try_get<ECS::Components::Transform::Component>(entity))
            return ECS::Components::Transform::GetMatrix(*local);
        return glm::mat4(1.0f);
    }

    [[nodiscard]] bool TryResolveParentWorldMatrix(entt::registry& registry,
                                                   entt::entity entity,
                                                   glm::mat4& outParentWorldMatrix)
    {
        const auto* hierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(entity);
        if (!hierarchy || hierarchy->Parent == entt::null || !registry.valid(hierarchy->Parent))
            return false;

        outParentWorldMatrix = ResolveWorldMatrix(registry, hierarchy->Parent);
        return true;
    }
}

namespace Graphics
{

void TransformGizmo::ResetInteraction()
{
    m_State = GizmoState::Idle;
    m_HoveredAxis = GizmoAxis::None;
    m_ActiveAxis = GizmoAxis::None;
    m_DragStart = glm::vec3(0.0f);
    m_DragStartMouseNDC = glm::vec2(0.0f);
    m_InitialRotateAngle = 0.0f;
    m_DragPivotPosition = glm::vec3(0.0f);
    m_DragPivotRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_DragHandleScale = 1.0f;
    m_CachedTransforms.clear();
}

void TransformGizmo::SetMode(GizmoMode mode)
{
    if (m_Config.Mode == mode)
        return;

    m_Config.Mode = mode;
    ResetInteraction();
}

bool TransformGizmo::ComputePivot(entt::registry& registry, bool refreshCachedTransforms)
{
    auto view = registry.view<ECS::Components::Selection::SelectedTag,
                              ECS::Components::Transform::Component>();

    if (refreshCachedTransforms)
        m_CachedTransforms.clear();

    bool first = true;
    glm::vec3 centroid{0.0f};
    int count = 0;

    for (auto [entity, transform] : view.each())
    {
        const ECS::Components::Transform::Component worldTransform = ResolveWorldTransform(registry, entity);

        if (refreshCachedTransforms)
        {
            EntityTransformCache cache;
            cache.Entity = entity;
            cache.InitialPosition = transform.Position;
            cache.InitialRotation = transform.Rotation;
            cache.InitialScale = transform.Scale;
            cache.InitialWorldPosition = worldTransform.Position;
            cache.InitialWorldRotation = worldTransform.Rotation;
            cache.InitialWorldScale = worldTransform.Scale;
            cache.HasInitialParentWorldMatrix = TryResolveParentWorldMatrix(registry, entity, cache.InitialParentWorldMatrix);
            m_CachedTransforms.push_back(cache);
        }

        centroid += worldTransform.Position;
        ++count;

        if (first)
        {
            if (m_Config.Space == GizmoSpace::Local)
                m_PivotRotation = worldTransform.Rotation;
            first = false;
        }
    }

    if (count == 0) return false;

    if (m_Config.Pivot == GizmoPivot::Centroid)
        m_PivotPosition = centroid / static_cast<float>(count);
    else if (refreshCachedTransforms && !m_CachedTransforms.empty())
        m_PivotPosition = m_CachedTransforms[0].InitialWorldPosition;

    if (!refreshCachedTransforms && m_Config.Pivot == GizmoPivot::FirstSelected)
    {
        auto firstView = registry.view<ECS::Components::Selection::SelectedTag,
                                       ECS::Components::Transform::Component>();
        for (auto [entity, transform] : firstView.each())
        {
            (void)transform;
            m_PivotPosition = ResolveWorldTransform(registry, entity).Position;
            break;
        }
    }

    if (m_Config.Space == GizmoSpace::World)
        m_PivotRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    return true;
}

float TransformGizmo::ComputeHandleScale(const CameraComponent& camera) const
{
    // Constant screen-size: scale based on distance from camera.
    const float dist = glm::length(m_PivotPosition - camera.Position);
    const float halfFov = glm::radians(camera.Fov) * 0.5f;
    // Target: handles occupy ~15% of viewport height.
    return dist * std::tan(halfFov) * 0.15f * m_Config.HandleLength;
}

glm::vec3 TransformGizmo::GetAxisDirection(GizmoAxis axis) const
{
    glm::vec3 dir{0.0f};
    switch (axis)
    {
    case GizmoAxis::X:  dir = glm::vec3(1.0f, 0.0f, 0.0f); break;
    case GizmoAxis::Y:  dir = glm::vec3(0.0f, 1.0f, 0.0f); break;
    case GizmoAxis::Z:  dir = glm::vec3(0.0f, 0.0f, 1.0f); break;
    default: return dir;
    }

    const glm::quat rotation = (m_State == GizmoState::Active) ? m_DragPivotRotation : m_PivotRotation;
    return glm::rotate(rotation, dir);
}

uint32_t TransformGizmo::GetAxisColor(GizmoAxis axis) const
{
    const bool isHighlighted = (axis == m_HoveredAxis || axis == m_ActiveAxis);
    const uint32_t highlightColor = DebugDraw::PackColorF(1.0f, 1.0f, 0.3f, 1.0f);

    switch (axis)
    {
    case GizmoAxis::X:
    case GizmoAxis::YZ:
        return isHighlighted ? highlightColor : DebugDraw::PackColorF(0.9f, 0.15f, 0.15f, 1.0f);
    case GizmoAxis::Y:
    case GizmoAxis::XZ:
        return isHighlighted ? highlightColor : DebugDraw::PackColorF(0.15f, 0.85f, 0.15f, 1.0f);
    case GizmoAxis::Z:
    case GizmoAxis::XY:
        return isHighlighted ? highlightColor : DebugDraw::PackColorF(0.15f, 0.35f, 0.9f, 1.0f);
    case GizmoAxis::All:
        return isHighlighted ? highlightColor : DebugDraw::PackColorF(0.8f, 0.8f, 0.8f, 1.0f);
    default:
        return DebugDraw::Gray();
    }
}

GizmoAxis TransformGizmo::HitTest(const glm::vec2& mouseNDC,
                                   const CameraComponent& camera,
                                   float handleScale) const
{
    const Ray ray = RayFromNDC(camera, mouseNDC);

    // Threshold in NDC space (pick tolerance).
    const float threshold = m_Config.PickRadius;

    struct AxisHit
    {
        GizmoAxis Axis;
        float DistNDC;
    };

    std::vector<AxisHit> hits;

    // Test each axis line.
    auto testAxis = [&](GizmoAxis axis)
    {
        const glm::vec3 dir = GetAxisDirection(axis);
        const glm::vec3 start = m_PivotPosition;
        const glm::vec3 end   = m_PivotPosition + dir * handleScale;

        float distSq = 0.0f;
        ClosestPointOnRayToSegment(ray, start, end, distSq);

        // Convert to NDC distance for resolution-independent picking.
        const glm::vec3 closestOnSeg = start + dir * handleScale * 0.5f; // approx midpoint
        const float screenDist = std::sqrt(distSq);
        const float worldDist = glm::length(closestOnSeg - camera.Position);

        // Approximate NDC error.
        float ndcDist = 0.0f;
        if (worldDist > 1e-4f)
        {
            const float halfFov = glm::radians(camera.Fov) * 0.5f;
            ndcDist = screenDist / (worldDist * std::tan(halfFov));
        }

        if (ndcDist < threshold)
            hits.push_back({axis, ndcDist});
    };

    testAxis(GizmoAxis::X);
    testAxis(GizmoAxis::Y);
    testAxis(GizmoAxis::Z);

    if (m_Config.Mode == GizmoMode::Translate)
    {
        // Test plane handles (small quads at 0.3*handleScale).
        auto testPlane = [&](GizmoAxis planeAxis, const glm::vec3& normal, const glm::vec3& u, const glm::vec3& v)
        {
            const float sz = handleScale * 0.3f;

            float t = RayPlaneIntersect(ray, m_PivotPosition, normal);
            if (t < 0.0f) return;

            const glm::vec3 hitPt = ray.Origin + ray.Direction * t;
            const glm::vec3 local = hitPt - m_PivotPosition;
            const float du = glm::dot(local, u);
            const float dv = glm::dot(local, v);

            if (du > 0.0f && du < sz && dv > 0.0f && dv < sz)
                hits.push_back({planeAxis, 0.0f}); // Plane hits have highest priority
        };

        const glm::vec3 ax = GetAxisDirection(GizmoAxis::X);
        const glm::vec3 ay = GetAxisDirection(GizmoAxis::Y);
        const glm::vec3 az = GetAxisDirection(GizmoAxis::Z);

        testPlane(GizmoAxis::XY, az, ax, ay);
        testPlane(GizmoAxis::XZ, ay, ax, az);
        testPlane(GizmoAxis::YZ, ax, ay, az);
    }

    if (m_Config.Mode == GizmoMode::Scale)
    {
        // Test center cube for uniform scale.
        const float cubeHalf = handleScale * 0.08f;
        float t = RayPlaneIntersect(ray, m_PivotPosition, glm::normalize(camera.Position - m_PivotPosition));
        if (t > 0.0f)
        {
            const glm::vec3 hitPt = ray.Origin + ray.Direction * t;
            const glm::vec3 d = glm::abs(hitPt - m_PivotPosition);
            if (d.x < cubeHalf && d.y < cubeHalf && d.z < cubeHalf)
                hits.push_back({GizmoAxis::All, 0.0f});
        }
    }

    if (hits.empty()) return GizmoAxis::None;

    // Sort by distance (closest first), plane handles have priority (dist=0).
    std::sort(hits.begin(), hits.end(), [](const AxisHit& a, const AxisHit& b)
    {
        return a.DistNDC < b.DistNDC;
    });

    return hits[0].Axis;
}

glm::vec3 TransformGizmo::ProjectMouseToAxis(const glm::vec2& mouseNDC,
                                              const CameraComponent& camera,
                                              GizmoAxis axis,
                                              float handleScale) const
{
    (void)handleScale;
    const Ray ray = RayFromNDC(camera, mouseNDC);
    const glm::vec3 pivotPosition = (m_State == GizmoState::Active) ? m_DragPivotPosition : m_PivotPosition;

    if (axis == GizmoAxis::X || axis == GizmoAxis::Y || axis == GizmoAxis::Z)
    {
        const glm::vec3 dir = glm::normalize(GetAxisDirection(axis));
        const glm::vec3 planeNormal = BuildAxisDragPlaneNormal(camera, dir);
        const float t = RayPlaneIntersect(ray, pivotPosition, planeNormal);
        if (t < 0.0f) return pivotPosition;

        const glm::vec3 hitPoint = ray.Origin + ray.Direction * t;
        return pivotPosition + dir * glm::dot(hitPoint - pivotPosition, dir);
    }
    else
    {
        // Plane handles: intersect ray with plane.
        glm::vec3 normal{0.0f};
        switch (axis)
        {
        case GizmoAxis::XY: normal = GetAxisDirection(GizmoAxis::Z); break;
        case GizmoAxis::XZ: normal = GetAxisDirection(GizmoAxis::Y); break;
        case GizmoAxis::YZ: normal = GetAxisDirection(GizmoAxis::X); break;
        default: return pivotPosition;
        }

        float t = RayPlaneIntersect(ray, pivotPosition, normal);
        if (t < 0.0f) return pivotPosition;
        return ray.Origin + ray.Direction * t;
    }
}

float TransformGizmo::ProjectMouseToRotation(const glm::vec2& mouseNDC,
                                              const CameraComponent& camera,
                                              GizmoAxis axis,
                                              float handleScale) const
{
    (void)handleScale;
    if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
        return 0.0f;

    const glm::vec3 pivotPosition = (m_State == GizmoState::Active) ? m_DragPivotPosition : m_PivotPosition;
    const glm::vec3 normal = GetAxisDirection(axis);
    const Ray ray = RayFromNDC(camera, mouseNDC);
    float t = RayPlaneIntersect(ray, pivotPosition, normal);
    if (t < 0.0f) return 0.0f;

    const glm::vec3 hitPt = ray.Origin + ray.Direction * t - pivotPosition;
    if (glm::length(hitPt) < 1e-6f) return 0.0f;

    // Build local 2D basis on the rotation plane.
    glm::vec3 u, v;
    if (axis == GizmoAxis::X)
    {
        u = GetAxisDirection(GizmoAxis::Y);
        v = GetAxisDirection(GizmoAxis::Z);
    }
    else if (axis == GizmoAxis::Y)
    {
        u = GetAxisDirection(GizmoAxis::Z);
        v = GetAxisDirection(GizmoAxis::X);
    }
    else
    {
        u = GetAxisDirection(GizmoAxis::X);
        v = GetAxisDirection(GizmoAxis::Y);
    }

    return std::atan2(glm::dot(hitPt, v), glm::dot(hitPt, u));
}

float TransformGizmo::ProjectMouseToScale(const glm::vec2& mouseNDC,
                                           const CameraComponent& camera,
                                           GizmoAxis axis,
                                           float handleScale) const
{
    const float anchoredHandleScale = (m_State == GizmoState::Active) ? m_DragHandleScale : handleScale;
    const glm::vec3 pivotPosition = (m_State == GizmoState::Active) ? m_DragPivotPosition : m_PivotPosition;

    if (axis == GizmoAxis::All)
    {
        const glm::vec2 mouseDeltaNDC = mouseNDC - m_DragStartMouseNDC;
        return 1.0f + mouseDeltaNDC.y;
    }

    if (axis == GizmoAxis::X || axis == GizmoAxis::Y || axis == GizmoAxis::Z)
    {
        const glm::vec3 axisVector = glm::normalize(GetAxisDirection(axis)) * anchoredHandleScale;
        glm::vec2 axisNDC{0.0f};
        if (ProjectWorldVectorToScreen(camera, pivotPosition, axisVector, axisNDC))
        {
            const float scalar = ProjectMouseDeltaOntoHandle(mouseNDC - m_DragStartMouseNDC, axisNDC);
            return 1.0f + scalar;
        }
    }

    const glm::vec3 projected = ProjectMouseToAxis(mouseNDC, camera, axis, anchoredHandleScale);
    const glm::vec3 dir = glm::normalize(GetAxisDirection(axis));
    const float dragDistance = glm::dot(projected - m_DragStart, dir);
    const float normalizedDrag = dragDistance / std::max(anchoredHandleScale, 1e-4f);
    return 1.0f + normalizedDrag;
}

float TransformGizmo::ApplySnap(float value, float snapIncrement) const
{
    if (!m_Config.SnapEnabled || snapIncrement < 1e-6f) return value;
    return std::round(value / snapIncrement) * snapIncrement;
}

void TransformGizmo::DrawTranslateGizmo(DebugDraw& dd, float handleScale) const
{
    const glm::vec3 o = m_PivotPosition;

    // Draw axis arrows.
    auto drawAxisArrow = [&](GizmoAxis axis)
    {
        const glm::vec3 dir = GetAxisDirection(axis);
        const glm::vec3 end = o + dir * handleScale;
        const uint32_t color = GetAxisColor(axis);
        dd.OverlayLine(o, end, color);

        // Arrowhead
        const float headSize = handleScale * 0.08f;
        dd.OverlayLine(end, end - dir * headSize + glm::cross(dir, glm::vec3(0.01f, 0.01f, 0.01f)) * headSize * 3.0f, color);
    };

    drawAxisArrow(GizmoAxis::X);
    drawAxisArrow(GizmoAxis::Y);
    drawAxisArrow(GizmoAxis::Z);

    // Draw plane handles (small filled quads).
    auto drawPlaneHandle = [&](GizmoAxis planeAxis, GizmoAxis uAxis, GizmoAxis vAxis)
    {
        const glm::vec3 u = GetAxisDirection(uAxis);
        const glm::vec3 v = GetAxisDirection(vAxis);
        const float sz = handleScale * 0.3f;
        const uint32_t color = GetAxisColor(planeAxis);

        const glm::vec3 p0 = o + u * sz * 0.15f + v * sz * 0.15f;
        const glm::vec3 p1 = o + u * sz + v * sz * 0.15f;
        const glm::vec3 p2 = o + u * sz + v * sz;
        const glm::vec3 p3 = o + u * sz * 0.15f + v * sz;

        dd.OverlayLine(p0, p1, color);
        dd.OverlayLine(p1, p2, color);
        dd.OverlayLine(p2, p3, color);
        dd.OverlayLine(p3, p0, color);
    };

    drawPlaneHandle(GizmoAxis::XY, GizmoAxis::X, GizmoAxis::Y);
    drawPlaneHandle(GizmoAxis::XZ, GizmoAxis::X, GizmoAxis::Z);
    drawPlaneHandle(GizmoAxis::YZ, GizmoAxis::Y, GizmoAxis::Z);
}

void TransformGizmo::DrawRotateGizmo(DebugDraw& dd, float handleScale) const
{
    const glm::vec3 o = m_PivotPosition;
    constexpr int segments = 48;

    auto drawCircle = [&](GizmoAxis axis)
    {
        const uint32_t color = GetAxisColor(axis);

        // Build perpendicular basis.
        glm::vec3 u, v;
        if (axis == GizmoAxis::X)
        {
            u = GetAxisDirection(GizmoAxis::Y);
            v = GetAxisDirection(GizmoAxis::Z);
        }
        else if (axis == GizmoAxis::Y)
        {
            u = GetAxisDirection(GizmoAxis::Z);
            v = GetAxisDirection(GizmoAxis::X);
        }
        else
        {
            u = GetAxisDirection(GizmoAxis::X);
            v = GetAxisDirection(GizmoAxis::Y);
        }

        for (int i = 0; i < segments; ++i)
        {
            const float a0 = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
            const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * glm::two_pi<float>();

            const glm::vec3 p0 = o + (u * std::cos(a0) + v * std::sin(a0)) * handleScale;
            const glm::vec3 p1 = o + (u * std::cos(a1) + v * std::sin(a1)) * handleScale;

            dd.OverlayLine(p0, p1, color);
        }
    };

    drawCircle(GizmoAxis::X);
    drawCircle(GizmoAxis::Y);
    drawCircle(GizmoAxis::Z);
}

void TransformGizmo::DrawScaleGizmo(DebugDraw& dd, float handleScale) const
{
    const glm::vec3 o = m_PivotPosition;

    // Draw axis lines with cube ends.
    auto drawAxisScale = [&](GizmoAxis axis)
    {
        const glm::vec3 dir = GetAxisDirection(axis);
        const glm::vec3 end = o + dir * handleScale;
        const uint32_t color = GetAxisColor(axis);
        dd.OverlayLine(o, end, color);

        // Small cube at the end (cross-hair).
        const float cubeSize = handleScale * 0.04f;
        dd.OverlayBox(end - glm::vec3(cubeSize), end + glm::vec3(cubeSize), color);
    };

    drawAxisScale(GizmoAxis::X);
    drawAxisScale(GizmoAxis::Y);
    drawAxisScale(GizmoAxis::Z);

    // Center cube for uniform scale.
    const float centerSize = handleScale * 0.06f;
    const uint32_t centerColor = GetAxisColor(GizmoAxis::All);
    dd.OverlayBox(o - glm::vec3(centerSize), o + glm::vec3(centerSize), centerColor);
}

bool TransformGizmo::Update(entt::registry& registry,
                             DebugDraw& debugDraw,
                             const CameraComponent& camera,
                             const Core::Input::Context& input,
                             uint32_t viewportWidth,
                             uint32_t viewportHeight,
                             bool uiCapturesMouse)
{
    // Early exit if no selected entities with transforms.
    if (!ComputePivot(registry, m_State != GizmoState::Active))
    {
        m_State = GizmoState::Idle;
        m_HoveredAxis = GizmoAxis::None;
        m_ActiveAxis = GizmoAxis::None;
        m_CachedTransforms.clear();
        return false;
    }

    const float handleScale = ComputeHandleScale(camera);

    // Convert mouse position to NDC (-1..1).
    const glm::vec2 mousePos = input.GetMousePosition();
    glm::vec2 mouseNDC{0.0f};
    if (viewportWidth > 0 && viewportHeight > 0)
    {
        mouseNDC.x = (mousePos.x / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
        mouseNDC.y = 1.0f - (mousePos.y / static_cast<float>(viewportHeight)) * 2.0f; // Flip Y
    }

    const bool mouseDown = input.IsMouseButtonPressed(m_Config.MouseButton);
    const bool mouseJustPressed = input.IsMouseButtonJustPressed(m_Config.MouseButton);

    bool consumed = false;

    switch (m_State)
    {
    case GizmoState::Idle:
    case GizmoState::Hovered:
    {
        if (!uiCapturesMouse)
        {
            m_HoveredAxis = HitTest(mouseNDC, camera, handleScale);
            m_State = (m_HoveredAxis != GizmoAxis::None) ? GizmoState::Hovered : GizmoState::Idle;

            if (m_State == GizmoState::Hovered && mouseJustPressed)
            {
                m_ActiveAxis = m_HoveredAxis;
                m_State = GizmoState::Active;
                m_DragStartMouseNDC = mouseNDC;

                // Cache transforms and drag anchor at drag start.
                if (!ComputePivot(registry, true))
                {
                    m_State = GizmoState::Idle;
                    m_ActiveAxis = GizmoAxis::None;
                    break;
                }
                m_DragPivotPosition = m_PivotPosition;
                m_DragPivotRotation = m_PivotRotation;
                m_DragHandleScale = handleScale;

                if (m_Config.Mode == GizmoMode::Translate)
                    m_DragStart = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                else if (m_Config.Mode == GizmoMode::Rotate)
                    m_InitialRotateAngle = ProjectMouseToRotation(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                else if (m_Config.Mode == GizmoMode::Scale)
                {
                    m_DragStart = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                }

                consumed = true;
            }
        }
        else
        {
            m_HoveredAxis = GizmoAxis::None;
            m_State = GizmoState::Idle;
        }
        break;
    }

    case GizmoState::Active:
    {
        consumed = true;

        if (!mouseDown)
        {
            // Release: finish dragging.
            m_State = GizmoState::Idle;
            m_ActiveAxis = GizmoAxis::None;
            break;
        }

        // Apply transform based on mode.
        const auto restoreInitialLocal = [](ECS::Components::Transform::Component& localTransform,
                                            const EntityTransformCache& cache)
        {
            localTransform.Position = cache.InitialPosition;
            localTransform.Rotation = cache.InitialRotation;
            localTransform.Scale = cache.InitialScale;
        };
        const auto applyTargetWorldTransform = [&](ECS::Components::Transform::Component& localTransform,
                                                   const EntityTransformCache& cache,
                                                   const ECS::Components::Transform::Component& targetWorldTransform)
        {
            if (!cache.HasInitialParentWorldMatrix)
            {
                localTransform = targetWorldTransform;
                return true;
            }

            ECS::Components::Transform::Component solvedLocalTransform{};
            if (!ECS::Components::Transform::TryComputeLocalTransform(
                    ECS::Components::Transform::GetMatrix(targetWorldTransform),
                    cache.InitialParentWorldMatrix,
                    solvedLocalTransform))
            {
                restoreInitialLocal(localTransform, cache);
                return false;
            }

            localTransform = solvedLocalTransform;
            return true;
        };

        if (m_Config.Mode == GizmoMode::Translate)
        {
            glm::vec3 delta{0.0f};
            const glm::vec2 mouseDeltaNDC = mouseNDC - m_DragStartMouseNDC;

            if (m_ActiveAxis == GizmoAxis::X || m_ActiveAxis == GizmoAxis::Y || m_ActiveAxis == GizmoAxis::Z)
            {
                const glm::vec3 axisDir = glm::normalize(GetAxisDirection(m_ActiveAxis));
                glm::vec2 axisNDC{0.0f};
                if (ProjectWorldVectorToScreen(camera, m_DragPivotPosition, axisDir * m_DragHandleScale, axisNDC))
                {
                    const float scalar = ProjectMouseDeltaOntoHandle(mouseDeltaNDC, axisNDC);
                    delta = axisDir * (scalar * m_DragHandleScale);
                }
                else
                {
                    const glm::vec3 currentPoint = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                    delta = currentPoint - m_DragStart;
                }
            }
            else
            {
                GizmoAxis uAxis = GizmoAxis::X;
                GizmoAxis vAxis = GizmoAxis::Y;
                switch (m_ActiveAxis)
                {
                case GizmoAxis::XY: uAxis = GizmoAxis::X; vAxis = GizmoAxis::Y; break;
                case GizmoAxis::XZ: uAxis = GizmoAxis::X; vAxis = GizmoAxis::Z; break;
                case GizmoAxis::YZ: uAxis = GizmoAxis::Y; vAxis = GizmoAxis::Z; break;
                default: break;
                }

                const glm::vec3 uDir = glm::normalize(GetAxisDirection(uAxis));
                const glm::vec3 vDir = glm::normalize(GetAxisDirection(vAxis));
                glm::vec2 uNDC{0.0f};
                glm::vec2 vNDC{0.0f};
                glm::vec2 coeffs{0.0f};
                if (ProjectWorldVectorToScreen(camera, m_DragPivotPosition, uDir * m_DragHandleScale, uNDC) &&
                    ProjectWorldVectorToScreen(camera, m_DragPivotPosition, vDir * m_DragHandleScale, vNDC) &&
                    SolvePlaneDragDelta(mouseDeltaNDC, uNDC, vNDC, coeffs))
                {
                    delta = uDir * (coeffs.x * m_DragHandleScale) + vDir * (coeffs.y * m_DragHandleScale);
                }
                else
                {
                    const glm::vec3 currentPoint = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                    delta = currentPoint - m_DragStart;
                }
            }

            // Apply snap per-axis.
            if (m_Config.SnapEnabled)
            {
                delta.x = ApplySnap(delta.x, m_Config.TranslateSnap);
                delta.y = ApplySnap(delta.y, m_Config.TranslateSnap);
                delta.z = ApplySnap(delta.z, m_Config.TranslateSnap);
            }

            for (auto& cache : m_CachedTransforms)
            {
                if (!registry.valid(cache.Entity)) continue;
                auto* transform = registry.try_get<ECS::Components::Transform::Component>(cache.Entity);
                if (!transform) continue;

                ECS::Components::Transform::Component targetWorldTransform;
                targetWorldTransform.Position = cache.InitialWorldPosition + delta;
                targetWorldTransform.Rotation = cache.InitialWorldRotation;
                targetWorldTransform.Scale = cache.InitialWorldScale;
                if (applyTargetWorldTransform(*transform, cache, targetWorldTransform))
                    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
            }
        }
        else if (m_Config.Mode == GizmoMode::Rotate)
        {
            float deltaAngle = ComputeScreenRotationDelta(
                camera,
                m_DragPivotPosition,
                GetAxisDirection(m_ActiveAxis),
                m_DragStartMouseNDC,
                mouseNDC);

            if (!std::isfinite(deltaAngle))
            {
                const float currentAngle = ProjectMouseToRotation(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);
                deltaAngle = WrapAngleDelta(currentAngle - m_InitialRotateAngle);
            }

            if (m_Config.SnapEnabled)
                deltaAngle = glm::radians(ApplySnap(glm::degrees(deltaAngle), m_Config.RotateSnap));

            const glm::quat rotation = glm::angleAxis(deltaAngle, GetAxisDirection(m_ActiveAxis));

            for (auto& cache : m_CachedTransforms)
            {
                if (!registry.valid(cache.Entity)) continue;
                auto* transform = registry.try_get<ECS::Components::Transform::Component>(cache.Entity);
                if (!transform) continue;

                ECS::Components::Transform::Component targetWorldTransform;
                const glm::vec3 offset = cache.InitialWorldPosition - m_DragPivotPosition;
                targetWorldTransform.Position = m_DragPivotPosition + rotation * offset;
                targetWorldTransform.Rotation = glm::normalize(rotation * cache.InitialWorldRotation);
                targetWorldTransform.Scale = cache.InitialWorldScale;
                if (applyTargetWorldTransform(*transform, cache, targetWorldTransform))
                    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
            }
        }
        else if (m_Config.Mode == GizmoMode::Scale)
        {
            float scaleFactor = ProjectMouseToScale(mouseNDC, camera, m_ActiveAxis, m_DragHandleScale);

            if (m_Config.SnapEnabled)
                scaleFactor = ApplySnap(scaleFactor, m_Config.ScaleSnap);

            // Clamp to prevent zero/negative scale.
            scaleFactor = std::max(scaleFactor, 0.01f);

            for (auto& cache : m_CachedTransforms)
            {
                if (!registry.valid(cache.Entity)) continue;
                auto* transform = registry.try_get<ECS::Components::Transform::Component>(cache.Entity);
                if (!transform) continue;

                if (m_ActiveAxis == GizmoAxis::All)
                {
                    transform->Scale = cache.InitialScale * scaleFactor;
                }
                else
                {
                    transform->Scale = cache.InitialScale;
                    if (m_ActiveAxis == GizmoAxis::X)
                        transform->Scale.x = cache.InitialScale.x * scaleFactor;
                    else if (m_ActiveAxis == GizmoAxis::Y)
                        transform->Scale.y = cache.InitialScale.y * scaleFactor;
                    else if (m_ActiveAxis == GizmoAxis::Z)
                        transform->Scale.z = cache.InitialScale.z * scaleFactor;
                }

                registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
            }
        }
        break;
    }
    }

    // Draw gizmo.
    switch (m_Config.Mode)
    {
    case GizmoMode::Translate: DrawTranslateGizmo(debugDraw, handleScale); break;
    case GizmoMode::Rotate:    DrawRotateGizmo(debugDraw, handleScale); break;
    case GizmoMode::Scale:     DrawScaleGizmo(debugDraw, handleScale); break;
    }

    return consumed;
}

} // namespace Graphics
