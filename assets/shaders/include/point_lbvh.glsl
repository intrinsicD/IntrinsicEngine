// Scalar-layout LBVH ABI shared by standalone queries and method consumers.
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
const uint LBVH_INVALID=0xffffffffu;
struct LbvhNode { vec3 lo; uint left; vec3 hi; uint right; uint object; uint first; uint last; uint reserved; };
layout(buffer_reference,scalar,buffer_reference_align=4) buffer LbvhFloats { float v[]; };
layout(buffer_reference,scalar) buffer LbvhKeys { uvec2 v[]; };
layout(buffer_reference,scalar) buffer LbvhNodes { LbvhNode v[]; };
layout(buffer_reference,scalar) buffer LbvhBounds { vec3 lo; uint invalid; vec3 hi; uint reserved; };
struct LbvhNeighbor { uint index; float distance; };
layout(buffer_reference,scalar) buffer LbvhNeighbors { LbvhNeighbor v[]; };
layout(buffer_reference,scalar) buffer LbvhHeaders { uvec2 v[]; };
layout(buffer_reference,scalar,buffer_reference_align=4) buffer LbvhIndices { uint v[]; };
bool lbvhValid(vec3 p) { return !any(isnan(p)) && !any(isinf(p)) && all(lessThanEqual(abs(p),vec3(1e18))); }
vec3 lbvhPoint(uint64_t address,uint stride,uint i)
{
    LbvhFloats f=LbvhFloats(address+uint64_t(i)*stride);
    return vec3(f.v[0],f.v[1],f.v[2]);
}
float lbvhDistance(vec3 a,vec3 b)
{
    precise vec3 d=a-b;
    precise float squared=(d.x*d.x+d.y*d.y)+d.z*d.z;
    return squared;
}
float lbvhBoxDistance(LbvhNode node,vec3 q) { return lbvhDistance(q,clamp(q,node.lo,node.hi)); }
// Orders an internal node's children by box distance. Pushing the far child first visits the
// near one first, so nearest/kNN limits shrink early; strict pruning keeps results order-independent.
void lbvhOrderChildren(LbvhNodes tree,LbvhNode node,vec3 q,out uint nearChild,out float nearDistance,
                       out uint farChild,out float farDistance)
{
    float left=lbvhBoxDistance(tree.v[node.left],q),right=lbvhBoxDistance(tree.v[node.right],q);
    bool leftNear=left<=right;
    nearChild=leftNear?node.left:node.right; nearDistance=leftNear?left:right;
    farChild=leftNear?node.right:node.left; farDistance=leftNear?right:left;
}
LbvhNeighbor lbvhNearest(uint64_t nodes,uint count,vec3 q,uint excluded)
{
    LbvhNeighbor best=LbvhNeighbor(LBVH_INVALID,uintBitsToFloat(0x7f800000u));
    if(count==0 || !lbvhValid(q)) return best;
    LbvhNodes tree=LbvhNodes(nodes);
    uint stack[64]; uint size=1; stack[0]=0;
    while(size>0)
    {
        LbvhNode node=tree.v[stack[--size]];
        if(lbvhBoxDistance(node,q)>best.distance) continue;
        if(node.object!=LBVH_INVALID)
        {
            if(node.object==excluded) continue;
            float d=lbvhDistance(q,node.lo);
            if(d<best.distance || (d==best.distance && node.object<best.index)) best=LbvhNeighbor(node.object,d);
            continue;
        }
        uint nearChild,farChild;float nearDistance,farDistance;
        lbvhOrderChildren(tree,node,q,nearChild,nearDistance,farChild,farDistance);
        if(farDistance<=best.distance) stack[size++]=farChild;
        if(nearDistance<=best.distance) stack[size++]=nearChild;
    }
    return best;
}

LbvhNeighbor lbvhNearest(uint64_t nodes,uint count,vec3 q)
{
    return lbvhNearest(nodes,count,q,LBVH_INVALID);
}
