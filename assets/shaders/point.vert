#version 460
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

struct PointData {
    vec4  PosSize;      // .xyz = Position, .w = Size
    vec4  NormCol;      // .xyz = Normal,   .w = bits of Color
};

layout(std430, set = 1, binding = 0) readonly buffer PointBuffer {
    PointData points[];
} pointCloud;

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    PointData ptData = pointCloud.points[pointIndex];
    vec3 ptPosition = ptData.PosSize.xyz;
    float ptSize = ptData.PosSize.w;
    uint ptColor = floatBitsToUint(ptData.NormCol.w);

    fragColor = unpackUnorm4x8(ptColor);

    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    float radiusWorld = ptSize * push.SizeMultiplier;
    vec4 viewPos = camera.view * vec4(ptPosition, 1.0);
    vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;
    gl_Position = camera.proj * vec4(cornerView, 1.0);
}
