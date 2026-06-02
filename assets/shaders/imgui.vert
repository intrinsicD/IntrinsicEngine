#version 450

// GRAPHICS-079 Slice A — canonical default-recipe ImGui overlay vertex
// shader (CPUContracted placeholder). The `ImGuiPass` pipeline created by
// `BuildImGuiPipelineDesc` references this artifact so the renderer's
// `m_ImGuiPipelineLease` resolves a real shader path; the CPU/null contract
// gate exercises the executor route through `MockDevice`, which does not
// read the SPV.
//
// This Slice-A body is intentionally minimal: the operational `ImDrawVert`
// vertex layout (`vec2 pos`, `vec2 uv`, packed `uint col`), the
// per-frame transient host-visible vertex/index buffers, and the
// scale/translate push-constant transform are owned by Slice C of
// GRAPHICS-079 (the transient-upload + font-atlas slice). Until then no
// vertex buffer is bound, so this shader derives clip positions from
// `gl_VertexIndex` rather than reading attributes.

layout(push_constant) uniform ImGuiOverlayPushConstants
{
    uint DrawCommandCount;
    uint VertexCount;
    uint IndexCount;
    uint Flags;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    // Degenerate placeholder geometry (Slice C replaces this with the real
    // ImGui vertex stream). Keep the output well-defined so the SPV is valid.
    const vec2 p = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    vUV = p;
    vColor = vec4(1.0);
}
