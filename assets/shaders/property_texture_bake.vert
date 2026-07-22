#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, scalar) readonly buffer TexcoordBuffer {
    vec2 value[];
};
layout(buffer_reference, scalar) readonly buffer PropertyBuffer {
    vec4 value[];
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

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragVertexValue;

void main()
{
    TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
    PropertyBuffer properties = PropertyBuffer(push.PropertyBDA);

    const vec2 uv = texcoords.value[gl_VertexIndex];
    fragUv = uv;
    fragVertexValue = push.Domain == 0u
        ? properties.value[gl_VertexIndex]
        : vec4(0.0);

    const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
