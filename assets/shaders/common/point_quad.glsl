// The screen-space quad of one point and its coverage and depth. The visible point draw and
// the point pick both use it, so a click hits exactly the pixels a point covers (BUG-254).
#ifndef INTRINSIC_POINT_QUAD_GLSL
#define INTRINSIC_POINT_QUAD_GLSL

#include "point_sphere_impostor.glsl"

const uint GpuPointMode_Sphere = 1u;
const uint GpuPointMode_Surfel = 2u;

struct GpuPointQuadCorner {
    vec4 ClipPosition;
    uint PointIndex;
    vec2 DiscUV;
    vec3 ViewCenter;
    float ViewRadius;
    vec3 ViewNormal;
};

float GpuPointSizePx(GpuEntityConfig cfg, uint pointIndex)
{
    float pointSizePx = cfg.Point.PointSize;
    if (cfg.Point.PointSizeBDA != uint64_t(0)) {
        pointSizePx = GpuFloatBufferRef(cfg.Point.PointSizeBDA).Data[pointIndex];
    }
    return clamp(pointSizePx, 0.5, 32.0);
}

// `quadVertex` is gl_VertexIndex of a point-quad draw: six vertices per point.
GpuPointQuadCorner GpuPointQuadCornerAt(GpuSceneTable scene,
                                        GpuInstanceDynamic dyn,
                                        GpuGeometryRecord geo,
                                        GpuEntityConfig cfg,
                                        uint quadVertex)
{
    const uint firstPointQuadVertex = geo.PointFirstVertex * 6u;
    const uint vertexIndex = quadVertex - firstPointQuadVertex;
    const uint sourceVertexIndex = vertexIndex / 6u;
    const uint vertexInQuad = vertexIndex % 6u;
    const vec3 pointPosition = GpuReadPackedVec3(geo.VertexBufferBDA, sourceVertexIndex);

    const uint cornerIndex = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[vertexInQuad];
    const vec2 localOffset = vec2[4](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0))[cornerIndex];

    const vec4 localPos = vec4(pointPosition, 1.0);
    const vec4 worldPos = dyn.Model * localPos;
    const vec4 viewCenter4 = scene.CameraView * worldPos;
    const vec4 centerClip = scene.CameraViewProj * dyn.Model * localPos;

    const float pointSizePx = GpuPointSizePx(cfg, sourceVertexIndex);
    const float radiusPx = pointSizePx * 0.5;
    const float viewportWidth = max(scene.CameraViewportWidth, 1.0);
    const float viewportHeight = max(scene.CameraViewportHeight, 1.0);
    const vec2 clipOffset = localOffset *
        vec2((2.0 * radiusPx) / viewportWidth, (2.0 * radiusPx) / viewportHeight) *
        centerClip.w;

    const float projectionScaleY = abs(scene.CameraProj[1][1]);
    const float depth = max(abs(viewCenter4.z), 1.0e-4);
    const float ndcRadiusY = (2.0 * radiusPx) / viewportHeight;

    GpuPointQuadCorner corner;
    corner.ClipPosition = centerClip + vec4(clipOffset, 0.0, 0.0);
    corner.PointIndex = sourceVertexIndex;
    corner.DiscUV = localOffset;
    corner.ViewCenter = viewCenter4.xyz;
    corner.ViewRadius = projectionScaleY > 1.0e-6
        ? max((ndcRadiusY * depth) / projectionScaleY, 1.0e-5)
        : 1.0e-3;
    corner.ViewNormal = vec3(0.0, 0.0, 1.0);
    // UI-083: only a Surfel is oriented by its normal.
    if (cfg.Point.PointMode == GpuPointMode_Surfel && geo.NormalBufferBDA != uint64_t(0)) {
        const vec3 n = GpuReadPackedVec3(geo.NormalBufferBDA, sourceVertexIndex);
        // The cofactor matrix is the inverse transpose times the determinant: it keeps the normal
        // perpendicular under non-uniform scale and stays defined for a singular model matrix.
        // Its sign does not matter, a Surfel is two-sided.
        const mat3 m = mat3(dyn.Model);
        const vec3 viewNormal = mat3(scene.CameraView) *
            (cross(m[1], m[2]) * n.x + cross(m[2], m[0]) * n.y + cross(m[0], m[1]) * n.z);
        // Rescaling by the largest component keeps tiny and huge model scales finite; a zero
        // normal has no direction and faces the camera.
        const float largest = max(max(abs(viewNormal.x), abs(viewNormal.y)), abs(viewNormal.z));
        if (largest > 0.0 && !isinf(largest) && !any(isnan(viewNormal))) {
            corner.ViewNormal = normalize(viewNormal / largest);
        }
    }
    return corner;
}

float GpuPointDiscMetric(uint pointMode, vec3 viewNormal, vec2 uv)
{
    if (pointMode != GpuPointMode_Surfel) {
        return dot(uv, uv);
    }

    vec3 n = normalize(viewNormal);
    float nz = max(abs(n.z), 0.15);
    float lifted = dot(n.xy, uv) / nz;
    return dot(uv, uv) + lifted * lifted;
}

// Whether the point covers this fragment and the depth it writes there (`fragDepth` is
// gl_FragCoord.z). A sphere also shades `color`; `r2` is the disc metric.
bool GpuPointFragmentCoverage(uint pointMode, vec3 viewNormal, vec2 uv, vec3 viewCenter,
                              float viewRadius, uint64_t sceneTableBDA, float fragDepth,
                              inout vec3 color, out float r2, out float depth)
{
    r2 = GpuPointDiscMetric(pointMode, viewNormal, uv);
    depth = fragDepth;
    if (r2 > 1.0) {
        return false;
    }
    if (pointMode == GpuPointMode_Sphere) {
        const mat4 projection = GpuSceneTableRef(sceneTableBDA).Value.CameraProj;
        return ShadeSphereImpostor(uv, viewCenter, viewRadius, projection, color, depth);
    }
    return true;
}

#endif
