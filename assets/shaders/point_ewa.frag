// point_ewa.frag — EWA splatting fragment shader: elliptical Gaussian weight.
//
// Part of the PointPass pipeline family (PLAN.md Phase 4).
// Evaluates the EWA Gaussian weight from the UV-space inverse covariance
// and applies Lambertian + ambient lighting (Zwicker et al. 2001).

#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

layout(location = 0) out vec4 outColor;

void main()
{
    // Evaluate the elliptical Gaussian weight.
    // weight = exp(-0.5 * uv^T * Q * uv) where Q is the pre-scaled inverse covariance.
    vec2 uv = fragDiscUV;
    float mahal = fragEwaCovInv.x * uv.x * uv.x
                + 2.0 * fragEwaCovInv.y * uv.x * uv.y
                + fragEwaCovInv.z * uv.y * uv.y;

    // Cutoff: discard fragments beyond ~3 sigma (exp(-4.5) ~ 0.011).
    if (mahal > 9.0) discard;

    float weight = exp(-0.5 * mahal);

    // Lambertian + ambient lighting.
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = dot(N, lightDir);
    float diffuse = max(abs(NdotL), 0.0);

    float ambient = 0.15;
    vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

    outColor = vec4(lit, fragColor.a * weight);
}
