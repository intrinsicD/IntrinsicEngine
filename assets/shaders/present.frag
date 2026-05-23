#version 450

// GRAPHICS-076 Slice A — canonical default-recipe present fragment.
// Samples `FrameRecipe.PresentSource` (LDR scene color after the
// postprocess chain or AA resolve, depending on `EnableAntiAliasing`)
// at UVs emitted by `present.vert` and writes the imported backbuffer
// LDR target with the alpha forced to opaque. CPU/null contract gate
// only validates the `BindPipeline + Draw(3, 1, 0, 0)` shape; Slice D's
// `gpu;vulkan` smoke is the operational verification that this fragment
// produces the correct pixels.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uPresentSource;

void main()
{
    vec3 color = texture(uPresentSource, vUV).rgb;
    outColor = vec4(color, 1.0);
}
