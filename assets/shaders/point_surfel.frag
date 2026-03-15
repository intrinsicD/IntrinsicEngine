// point_surfel.frag — Surfel and EWA point cloud fragment shader.
//
// Render modes (selected by push.Flags bit 1):
//   Surfel (default): Normal-oriented disc with Lambertian + ambient lighting.
//   EWA (Flags bit 1): Elliptical Gaussian splat with Lambertian shading.
//
// Part of the PointPass pipeline array in the three-pass rendering architecture.

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAttr;
    float    PointSize;
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;
    uint     Flags;             // bit 0: per-point colors, bit 1: EWA mode, bit 2: per-point radii
    uint64_t PtrRadii;          // per-point float radii (0 = uniform PointSize)
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    bool isEWA = (push.Flags & 2u) != 0u;

    if (isEWA)
    {
        // Check if the vertex shader fell back to isotropic FlatDisc due to
        // ill-conditioned covariance (fragEwaCovInv == 0).
        bool isIsotropicFallback = (fragEwaCovInv.x == 0.0 && fragEwaCovInv.y == 0.0 && fragEwaCovInv.z == 0.0);

        if (isIsotropicFallback)
        {
            // Render as flat disc (same as surfel mode disc test).
            float r2 = dot(fragDiscUV, fragDiscUV);
            if (r2 > 1.0) discard;
            float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

            float nLenF = length(fragNormal);
            vec3 N = (nLenF > 1e-6) ? (fragNormal / nLenF) : vec3(0.0, 0.0, 1.0);
            vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
            float diffuse = max(abs(dot(N, lightDir)), 0.0);
            float ambient = 0.15;
            vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);
            outColor = vec4(lit, fragColor.a * alpha);
        }
        else
        {
        // ---- EWA mode: Gaussian splat evaluation ----
        vec2 uv = fragDiscUV;
        float mahal = fragEwaCovInv.x * uv.x * uv.x
                    + 2.0 * fragEwaCovInv.y * uv.x * uv.y
                    + fragEwaCovInv.z * uv.y * uv.y;

        // Cutoff at ~3 sigma.
        if (mahal > 9.0) discard;

        float weight = exp(-0.5 * mahal);

        // Lambertian + ambient lighting.
        // Epsilon-guarded renormalization with camera-facing fallback.
        float nLenE = length(fragNormal);
        vec3 N = (nLenE > 1e-6) ? (fragNormal / nLenE) : vec3(0.0, 0.0, 1.0);
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
        float diffuse = max(abs(dot(N, lightDir)), 0.0);
        float ambient = 0.15;
        vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

        outColor = vec4(lit, fragColor.a * weight);
        }
    }
    else
    {
        // ---- Surfel mode: normal-oriented disc ----
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;

        float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

        // Epsilon-guarded renormalization with camera-facing fallback.
        float nLenS = length(fragNormal);
        vec3 N = (nLenS > 1e-6) ? (fragNormal / nLenS) : vec3(0.0, 0.0, 1.0);
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
        float diffuse = max(abs(dot(N, lightDir)), 0.0);
        float ambient = 0.15;
        vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

        outColor = vec4(lit, fragColor.a * alpha);
    }
}
