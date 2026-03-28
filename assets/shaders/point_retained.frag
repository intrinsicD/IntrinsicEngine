// point_retained.frag — Point cloud fragment shader for retained-mode BDA points.
//
// Push constant layout matches point_retained.vert. The fragment shader only
// reads RenderMode for mode-dependent shading.
//
// Render modes:
//   0 = FlatDisc:  Unlit circular disc with anti-aliased edge.
//   1 = Surfel:    Normal-oriented disc with Lambertian + ambient lighting.
//   2 = EWA:       Elliptical Gaussian splat with Lambertian shading (Zwicker et al. 2001).

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

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

// Push constant layout must match point_retained.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    float    PointSize;
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     RenderMode;       // 0 = FlatDisc, 1 = Surfel, 2 = EWA
    uint     Color;
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    if (push.RenderMode == 2u)
    {
        // ---- EWA mode: Gaussian splat evaluation ----
        // Evaluate the elliptical Gaussian weight from the UV-space inverse covariance.
        // weight = exp(-0.5 * uv^T * Q * uv) where Q is the pre-scaled inverse covariance.
        vec2 uv = fragDiscUV;
        float mahal = fragEwaCovInv.x * uv.x * uv.x
                    + 2.0 * fragEwaCovInv.y * uv.x * uv.y
                    + fragEwaCovInv.z * uv.y * uv.y;

        // Cutoff: discard fragments beyond ~3 sigma (exp(-4.5) ~ 0.011).
        if (mahal > 9.0) discard;

        float weight = exp(-0.5 * mahal);

        // Lambertian + ambient lighting (same as Surfel mode).
        // Epsilon-guarded renormalization with camera-facing fallback.
        float nLenE = length(fragNormal);
        vec3 N = (nLenE > 1e-6) ? (fragNormal / nLenE) : vec3(0.0, 0.0, 1.0);
        vec3 lightDir = normalize(camera.lightDirAndIntensity.xyz);
        float NdotL = dot(N, lightDir);
        float diffuse = max(abs(NdotL), 0.0) * camera.lightDirAndIntensity.w;

        float ambient = camera.ambientColorAndIntensity.w;
        vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

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
            float nLenS = length(fragNormal);
            vec3 N = (nLenS > 1e-6) ? (fragNormal / nLenS) : vec3(0.0, 0.0, 1.0);
            vec3 lightDir = normalize(camera.lightDirAndIntensity.xyz);

            // Two-sided lighting: flip normal if facing away from light.
            float NdotL = dot(N, lightDir);
            float diffuse = max(abs(NdotL), 0.0) * camera.lightDirAndIntensity.w;

            float ambient = camera.ambientColorAndIntensity.w;
            vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

            outColor = vec4(lit, fragColor.a * alpha);
        }
        else
        {
            // ---- FlatDisc mode: unlit ----
            outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        }
    }
}
