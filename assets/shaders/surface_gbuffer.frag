//surface_gbuffer.frag — G-buffer output for deferred lighting path.
//
// Same vertex shader (surface.vert) and push constants as the forward path.
// Writes material properties to MRT targets instead of computing final lighting.
//
// MRT layout:
//   location 0 = SceneNormal  (RGBA16F) — world-space normal (xyz), unused (w)
//   location 1 = Albedo       (RGBA8)   — base color (rgb), alpha (a)
//   location 2 = Material0    (RGBA16F) — roughness (r), metallic (g), reserved (b,a)
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

// G-buffer MRT outputs.
layout(location = 0) out vec4 outNormal;    // SceneNormal
layout(location = 1) out vec4 outAlbedo;    // Albedo
layout(location = 2) out vec4 outMaterial;  // Material0

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

// Binding 0 = Camera (UBO), Binding 1 = Bindless Array
layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

#include "surface_color_resolve.glsl"

void main() {
    // Epsilon-guarded renormalization.
    float nLen = length(fragNormal);
    vec3 norm = (nLen > 1e-6) ? (fragNormal / nLen) : vec3(0.0, 0.0, 1.0);

    vec4 baseColor = ResolveSurfaceBaseColor();

    // Write G-buffer.
    outNormal   = vec4(norm, 0.0);
    outAlbedo   = baseColor;
    outMaterial  = vec4(0.5, 0.0, 0.0, 0.0); // Default roughness=0.5, metallic=0.0
}
