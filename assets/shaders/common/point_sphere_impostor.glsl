// Ray-cast sphere impostor shared by the forward point pass (forward/point.frag) and the
// transient debug spheres (transient_debug_sphere.frag). `uv` is the billboard coordinate in
// [-1, 1]^2, `viewCenter` and `viewRadius` the sphere in view space (camera looks down -Z).
// Returns false when the fragment misses the sphere or its surface point lies behind the camera
// or outside the depth range; otherwise shades `color` and writes the surface depth.
bool ShadeSphereImpostor(vec2 uv, vec3 viewCenter, float viewRadius, mat4 projection,
                         inout vec3 color, out float depth)
{
    depth = 0.0;
    if (dot(uv, uv) > 1.0) return false;
    float sphereZ = sqrt(max(1.0 - dot(uv, uv), 0.0));
    vec3 normal = normalize(vec3(uv, sphereZ));
    vec3 surfaceViewPos = viewCenter + vec3(uv * viewRadius, sphereZ * viewRadius);
    if (surfaceViewPos.z >= -1.0e-6) return false;
    vec4 clipPos = projection * vec4(surfaceViewPos, 1.0);
    if (clipPos.w <= 0.0) return false;
    depth = clipPos.z / clipPos.w;
    if (depth < 0.0 || depth > 1.0) return false;

    vec3 lightDir = normalize(vec3(-0.35, 0.45, 0.82));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = pow(max(dot(normal, normalize(lightDir + vec3(0.0, 0.0, 1.0))), 0.0), 24.0);
    color *= 0.35 + 0.65 * diffuse;
    color += vec3(0.18) * specular;
    return true;
}
