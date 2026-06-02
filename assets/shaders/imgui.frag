#version 450

// GRAPHICS-079 Slice A — canonical default-recipe ImGui overlay fragment
// shader (CPUContracted placeholder). Pairs with `imgui.vert`. The
// premultiplied-alpha blend state lives on the pipeline
// (`BuildImGuiPipelineDesc`). Slice C forwards the copied ImGui vertex color;
// Slice D.2 samples the retained font atlas or a per-command user texture from
// the global bindless heap selected by `ImGuiOverlayPushConstants`.

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const uint kImGuiOverlayPushFlagUserTexture = 1u;
const uint kImGuiOverlayPushFlagFontAtlasColor = 2u;

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform ImGuiOverlayPushConstants
{
    uint64_t VertexBufferBDA;
    uint FirstVertex;
    uint IndexCount;
    uint FontAtlasBindlessIndex;
    uint TextureBindlessIndex;
    uint Flags;
    uint Reserved0;
    vec2 Scale;
    vec2 Translate;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    uint textureIndex = pc.TextureBindlessIndex;
    if (textureIndex == 0u)
        textureIndex = pc.FontAtlasBindlessIndex;

    if (textureIndex == 0u)
    {
        outColor = vColor;
        return;
    }

    vec4 sampled = texture(globalTextures[nonuniformEXT(textureIndex)], vUV);
    bool usesUserTexture = (pc.Flags & kImGuiOverlayPushFlagUserTexture) != 0u;
    bool fontAtlasHasColor = (pc.Flags & kImGuiOverlayPushFlagFontAtlasColor) != 0u;
    outColor = (usesUserTexture || fontAtlasHasColor)
        ? (vColor * sampled)
        : vec4(vColor.rgb, vColor.a * sampled.r);
}
