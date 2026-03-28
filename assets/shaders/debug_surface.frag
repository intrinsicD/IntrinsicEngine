// debug_surface.frag — Lightweight fragment shader for transient debug triangles.
// Simple diffuse + ambient lighting with alpha pass-through for semi-transparent fills.
#version 460

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(camera.lightDirAndIntensity.xyz);
    float NdotL = max(dot(N, L), 0.0);

    vec3 ambient = camera.ambientColorAndIntensity.xyz * camera.ambientColorAndIntensity.w;
    vec3 diffuse = camera.lightColor.xyz * camera.lightDirAndIntensity.w * NdotL;

    outColor = vec4(fragColor.rgb * (ambient + diffuse), fragColor.a);
}
