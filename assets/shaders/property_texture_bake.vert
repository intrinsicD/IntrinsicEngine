#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Property texture bake: Mode 0 rasterizes atlas triangles from an indexed
// draw; Mode 1 draws one texel-space rectangle per chart boundary edge,
// covering every texel centre within the Chebyshev padding distance.

layout(buffer_reference, scalar) readonly buffer TexcoordBuffer {
    vec2 value[];
};
layout(buffer_reference, scalar) readonly buffer PropertyBuffer {
    vec4 value[];
};
layout(buffer_reference, scalar) readonly buffer GutterEdgeBuffer {
    uvec4 value[];
};

layout(push_constant, scalar) uniform PushConstants {
    uint64_t TexcoordBDA;
    uint64_t PropertyBDA;
    uint64_t IndexBDA;
    uint64_t ChartBDA;
    uint64_t GutterEdgeBDA;
    uint Domain;
    uint ValueKind;
    uint Encoding;
    uint ColormapID;
    float RangeMin;
    float RangeMax;
    uint Mode;
    uint PaddingTexels;
    uint Width;
    uint Height;
} push;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragVertexValue;
layout(location = 2) flat out uint fragGutterEdge;

const uint ModeGutter = 1u;
const uint kQuadCorner[6] = uint[6](0u, 1u, 2u, 2u, 1u, 3u);

void main()
{
    TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
    vec2 uv;
    if (push.Mode == ModeGutter)
    {
        GutterEdgeBuffer edges = GutterEdgeBuffer(push.GutterEdgeBDA);
        const uint edgeIndex = uint(gl_VertexIndex) / 6u;
        const uint corner = kQuadCorner[uint(gl_VertexIndex) % 6u];
        const uvec4 edge = edges.value[edgeIndex];
        const vec2 extent = vec2(float(push.Width), float(push.Height));
        const vec2 a = texcoords.value[edge.x] * extent;
        const vec2 b = texcoords.value[edge.y] * extent;
        // The Chebyshev neighbourhood of a segment is exactly its bounding box
        // grown by the radius; a quarter texel keeps boundary centres inside.
        const float grow = float(push.PaddingTexels) + 0.25;
        const vec2 lo = min(a, b) - vec2(grow);
        const vec2 hi = max(a, b) + vec2(grow);
        const vec2 texel = vec2(
            (corner & 1u) != 0u ? hi.x : lo.x,
            (corner & 2u) != 0u ? hi.y : lo.y);
        uv = texel / extent;
        fragVertexValue = vec4(0.0);
        fragGutterEdge = edgeIndex;
    }
    else
    {
        PropertyBuffer properties = PropertyBuffer(push.PropertyBDA);
        uv = texcoords.value[gl_VertexIndex];
        fragVertexValue = push.Domain == 0u
            ? properties.value[gl_VertexIndex]
            : vec4(0.0);
        // A common power-of-two scale preserves magnitude-weighted
        // interpolation while leaving headroom for finite near-FLT_MAX
        // direction components. The fragment normal encoder uses the
        // correspondingly scaled cutoff.
        if (push.Domain == 0u && push.Encoding == 1u)
            fragVertexValue.xyz *= 0.125;
        fragGutterEdge = 0u;
    }
    fragUv = uv;

    const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
