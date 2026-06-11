#version 460
layout(location = 0) in vec4 vColor;
layout(location = 1) flat in uint vPointMode;
layout(location = 2) in vec3 vViewNormal;
layout(location = 0) out vec4 outColor;

float DiscMetric(vec2 uv) {
    if (vPointMode != 2u) {
        return dot(uv, uv);
    }

    vec3 n = normalize(vViewNormal);
    float nz = max(abs(n.z), 0.15);
    float lifted = dot(n.xy, uv) / nz;
    return dot(uv, uv) + lifted * lifted;
}

void main() {
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = DiscMetric(uv);
    if (r2 > 1.0) {
        discard;
    }

    float edge = sqrt(max(r2, 0.0));
    float alpha = 1.0 - smoothstep(0.85, 1.0, edge);
    vec3 color = vColor.rgb;

    if (vPointMode == 1u) {
        float sphereZ = sqrt(max(1.0 - dot(uv, uv), 0.0));
        vec3 normal = normalize(vec3(uv, sphereZ));
        vec3 lightDir = normalize(vec3(-0.35, 0.45, 0.82));
        float diffuse = max(dot(normal, lightDir), 0.0);
        float specular = pow(max(dot(normal, normalize(lightDir + vec3(0.0, 0.0, 1.0))), 0.0), 24.0);
        color *= 0.35 + 0.65 * diffuse;
        color += vec3(0.18) * specular;
    } else if (vPointMode == 2u) {
        vec3 n = normalize(vViewNormal);
        float facing = abs(n.z);
        color *= 0.7 + 0.3 * facing;
    }

    outColor = vec4(color, vColor.a * alpha);
}
