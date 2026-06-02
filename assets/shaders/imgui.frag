#version 450

// GRAPHICS-079 Slice A — canonical default-recipe ImGui overlay fragment
// shader (CPUContracted placeholder). Pairs with `imgui.vert`. The
// premultiplied-alpha blend state lives on the pipeline
// (`BuildImGuiPipelineDesc`), so this body just forwards the interpolated
// vertex color; the font-atlas / user-texture sample (via `RHI::Bindless`)
// and the `Flags`-driven texel path are owned by Slice C of GRAPHICS-079.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
