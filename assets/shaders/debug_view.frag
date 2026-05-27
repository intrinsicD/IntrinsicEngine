#version 450

// GRAPHICS-076 Slice B — canonical default-recipe `Pass.DebugView`
// fragment. Pairs with `debug_view.vert` (fullscreen-triangle UVs) and
// the canonical 16-byte `DebugViewPushConstants` packing
// (`ResourceKind`, `ResourceClass`, `UsedFallback`, `Reserved`) from
// `Graphics.DebugViewSystem.cppm`, per the GRAPHICS-013BQ decision that
// visualization mode is derived deterministically from the resolved
// selection's `FrameRecipeResourceKind` + `DebugViewResourceClass` rather
// than a user-selectable mode field. The CPU/null contract gate only
// validates the `BindPipeline + PushConstants(16) + Draw(3, 1, 0, 0)`
// command shape and pipeline-desc `PushConstantSize = 16u`; full
// per-class pixel-correctness on a real Vulkan device is deferred to a
// follow-up operational slice (alongside the descriptor-layout work in
// GRAPHICS-013BQ §"Descriptor binding ownership").

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[];

// 16-byte push-constant block matching
// `Extrinsic::Graphics::DebugViewPushConstants` (4 × uint32). Keep the
// layout strict-aligned to `std430` so the SPIR-V offsets line up with
// the C++ struct member order (ResourceKind = 0, ResourceClass = 4,
// UsedFallback = 8, Reserved = 12).
layout(push_constant) uniform Push
{
    uint ResourceKind;
    uint ResourceClass;
    uint UsedFallback;
    uint Reserved;
} pc;

// `DebugViewResourceClass` enum (must match
// `Graphics.DebugViewSystem.cppm`).
const uint kResourceClassTexture      = 0u;
const uint kResourceClassDepthTexture = 1u;
const uint kResourceClassBuffer       = 2u;
const uint kResourceClassBackbuffer   = 3u;
const uint kResourceClassAlias        = 4u;
const uint kResourceClassUnknown      = 5u;

ivec2 SampleCoords(vec2 uv, ivec2 size)
{
    ivec2 maxCoord = max(size - ivec2(1), ivec2(0));
    vec2 clamped = clamp(uv * vec2(size), vec2(0.0), vec2(maxCoord));
    return ivec2(clamped);
}

vec3 HashColor(uint v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;

    uint r = v;
    uint g = v * 1664525u + 1013904223u;
    uint b = v * 22695477u + 1u;

    return vec3(float(r & 255u), float(g & 255u), float(b & 255u)) / 255.0;
}

void main()
{
    // Buffer / Unknown classes are not previewable per
    // `DebugViewSystem::BuildInspectionTable()`; the renderer's
    // ResolveSelection fallback should have already routed away from
    // these before recording the pass, so we treat them as a safe
    // black fallback rather than asserting.
    if (pc.ResourceClass == kResourceClassBuffer ||
        pc.ResourceClass == kResourceClassUnknown)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    if (pc.ResourceClass == kResourceClassDepthTexture)
    {
        ivec2 size = textureSize(uTextures[0], 0);
        if (size.x <= 0 || size.y <= 0)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        float z = texelFetch(uTextures[0], SampleCoords(vUV, size), 0).r;
        outColor = vec4(vec3(clamp(z, 0.0, 1.0)), 1.0);
        return;
    }

    // Texture / Backbuffer / Alias — direct color sample. Backbuffer is
    // gated non-previewable by the inspection table for `DebugViewRGBA`,
    // but a `Backbuffer`-class resource that *is* previewable (the
    // imported swapchain target itself) sees the same direct color
    // path.
    ivec2 size = textureSize(uTextures[0], 0);
    if (size.x <= 0 || size.y <= 0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 c = texelFetch(uTextures[0], SampleCoords(vUV, size), 0).rgb;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
