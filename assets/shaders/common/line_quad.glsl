// The screen-space quad of one edge line. The visible line draw and the edge pick both
// use it, so a click hits exactly the pixels a line covers (BUG-254).
#ifndef INTRINSIC_LINE_QUAD_GLSL
#define INTRINSIC_LINE_QUAD_GLSL

struct GpuLineQuadCorner {
    vec4 ClipPosition;
    uint SegmentIndex;
    uint EndpointVertex; // local vertex index of the endpoint this corner belongs to
};

// `quadVertex` is gl_VertexIndex of a `LineQuads` draw: six vertices per segment.
GpuLineQuadCorner GpuLineQuadCornerAt(GpuSceneTable scene,
                                      GpuInstanceDynamic dyn,
                                      GpuGeometryRecord geo,
                                      GpuEntityConfig cfg,
                                      uint quadVertex)
{
    GpuUIntBufferRef indices = GpuUIntBufferRef(geo.IndexBufferBDA);

    const uint firstLineQuadVertex = (geo.LineFirstIndex / 2u) * 6u;
    const uint vertexIndex = quadVertex - firstLineQuadVertex;
    const uint segmentIndex = vertexIndex / 6u;
    const uint vertexInQuad = vertexIndex % 6u;
    const uint cornerIndex = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[vertexInQuad];
    const uint lineIndexBase = geo.LineFirstIndex + segmentIndex * 2u;
    const uint localEndpointA = indices.Data[lineIndexBase];
    const uint localEndpointB = indices.Data[lineIndexBase + 1u];
    const vec3 a = GpuReadPackedVec3(geo.VertexBufferBDA, localEndpointA);
    const vec3 b = GpuReadPackedVec3(geo.VertexBufferBDA, localEndpointB);

    const vec4 clipA = scene.CameraViewProj * dyn.Model * vec4(a, 1.0);
    const vec4 clipB = scene.CameraViewProj * dyn.Model * vec4(b, 1.0);
    const bool useEnd = cornerIndex >= 2u;
    const float side = (cornerIndex == 0u || cornerIndex == 3u) ? -1.0 : 1.0;
    const vec4 centerClip = useEnd ? clipB : clipA;

    const vec2 ndcA = clipA.xy / max(abs(clipA.w), 1.0e-6);
    const vec2 ndcB = clipB.xy / max(abs(clipB.w), 1.0e-6);
    const vec2 direction = ndcB - ndcA;
    const float directionLength = length(direction);
    const vec2 tangent = directionLength > 1.0e-6 ? direction / directionLength : vec2(1.0, 0.0);
    const vec2 normal = vec2(-tangent.y, tangent.x);
    const vec2 viewport = max(vec2(scene.CameraViewportWidth, scene.CameraViewportHeight), vec2(1.0));
    float lineWidthPx = cfg.Line.LineWidth;
    if (cfg.Line.LineWidthBDA != uint64_t(0)) {
        lineWidthPx = GpuFloatBufferRef(cfg.Line.LineWidthBDA).Data[segmentIndex];
    }
    const float halfWidthPx = clamp(lineWidthPx, 0.5, 32.0) * 0.5;
    const vec2 clipOffset = normal * side * vec2((2.0 * halfWidthPx) / viewport.x,
                                                 (2.0 * halfWidthPx) / viewport.y) * centerClip.w;

    GpuLineQuadCorner corner;
    corner.ClipPosition = centerClip + vec4(clipOffset, 0.0, 0.0);
    corner.SegmentIndex = segmentIndex;
    corner.EndpointVertex = useEnd ? localEndpointB : localEndpointA;
    return corner;
}

#endif
