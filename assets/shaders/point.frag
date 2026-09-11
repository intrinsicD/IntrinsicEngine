// point.frag — Point cloud fragment shader with render mode support.
//
// Render modes (selected via push.RenderMode):
//   0 = FlatDisc:  Unlit circular disc with anti-aliased edge.
//   1 = Surfel:    Normal-oriented disc with Lambertian + ambient lighting.
//   2 = EWA:       Elliptical Gaussian splat with Lambertian shading (Zwicker et al. 2001).

#version 460
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;       // 0 = FlatDisc, 1 = Surfel, 2 = EWA
} push;

layout(location = 0) out vec4 outColor;

#include "common/point_splat.glsl"

void main()
{
    if (push.RenderMode == 2u)
    {
        // ---- EWA mode: Gaussian splat evaluation ----
        vec2 uv = fragDiscUV;
        float mahal = PointMahalanobis(fragEwaCovInv, uv);

        if (mahal > 9.0) discard;

        float weight = exp(-0.5 * mahal);

        // Epsilon-guarded renormalization with camera-facing fallback.
        vec3 lit = ShadePointLambert(fragColor.rgb, fragNormal,
                                     camera.lightDirAndIntensity, camera.ambientColorAndIntensity.w);

        outColor = vec4(lit, fragColor.a * weight);
    }
    else
    {
        // Disc test: discard fragments outside the unit circle.
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;

        // Anti-aliased edge via smoothstep.
        float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

        if (push.RenderMode == 1u)
        {
            // ---- Surfel mode: Lambertian + ambient lighting ----
            // Epsilon-guarded renormalization with camera-facing fallback.
            vec3 lit = ShadePointLambert(fragColor.rgb, fragNormal,
                                         camera.lightDirAndIntensity, camera.ambientColorAndIntensity.w);

            outColor = vec4(lit, fragColor.a * alpha);
        }
        else
        {
            // ---- FlatDisc mode: unlit ----
            outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        }
    }
}
