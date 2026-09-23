#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// Writes the encoded property value, the chart coverage id and a depth that
// orders interior data before gutters and nearer boundaries before farther
// ones. Graphics.PropertyTextureBake documents the shared raster contract and
// mirrors it in its CPU reference.

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(buffer_reference, scalar) readonly buffer TexcoordBuffer {
    vec2 value[];
};
layout(buffer_reference, scalar) readonly buffer PropertyBuffer {
    vec4 value[];
};
layout(buffer_reference, scalar) readonly buffer IndexBuffer {
    uint value[];
};
layout(buffer_reference, scalar) readonly buffer ChartBuffer {
    uint value[];
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

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragVertexValue;
layout(location = 2) flat in uint fragGutterEdge;
layout(location = 0) out vec4 outValue;
layout(location = 1) out float outCoverage;

const uint DomainVertex = 0u;
const uint DomainFace = 1u;
const uint DomainNearestEdge = 2u;
const uint ValueLabel = 1u;
const uint EncodingRaw = 0u;
const uint EncodingNormal = 1u;
const uint EncodingRgba = 2u;
const uint EncodingColormap = 3u;
const uint EncodingLabelPalette = 4u;
const uint EncodingLinearScalar = 5u;
const uint ModeGutter = 1u;

uvec3 TriangleIndices(uint primitive)
{
    IndexBuffer indices = IndexBuffer(push.IndexBDA);
    const uint first = primitive * 3u;
    return uvec3(
        indices.value[first + 0u],
        indices.value[first + 1u],
        indices.value[first + 2u]);
}

float SegmentDistanceSquared(vec2 point, vec2 a, vec2 b)
{
    const vec2 edge = b - a;
    const float lengthSquared = dot(edge, edge);
    const float t = lengthSquared > 1.0e-20
        ? clamp(dot(point - a, edge) / lengthSquared, 0.0, 1.0)
        : 0.0;
    const vec2 delta = point - (a + t * edge);
    return dot(delta, delta);
}

uint NearestTriangleEdge(uint primitive, vec2 uv)
{
    TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
    const uvec3 tri = TriangleIndices(primitive);
    const vec2 a = texcoords.value[tri.x];
    const vec2 b = texcoords.value[tri.y];
    const vec2 c = texcoords.value[tri.z];
    const float oppositeA = SegmentDistanceSquared(uv, b, c);
    const float oppositeB = SegmentDistanceSquared(uv, c, a);
    const float oppositeC = SegmentDistanceSquared(uv, a, b);
    if (oppositeA <= oppositeB && oppositeA <= oppositeC)
        return 0u;
    return oppositeB <= oppositeC ? 1u : 2u;
}

uint NearestTriangleVertex(uint primitive, vec2 uv)
{
    TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
    const uvec3 tri = TriangleIndices(primitive);
    const vec2 deltaA = uv - texcoords.value[tri.x];
    const vec2 deltaB = uv - texcoords.value[tri.y];
    const vec2 deltaC = uv - texcoords.value[tri.z];
    const float distanceA = dot(deltaA, deltaA);
    const float distanceB = dot(deltaB, deltaB);
    const float distanceC = dot(deltaC, deltaC);
    if (distanceA <= distanceB && distanceA <= distanceC)
        return 0u;
    return distanceB <= distanceC ? 1u : 2u;
}

vec4 ResolveInteriorValue(uint primitive)
{
    PropertyBuffer properties = PropertyBuffer(push.PropertyBDA);
    if (push.Domain == DomainFace)
        return properties.value[primitive];
    if (push.Domain == DomainNearestEdge)
    {
        const uint side = NearestTriangleEdge(primitive, fragUv);
        return properties.value[primitive * 3u + side];
    }
    if (push.ValueKind == ValueLabel)
    {
        const uvec3 tri = TriangleIndices(primitive);
        const uint corner = NearestTriangleVertex(primitive, fragUv);
        return properties.value[tri[corner]];
    }
    return fragVertexValue;
}

// Chebyshev distance from p to segment [a, b], evaluated at the same
// breakpoint candidates as the CPU reference.
float ChebyshevAt(vec2 o, vec2 d, float t)
{
    return max(abs(o.x + t * d.x), abs(o.y + t * d.y));
}

float ChebyshevCandidate(vec2 o, vec2 d, float numerator, float denominator,
                         float best)
{
    if (denominator == 0.0)
        return best;
    const float t = numerator / denominator;
    return t > 0.0 && t < 1.0 ? min(best, ChebyshevAt(o, d, t)) : best;
}

float ChebyshevSegmentDistance(vec2 p, vec2 a, vec2 b)
{
    const vec2 d = b - a;
    const vec2 o = a - p;
    float best = min(ChebyshevAt(o, d, 0.0), ChebyshevAt(o, d, 1.0));
    best = ChebyshevCandidate(o, d, -o.x, d.x, best);
    best = ChebyshevCandidate(o, d, -o.y, d.y, best);
    best = ChebyshevCandidate(o, d, o.y - o.x, d.x - d.y, best);
    best = ChebyshevCandidate(o, d, -(o.x + o.y), d.x + d.y, best);
    return best;
}

vec3 LabelColor(uint label)
{
    uint hash = label * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    return vec3(
        float((hash >> 0u) & 255u),
        float((hash >> 8u) & 255u),
        float((hash >> 16u) & 255u)) / 255.0;
}

vec4 EncodeValue(vec4 value)
{
    if (push.Encoding == EncodingRaw)
        return value;
    if (push.Encoding == EncodingNormal)
    {
        // Normalize finite directions without overflowing the squared length.
        const float scale = max(abs(value.x), max(abs(value.y), abs(value.z)));
        const vec3 scaled = value.xyz / max(scale, 1.0e-20);
        const float scaledLength = length(scaled);
        // Vertex directions were uniformly scaled before interpolation.
        const float cutoff = push.Domain == DomainVertex ? 1.25e-7 : 1.0e-6;
        const vec3 normal = scaledLength > 0.0 && scale > cutoff / scaledLength
            ? scaled / scaledLength
            : vec3(0.0, 0.0, 1.0);
        return vec4(normal * 0.5 + 0.5, 1.0);
    }
    if (push.Encoding == EncodingColormap)
    {
        const float t = (value.x - push.RangeMin) /
            (push.RangeMax - push.RangeMin);
        return texture(
            globalTextures[nonuniformEXT(push.ColormapID)],
            vec2(clamp(t, 0.0, 1.0), 0.5));
    }
    if (push.Encoding == EncodingLabelPalette)
        return vec4(LabelColor(floatBitsToUint(value.x)), 1.0);
    if (push.Encoding == EncodingLinearScalar)
    {
        const float t = (value.x - push.RangeMin) /
            (push.RangeMax - push.RangeMin);
        return vec4(clamp(t, 0.0, 1.0), 0.0, 0.0, 1.0);
    }
    return clamp(value, 0.0, 1.0);
}

void main()
{
    ChartBuffer charts = ChartBuffer(push.ChartBDA);
    if (push.Mode == ModeGutter)
    {
        GutterEdgeBuffer edges = GutterEdgeBuffer(push.GutterEdgeBDA);
        TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
        PropertyBuffer properties = PropertyBuffer(push.PropertyBDA);
        const uvec4 edge = edges.value[fragGutterEdge];
        const vec2 extent = vec2(float(push.Width), float(push.Height));
        const vec2 p = gl_FragCoord.xy;
        const vec2 a = texcoords.value[edge.x] * extent;
        const vec2 b = texcoords.value[edge.y] * extent;
        const float padding = float(push.PaddingTexels);
        const float distance = ChebyshevSegmentDistance(p, a, b);
        if (distance > padding)
            discard;

        const vec2 ab = b - a;
        const float lengthSquared = dot(ab, ab);
        const float t = lengthSquared > 0.0
            ? clamp(dot(p - a, ab) / lengthSquared, 0.0, 1.0)
            : 0.0;
        const float euclidean = length(p - (a + t * ab));
        vec4 value;
        if (push.Domain == DomainFace)
        {
            value = properties.value[edge.z];
        }
        else if (push.Domain == DomainNearestEdge)
        {
            value = properties.value[edge.z * 3u + edge.w];
        }
        else if (push.ValueKind == ValueLabel)
        {
            value = properties.value[t < 0.5 ? edge.x : edge.y];
        }
        else
        {
            vec4 va = properties.value[edge.x];
            vec4 vb = properties.value[edge.y];
            if (push.Encoding == EncodingNormal)
            {
                va.xyz *= 0.125;
                vb.xyz *= 0.125;
            }
            value = mix(va, vb, t);
        }
        outValue = EncodeValue(value);
        outCoverage = -(float(charts.value[edge.z]) + 1.0);
        // Chebyshev distance orders gutters; the Euclidean term only breaks
        // equal-distance ties toward the geometrically nearer edge.
        gl_FragDepth = (distance + euclidean / 64.0 + 1.0) /
            (padding + padding / 32.0 + 2.0);
        return;
    }

    const uint primitive = uint(gl_PrimitiveID);
    outValue = EncodeValue(ResolveInteriorValue(primitive));
    outCoverage = float(charts.value[primitive]) + 1.0;
    gl_FragDepth = 0.0;
}
