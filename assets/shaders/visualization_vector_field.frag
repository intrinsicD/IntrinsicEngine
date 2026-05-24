// visualization_vector_field.frag — canonical default-recipe
// visualization-overlay vector-field fragment shader (GRAPHICS-078
// Slice B).
//
// Forwards the per-vertex packed-unorm color (interpolated as a vec4)
// to `SceneColorHDR` with the alpha forced to opaque. The CPU/null
// contract only validates the `BindPipeline + PushConstants +
// Draw(N, 1, 0, 0)` shape; per-pixel correctness on a real Vulkan
// device is owned by the optional `gpu;vulkan` smoke (GRAPHICS-078
// Slice D).

#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor.rgb, 1.0);
}
