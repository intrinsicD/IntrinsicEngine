// Shared surface color resolution helpers for both forward and deferred surface passes.
// These helpers stay source-level so the same code path works with external build-time
// compilation today and runtime shader recompilation / hot reload later.

vec4 ResolveSurfaceBaseColor()
{
    // Color priority: per-vertex color > per-face color > texture.
    //
    // Per-vertex colors are interpolated across the triangle by the rasterizer,
    // providing smooth scalar field / RGB visualization on mesh surfaces.
    // Per-face colors are flat (one color per triangle via gl_PrimitiveID).
    //
    // When PtrIndices != 0 AND PtrVertexAttr != 0: nearest-vertex (Voronoi) mode.
    // Each fragment picks the color of the closest triangle vertex by Euclidean
    // distance in object space, producing sharp Voronoi-like cell boundaries.
    if (push.PtrVertexAttr != 0ul && push.PtrIndices != 0ul)
    {
        IndexBuf iBuf = IndexBuf(push.PtrIndices);
        PosBuf pBuf = PosBuf(push.PtrPositions);
        VertexAttrBuf vaBuf = VertexAttrBuf(push.PtrVertexAttr);

        uint i0 = iBuf.idx[gl_PrimitiveID * 3 + 0];
        uint i1 = iBuf.idx[gl_PrimitiveID * 3 + 1];
        uint i2 = iBuf.idx[gl_PrimitiveID * 3 + 2];

        vec3 p0 = pBuf.v[i0];
        vec3 p1 = pBuf.v[i1];
        vec3 p2 = pBuf.v[i2];

        vec3 d0 = fragObjectPos - p0;
        vec3 d1 = fragObjectPos - p1;
        vec3 d2 = fragObjectPos - p2;

        float dist0 = dot(d0, d0);
        float dist1 = dot(d1, d1);
        float dist2 = dot(d2, d2);

        uint nearestIdx;
        if (dist0 <= dist1 && dist0 <= dist2)
            nearestIdx = i0;
        else if (dist1 <= dist2)
            nearestIdx = i1;
        else
            nearestIdx = i2;

        return unpackUnorm4x8(vaBuf.color[nearestIdx]);
    }

    if (push.PtrVertexAttr != 0ul)
    {
        return fragVertexColor;
    }

    if (push.PtrFaceAttr != 0ul)
    {
        FaceAttrBuf fBuf = FaceAttrBuf(push.PtrFaceAttr);
        return unpackUnorm4x8(fBuf.color[gl_PrimitiveID]);
    }

    return texture(globalTextures[nonuniformEXT(fragTexID)], fragTexCoord);
}
