#version 460

// GRAPHICS-079 Slice A — canonical default-recipe ImGui overlay vertex
// shader (CPUContracted placeholder). The `ImGuiPass` pipeline created by
// `BuildImGuiPipelineDesc` references this artifact so the renderer's
// `m_ImGuiPipelineLease` resolves a real shader path; the CPU/null contract
// gate exercises the executor route through `MockDevice`, which does not
// read the SPV.
//
// Slice C switches the body to the operational `ImDrawVert` stream copied into
// a renderer-owned host-visible vertex buffer. The index buffer is bound
// through RHI; this shader resolves the indexed `gl_VertexIndex` against the
// pushed vertex-buffer device address and first-vertex offset.
// Slice D.2 extends the push-constant block with the selected per-command
// texture bindless index consumed by the fragment shader.

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct ImGuiVertex
{
    vec2 Position;
    vec2 UV;
    uint Color;
};

layout(buffer_reference, scalar) readonly buffer ImGuiVertexBuffer
{
    ImGuiVertex Vertices[];
};

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

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    ImGuiVertexBuffer vertexBuffer = ImGuiVertexBuffer(pc.VertexBufferBDA);
    ImGuiVertex vertex = vertexBuffer.Vertices[pc.FirstVertex + gl_VertexIndex];
    gl_Position = vec4(vertex.Position * pc.Scale + pc.Translate, 0.0, 1.0);
    vUV = vertex.UV;
    vColor = vec4(
        float(vertex.Color & 0xffu) / 255.0,
        float((vertex.Color >> 8u) & 0xffu) / 255.0,
        float((vertex.Color >> 16u) & 0xffu) / 255.0,
        float((vertex.Color >> 24u) & 0xffu) / 255.0);
}
