// Double method queries over the existing float-coordinate LBVH; result ABI stores IDs.
// All pruning, inclusion and kNN tie-break keys use the same double expression.
layout(buffer_reference,scalar,buffer_reference_align=8) buffer LbvhDoubles { double v[]; };
double lbvhDistanceDouble(vec3 a,vec3 b)
{
    precise dvec3 d=dvec3(a)-dvec3(b);
    precise double squared=(d.x*d.x+d.y*d.y)+d.z*d.z;
    return squared;
}
// Encode subnormal public float results with integer stores, bypassing float FTZ.
// Method outputs are nonnegative. Scaling by 2^149 is exact in binary64.
void lbvhStorePositiveFloat(uint64_t address,uint row,double value)
{
    uint bits=value>=0.0lf && value<1.1754943508222875e-38lf
        ? (uint(roundEven(value/1.4012984643248171e-45lf)) | (unpackDouble2x32(value).y & 0x80000000u))
        : floatBitsToUint(float(value));
    LbvhIndices(address).v[row]=bits;
}
uint lbvhQueryDouble(uint64_t positions,uint64_t nodes,uint pointCount,vec3 q,uint excluded,
                     double radius,uint kNearestCount,uint capacity,LbvhNeighbors neighbors,uint base)
{
    for(uint j=0;j<capacity;++j)neighbors.v[base+j]=LbvhNeighbor(LBVH_INVALID,0.);
    uint count=0,size=pointCount==0?0:1;uint stack[64];stack[0]=0;
    LbvhNodes tree=LbvhNodes(nodes);
    precise double limit=kNearestCount>0?1.7976931348623157e308lf:radius*radius;
    while(size>0)
    {
        LbvhNode node=tree.v[stack[--size]];
        if(lbvhDistanceDouble(q,clamp(q,node.lo,node.hi))>limit)continue;
        if(node.object==LBVH_INVALID){
            LbvhNode left=tree.v[node.left],right=tree.v[node.right];
            double ld=lbvhDistanceDouble(q,clamp(q,left.lo,left.hi));
            double rd=lbvhDistanceDouble(q,clamp(q,right.lo,right.hi));
            bool leftNear=ld<=rd;
            if((leftNear?rd:ld)<=limit)stack[size++]=leftNear?node.right:node.left;
            if((leftNear?ld:rd)<=limit)stack[size++]=leftNear?node.left:node.right;
            continue;
        }
        if(node.object==excluded)continue;
        double distance=lbvhDistanceDouble(q,node.lo);if(distance>limit)continue;
        uint value=node.object;
        if(kNearestCount>0)
        {
            for(uint k=0;k<capacity;++k)
            {
                if(value==LBVH_INVALID)break;
                uint old=neighbors.v[base+k].index;
                double oldDistance=old==LBVH_INVALID?1.7976931348623157e308lf:lbvhDistanceDouble(q,lbvhPoint(positions,12,old));
                if(distance<oldDistance || (distance==oldDistance && value<old))
                {neighbors.v[base+k].index=value;value=old;distance=oldDistance;}
            }
            count=min(count+1,capacity);
            if(count==capacity)limit=lbvhDistanceDouble(q,lbvhPoint(positions,12,neighbors.v[base+capacity-1].index));
        }
        else
        {
            ++count;
            for(uint k=0;k<capacity;++k)
                if(value<neighbors.v[base+k].index){uint old=neighbors.v[base+k].index;neighbors.v[base+k].index=value;value=old;}
        }
    }
    return count;
}
