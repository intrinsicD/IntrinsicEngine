module;

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <entt/entity/registry.hpp>

module Graphics;
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

    // Project a world point to NDC.
    glm::vec2 WorldToNDC(const glm::vec3& p, const Graphics::CameraComponent& camera)
    {
        glm::vec4 clip = camera.ProjectionMatrix * camera.ViewMatrix * glm::vec4(p, 1.0f);
        if (std::abs(clip.w) < 1e-6f) return glm::vec2(0.0f);
        return glm::vec2(clip.x / clip.w, clip.y / clip.w);
    }

    // Closest point on a ray to a point in world space.
    glm::vec3 ClosestPointOnRayToPoint(const Ray& ray, const glm::vec3& point)
    {
        float t = glm::dot(point - ray.Origin, ray.Direction);
        t = std::max(t, 0.0f);
        return ray.Origin + ray.Direction * t;
    }

    // Ray-plane intersection. Returns distance along ray or -1 if parallel.
    float RayPlaneIntersect(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal)
    {
        const float denom = glm::dot(ray.Direction, planeNormal);
        if (std::abs(denom) < 1e-6f) return -1.0f;
        return glm::dot(planePoint - ray.Origin, planeNormal) / denom;
    }
}

namespace Graphics
{

bool TransformGizmo::ComputePivot(entt::registry& registry)
{
    auto view = registry.view<ECS::Components::Selection::SelectedTag,
                              ECS::Components::Transform::Component>();

    m_CachedTransforms.clear();

    bool first = true;
    glm::vec3 centroid{0.0f};
    int count = 0;

    for (auto [entity, sel, transform] : view.each())
    {
        EntityTransformCache cache;
        cache.Entity = entity;
        cache.InitialPosition = transform.Position;
        cache.InitialRotation = transform.Rotation;
        cache.InitialScale = transform.Scale;
        m_CachedTransforms.push_back(cache);

        centroid += transform.Position;
        ++count;

        if (first)
        {
            if (m_Config.Space == GizmoSpace::Local)
                m_PivotRotation = transform.Rotation;
            first = false;
        }
    }

    if (count == 0) return false;

    if (m_Config.Pivot == GizmoPivot::Centroid)
        m_PivotPosition = centroid / static_cast<float>(count);
    else if (!m_CachedTransforms.empty())
        m_PivotPosition = m_CachedTransforms[0].InitialPosition;

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

    return glm::rotate(m_PivotRotation, dir);
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
            const glm::vec3 center = m_PivotPosition + (u + v) * sz * 0.5f;

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
    const Ray ray = RayFromNDC(camera, mouseNDC);

    if (axis == GizmoAxis::X || axis == GizmoAxis::Y || axis == GizmoAxis::Z)
    {
        // Project onto the axis line.
        const glm::vec3 dir = GetAxisDirection(axis);
        const glm::vec3 w = ray.Origin - m_PivotPosition;
        const float a = glm::dot(dir, dir);
        const float b = glm::dot(dir, ray.Direction);
        const float c = glm::dot(ray.Direction, ray.Direction);
        const float d = glm::dot(dir, w);
        const float e = glm::dot(ray.Direction, w);

        const float det = a * c - b * b;
        if (std::abs(det) < 1e-10f) return m_PivotPosition;

        const float t = (b * e - c * d) / det;
        return m_PivotPosition + dir * t;
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
        default: return m_PivotPosition;
        }

        float t = RayPlaneIntersect(ray, m_PivotPosition, normal);
        if (t < 0.0f) return m_PivotPosition;
        return ray.Origin + ray.Direction * t;
    }
}

float TransformGizmo::ProjectMouseToRotation(const glm::vec2& mouseNDC,
                                              const CameraComponent& camera,
                                              GizmoAxis axis,
                                              float handleScale) const
{
    if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z)
        return 0.0f;

    const glm::vec3 normal = GetAxisDirection(axis);
    const Ray ray = RayFromNDC(camera, mouseNDC);
    float t = RayPlaneIntersect(ray, m_PivotPosition, normal);
    if (t < 0.0f) return 0.0f;

    const glm::vec3 hitPt = ray.Origin + ray.Direction * t - m_PivotPosition;
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
    if (axis == GizmoAxis::All)
    {
        // Uniform scale based on vertical mouse delta from pivot NDC.
        const glm::vec2 pivotNDC = WorldToNDC(m_PivotPosition, camera);
        return 1.0f + (mouseNDC.y - pivotNDC.y);
    }

    // Single-axis scale: project along axis.
    const glm::vec3 projected = ProjectMouseToAxis(mouseNDC, camera, axis, handleScale);
    const glm::vec3 dir = GetAxisDirection(axis);
    const float dist = glm::dot(projected - m_PivotPosition, dir);
    const float startDist = glm::dot(m_DragStart - m_PivotPosition, dir);

    if (std::abs(startDist) < 1e-6f) return 1.0f;
    return dist / startDist;
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
        const glm::vec3 normal = GetAxisDirection(axis);
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
    if (!ComputePivot(registry))
    {
        m_State = GizmoState::Idle;
        m_HoveredAxis = GizmoAxis::None;
        m_ActiveAxis = GizmoAxis::None;
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

                // Cache transforms at drag start.
                ComputePivot(registry);

                if (m_Config.Mode == GizmoMode::Translate)
                    m_DragStart = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, handleScale);
                else if (m_Config.Mode == GizmoMode::Rotate)
                    m_InitialRotateAngle = ProjectMouseToRotation(mouseNDC, camera, m_ActiveAxis, handleScale);
                else if (m_Config.Mode == GizmoMode::Scale)
                {
                    m_DragStart = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, handleScale);
                    for (auto& cache : m_CachedTransforms)
                        m_InitialScale = cache.InitialScale;
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
        if (m_Config.Mode == GizmoMode::Translate)
        {
            const glm::vec3 currentPoint = ProjectMouseToAxis(mouseNDC, camera, m_ActiveAxis, handleScale);
            glm::vec3 delta = currentPoint - m_DragStart;

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

                transform->Position = cache.InitialPosition + delta;
                registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
            }
        }
        else if (m_Config.Mode == GizmoMode::Rotate)
        {
            const float currentAngle = ProjectMouseToRotation(mouseNDC, camera, m_ActiveAxis, handleScale);
            float deltaAngle = currentAngle - m_InitialRotateAngle;

            if (m_Config.SnapEnabled)
                deltaAngle = ApplySnap(glm::degrees(deltaAngle), m_Config.RotateSnap);
            else
                deltaAngle = glm::degrees(deltaAngle);

            const float radians = glm::radians(deltaAngle);
            const glm::vec3 axis = GetAxisDirection(m_ActiveAxis);
            const glm::quat rotation = glm::angleAxis(radians, axis);

            for (auto& cache : m_CachedTransforms)
            {
                if (!registry.valid(cache.Entity)) continue;
                auto* transform = registry.try_get<ECS::Components::Transform::Component>(cache.Entity);
                if (!transform) continue;

                // Rotate around pivot.
                const glm::vec3 offset = cache.InitialPosition - m_PivotPosition;
                transform->Position = m_PivotPosition + rotation * offset;
                transform->Rotation = rotation * cache.InitialRotation;
                registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
            }
        }
        else if (m_Config.Mode == GizmoMode::Scale)
        {
            float scaleFactor = ProjectMouseToScale(mouseNDC, camera, m_ActiveAxis, handleScale);

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
                    const glm::vec3 dir = GetAxisDirection(m_ActiveAxis);
                    // Apply scale along the specific axis.
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
