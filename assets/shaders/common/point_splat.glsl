// Pure point/surfel projection and shading math; callers own mode and fallback policy.
#ifndef INTRINSIC_POINT_SPLAT_GLSL
#define INTRINSIC_POINT_SPLAT_GLSL

void PointTangentFrame(vec3 N, out vec3 T, out vec3 B)
{
    vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    T = normalize(cross(N, ref));
    B = cross(N, T);
}

void ProjectPointTangents(mat4 projection, vec2 viewport, vec3 viewPos,
                          vec3 viewT, vec3 viewB, out vec2 scrT, out vec2 scrB)
{
float z = max(-viewPos.z, 1e-4);
        float invZ2 = 1.0 / (z * z);
        float fx = projection[0][0];
        float fy = projection[1][1];
        float sx = 0.5 * viewport.x;
        float sy = 0.5 * viewport.y;

        scrT = vec2(
            sx * fx * (viewT.x * z + viewT.z * viewPos.x) * invZ2,
            sy * fy * (viewT.y * z + viewT.z * viewPos.y) * invZ2
        );
        scrB = vec2(
            sx * fx * (viewB.x * z + viewB.z * viewPos.x) * invZ2,
            sy * fy * (viewB.y * z + viewB.z * viewPos.y) * invZ2
        );
}

vec3 PointInverseCovariance(vec3 covariance, float det)
{
    float invDet = 1.0 / max(det, 1e-6);
    return vec3(covariance.z * invDet, -covariance.y * invDet, covariance.x * invDet);
}

void PointSplatExtent(float c00, float c11, vec2 viewport, out float extX, out float extY)
{
    const float CUTOFF = 3.0;
    extX = CUTOFF * sqrt(max(c00, 0.0));
    extY = CUTOFF * sqrt(max(c11, 0.0));
    extX = min(extX, viewport.x * 0.5);
    extY = min(extY, viewport.y * 0.5);
}

float PointMahalanobis(vec3 inverseCovariance, vec2 uv)
{
    return inverseCovariance.x * uv.x * uv.x
         + 2.0 * inverseCovariance.y * uv.x * uv.y
         + inverseCovariance.z * uv.y * uv.y;
}

vec3 ShadePointLambert(vec3 color, vec3 normal, vec4 lightDirectionAndIntensity, float ambient)
{
    float nLen = length(normal);
    vec3 N = (nLen > 1e-6) ? (normal / nLen) : vec3(0.0, 0.0, 1.0);
    vec3 lightDir = normalize(lightDirectionAndIntensity.xyz);
    float NdotL = dot(N, lightDir);
    float diffuse = max(abs(NdotL), 0.0) * lightDirectionAndIntensity.w;
    return color * (ambient + (1.0 - ambient) * diffuse);
}

#endif
