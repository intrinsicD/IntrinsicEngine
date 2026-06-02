#version 450

// GRAPHICS-079 Slice A — canonical default-recipe ImGui overlay fragment
// shader (CPUContracted placeholder). Pairs with `imgui.vert`. The
// premultiplied-alpha blend state lives on the pipeline
// (`BuildImGuiPipelineDesc`). Slice C forwards the copied ImGui vertex color;
// font-atlas / user-texture sampling through the bindless heap remains tied to
// the Slice D write-topology + Vulkan smoke, where the pass has a real render
// target and can validate pixel output.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
