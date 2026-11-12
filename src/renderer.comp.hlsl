RWTexture2D<float4> outTexture : register(u0, space1);

cbuffer CameraBuffer : register(b0, space2) {
    float3 cameraPos;
    float3 cameraForward;
    float3 cameraRight;
    float3 cameraUp;
    uint debugMode;
    float3 sunDir;
}
ByteAddressBuffer octreeBuffer : register(t0, space0);


static const uint HEADER_SIZE = 32;
#define STACK_SIZE 64

struct Header {
    int3 center;
    int half_len;
    uint root_node;
};
struct Node {
    uint child_pointer;
    uint masks;
    
    uint getChildMask() { return (masks >> 8) & 0xFF; }
    uint getLeafMask() { return masks & 0xFF; }
};
struct Voxel {
    uint color;
    uint padding;
};

Node loadNode(uint index) {
    uint2 raw = octreeBuffer.Load2(HEADER_SIZE + index * 8);
    Node n; n.child_pointer = raw.x, n.masks = raw.y;
    return n;
}
uint childIndex(Node n, uint octant) {
    uint below = n.getChildMask() & ((1u << octant) - 1u);
    return n.child_pointer + countbits(below);
}
Voxel loadVoxel(uint index) {
    uint2 raw = octreeBuffer.Load2(HEADER_SIZE + index * 8);
    Voxel v;
    v.color = raw.x;
    v.padding = raw.y;
    return v;
}
float3 unpackVoxelColor(uint color) {
    return float3(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF) / 255.0;
}

float3 heatmap(float t) {
    t = saturate(t);
    return float3(saturate(t * 2.0 - 1.0), 1.0 - abs(t * 2.0 - 1.0), saturate(1.0 - t * 2.0));
}

bool intersectAABB(float3 ro, float3 invDir, float3 bmin, float3 bmax, out float tEnter, out float tExit) {
    float3 t0 = (bmin - ro) * invDir;
    float3 t1 = (bmax - ro) * invDir;

    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);

    tEnter = max(max(tNear.x, tNear.y), tNear.z);
    tExit  = min(min(tFar.x, tFar.y), tFar.z);

    return tEnter <= tExit && tExit >= 0.0;
}
    
struct RayHit {
    bool hit;
    float hitT;
    float3 position;
    float3 color;
    float3 normal;

    uint iterations;
};
RayHit castRay(Header header, float3 ro, float3 rd) {
    RayHit result;
    result.hit = false;
    result.hitT = 1e30;
    result.iterations = 0;

    float3 safeRD = rd + float3(abs(rd) < 1e-8) * 1e-8;
    float3 invDir = 1 / safeRD;

    uint octMask = (rd.x < 0 ? 1u : 0u) | (rd.y < 0 ? 2u : 0u) | (rd.z < 0 ? 4u : 0u);
    float3 rootCenter = float3(header.center);
    float rootHalf = (float)header.half_len;

    float tr0, tr1;
    if (!intersectAABB(ro, invDir, rootCenter - rootHalf, rootCenter + rootHalf, tr0, tr1))
        return result;

    uint stackNode[STACK_SIZE];
    float3 stackCenter[STACK_SIZE];
    uint stackI[STACK_SIZE];
    int sp = 0;

    uint curIdx = header.root_node;
    Node node = loadNode(curIdx);
    float3 center = rootCenter;
    float halfLen = rootHalf;
    uint i = 0;
    

    while (true) {
        if (i >= 8) {
            if (sp == 0) break;
            sp--;
            curIdx= stackNode[sp];
            node = loadNode(curIdx);
            center = stackCenter[sp];
            i = stackI[sp];
            halfLen *= 2.0;
            result.iterations++;
            continue;
        }
        
        uint oct = i ^ octMask;
        i++;

        if (((node.getChildMask() >> oct) & 1u) == 0) continue;
        float childHalf = halfLen * 0.5;
        float3 offset = float3(oct & 1, (oct >> 1) & 1, (oct >> 2) & 1) * 2.0 - 1.0;
        float3 cc = center + offset * childHalf;

        float t0, t1;
        if (!intersectAABB(ro, invDir, cc - childHalf, cc + childHalf, t0, t1)) continue;

        uint next = childIndex(node, oct);

        if ((node.getLeafMask() >> oct) & 1u) {
            result.hit = true;
            result.hitT = max(t0, 0.0);
            result.position = ro + rd * result.hitT;
            result.color = unpackVoxelColor(loadVoxel(next).color);
            return result;
        }

        if (sp < STACK_SIZE) {
            stackNode[sp] = curIdx;
            stackCenter[sp] = center;
            stackI[sp] = i;
            sp++;
            curIdx = next;
            node = loadNode(curIdx);
            center = cc;
            halfLen = childHalf;
            i = 0;
            result.iterations++;
        }
    }
    
    return result;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Header header;
    header.center = octreeBuffer.Load3(0);
    header.half_len = octreeBuffer.Load(12);
    header.root_node = octreeBuffer.Load(16);

    uint nodeIndex = 0;
    uint nodeOffset = 32 + (nodeIndex * 8);

    // Threads pixel
    uint width, height;
    outTexture.GetDimensions(width, height);

    uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= width || pixel.y >= height)
        return;
        
    float2 screenSize = float2(width, height);
    float2 uv = (float2(pixel) + 0.5) / screenSize * 2.0 - 1.0;

    float fov = radians(60);
    float fovScale = tan(fov * 0.5);
    float aspect = (float)width / (float)height;

    float screenX = uv.x * aspect * fovScale;
    float screenY = -uv.y * fovScale;


    float3 rayDir = normalize(screenX * cameraRight + screenY * cameraUp + cameraForward);
    // Pixel color
    RayHit hit = castRay(header, cameraPos, rayDir);
    float3 color = hit.color;
    if (debugMode == 1) {
        color = heatmap(hit.iterations / 64.0);
    }

    float4 pixelColor = float4(color, 1.0);
    outTexture[pixel] = pixelColor;
}