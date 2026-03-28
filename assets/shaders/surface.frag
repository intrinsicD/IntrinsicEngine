//surface.frag
#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTexID;
layout(location = 3) in vec4 fragVertexColor;
layout(location = 4) in vec3 fragObjectPos;

layout(location = 0) out vec4 outColor;

// Per-face color buffer (optional BDA — when PtrFaceAttr != 0).
layout(buffer_reference, scalar) readonly buffer FaceAttrBuf { uint color[]; };

// Index buffer (optional BDA — when PtrIndices != 0).
layout(buffer_reference, scalar) readonly buffer IndexBuf { uint idx[]; };

// Position buffer for nearest-vertex lookup.
layout(buffer_reference, scalar) readonly buffer PosBuf { vec3 v[]; };

// Per-vertex color buffer for nearest-vertex lookup.
layout(buffer_reference, scalar) readonly buffer VertexAttrBuf { uint color[]; };

// Push constant layout must match surface.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    uint     VisibilityBase;
    float    PointSizePx;
    uint64_t PtrFaceAttr;
    uint64_t PtrVertexAttr;
    uint64_t PtrIndices;
    uint64_t PtrCentroids;
} push;

// Binding 0 = Camera + Lighting (UBO), Binding 1 = Bindless Array
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

#include "surface_color_resolve.glsl"

void main() {
    vec3 lightDir = normalize(camera.lightDirAndIntensity.xyz);
    float lightIntensity = camera.lightDirAndIntensity.w;
    vec3 lColor = camera.lightColor.xyz * lightIntensity;
    float ambientStrength = camera.ambientColorAndIntensity.w;
    vec3 ambient = ambientStrength * camera.ambientColorAndIntensity.xyz;

    // Epsilon-guarded renormalization: interpolation across a triangle can
    // produce near-zero normals when adjacent vertices have opposing directions.
    float nLen = length(fragNormal);
    vec3 norm = (nLen > 1e-6) ? (fragNormal / nLen) : vec3(0.0, 0.0, 1.0);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lColor;

    vec4 baseColor = ResolveSurfaceBaseColor();

    vec3 result = (ambient + diffuse) * baseColor.rgb;

    outColor = vec4(result, baseColor.a);
}
