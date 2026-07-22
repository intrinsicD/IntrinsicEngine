#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

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

layout(push_constant, scalar) uniform PushConstants {
    uint64_t TexcoordBDA;
    uint64_t PropertyBDA;
    uint64_t IndexBDA;
    uint Domain;
    uint ValueKind;
    uint Encoding;
    uint ColormapID;
    float RangeMin;
    float RangeMax;
} push;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragVertexValue;
layout(location = 0) out vec4 outValue;

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

vec4 ResolvePropertyValue()
{
    PropertyBuffer properties = PropertyBuffer(push.PropertyBDA);
    const uint primitive = uint(gl_PrimitiveID);
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

void main()
{
    const vec4 value = ResolvePropertyValue();
    if (push.Encoding == EncodingRaw)
    {
        outValue = value;
        return;
    }
    if (push.Encoding == EncodingNormal)
    {
        const float lengthSquared = dot(value.xyz, value.xyz);
        const vec3 normal = lengthSquared > 1.0e-12
            ? value.xyz * inversesqrt(lengthSquared)
            : vec3(0.0, 0.0, 1.0);
        outValue = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (push.Encoding == EncodingColormap)
    {
        const float t = (value.x - push.RangeMin) /
            (push.RangeMax - push.RangeMin);
        outValue = texture(
            globalTextures[nonuniformEXT(push.ColormapID)],
            vec2(clamp(t, 0.0, 1.0), 0.5));
        return;
    }
    if (push.Encoding == EncodingLabelPalette)
    {
        outValue = vec4(LabelColor(floatBitsToUint(value.x)), 1.0);
        return;
    }
    if (push.Encoding == EncodingLinearScalar)
    {
        const float t = (value.x - push.RangeMin) /
            (push.RangeMax - push.RangeMin);
        outValue = vec4(clamp(t, 0.0, 1.0), 0.0, 0.0, 1.0);
        return;
    }
    outValue = clamp(value, 0.0, 1.0);
}
