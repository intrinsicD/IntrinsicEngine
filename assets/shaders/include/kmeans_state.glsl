// Scalar BDA layout shared with Runtime.ClusteringGpuBackend's Push.
#ifndef INTRINSIC_KMEANS_STATE_GLSL
#define INTRINSIC_KMEANS_STATE_GLSL
#extension GL_ARB_gpu_shader_fp64 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(buffer_reference, scalar) buffer Floats { float v[]; };
layout(buffer_reference, scalar) buffer Doubles { double v[]; };
layout(buffer_reference, scalar) buffer UInts { uint v[]; };
layout(buffer_reference, scalar) buffer Stats {
    float inertia; float maxDistance; uint maxShift;
    uint maxDistanceIndex; uint changed; uint invalid;
    double inertiaSum;
};
layout(push_constant, scalar) uniform Push {
    uint64_t inputBDA; uint64_t positions; uint64_t slots; uint64_t centroids;
    uint64_t labels; uint64_t previous; uint64_t distances; uint64_t sums;
    uint64_t counts; uint64_t stats; uint64_t outputBDA; uint64_t presentation;
    uint points; uint clusters; uint phase; uint first; uint count; uint innerFirst; uint innerCount;
} pc;
vec3 load3(uint64_t address,uint row){Floats a=Floats(address);return vec3(a.v[row*3],a.v[row*3+1],a.v[row*3+2]);}
void store3(uint64_t address,uint row,vec3 value){Floats a=Floats(address);a.v[row*3]=value.x;a.v[row*3+1]=value.y;a.v[row*3+2]=value.z;}
float distance2(vec3 a,vec3 b){precise vec3 d=a-b;precise vec3 sq=d*d;precise float result=(sq.x+sq.y)+sq.z;return result;}
uint slot(uint row){return pc.slots==uint64_t(0)?row:UInts(pc.slots).v[row];}
#endif
