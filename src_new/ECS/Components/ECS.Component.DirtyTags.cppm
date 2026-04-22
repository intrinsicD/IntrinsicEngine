module;

export module Extrinsic.ECS.Component.DirtyTags;

export namespace Extrinsic::ECS::Components::DirtyTags
{
    struct GpuDirty
    {
    }; // full geometry re-upload
    struct DirtyVertexPositions
    {
    }; // positions changed, re-upload vertex buffer
    struct DirtyVertexAttributes
    {
    }; // colors/normals changed, re-upload attrib buffer
    struct DirtyEdgeTopology
    {
    }; // edge connectivity changed
    struct DirtyFaceTopology
    {
    }; // face connectivity changed
    struct DirtyTransform
    {
    }; // world matrix needs GPU sync (distinct from IsDirtyTag which is CPU-only)
}
