RWTexture2D<float4> outTexture : register(u0, space1);

cbuffer CameraBuffer : register(b0, space2) {
    float3 cameraPos;
    float3 cameraForward;
    float3 cameraRight;
    float3 cameraUp;
    uint debugMode;
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
    

float3 castRay(Header header, float3 ro, float3 rd, out float hitT, out uint iterations) {
    Node n = loadNode(header.root_node);
    float3 invDir = 1 / rd;
    float childHalf = header.half_len * 0.5;

    uint stackNode[STACK_SIZE];
    float3 stackCenter[STACK_SIZE];
    float stackHalf[STACK_SIZE];
    int sp;

    stackNode[0] = header.root_node;
    stackCenter[0] = header.center;
    stackHalf[0] = header.half_len;
    sp = 1;

    hitT = 1e30;
    iterations = 0;
    float3 voxelColor = float3(0, 0, 0);
    

    while (sp > 0) {
        iterations++;
        int cur = sp - 1;
        Node node = loadNode(stackNode[cur]);
        float3 center = stackCenter[cur];
        float half_len = stackHalf[cur];

        int w = sp;

        float child_half = half_len * 0.5;
        for (uint oct = 0; oct < 8; oct++) {
            if (((node.getChildMask() >> oct) & 1u) == 0) continue;
            
            float3 offset = float3(oct & 1, (oct >> 1) & 1, (oct >> 2) & 1) * 2.0 - 1.0;
            float3 cc = center + offset * child_half;

            float t0, t1;
            if (!intersectAABB(ro, invDir, cc - child_half, cc + child_half, t0, t1) || t0 >= hitT) continue;

            if ((node.getLeafMask() >> oct) & 1u) {
                Voxel v = loadVoxel(childIndex(node, oct));
                voxelColor = unpackVoxelColor(v.color);
                hitT = t0;
            }
            else if (sp < STACK_SIZE) {
                stackNode[w] = childIndex(node, oct);
                stackCenter[w] = cc;
                stackHalf[w] = child_half;
                w++;
            }
        }
        if (w > sp) {
            stackNode[cur] = stackNode[w - 1];
            stackCenter[cur] = stackCenter[w - 1];
            stackHalf[cur] = stackHalf[w - 1];
            sp = w - 1;
        }
        else {
            sp = cur;
        }
    }
    
    return voxelColor;
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
    float2 uv = (float2(pixel)) / screenSize * 2.0 - 1.0;

    float fov = radians(60);
    float fovScale = tan(fov * 0.5);
    float aspect = (float)width / (float)height;

    float screenX = uv.x * aspect * fovScale;
    float screenY = -uv.y * fovScale;


    float3 rayDir = normalize(screenX * cameraRight + screenY * cameraUp + cameraForward);
    // Pixel color
    float t;
    uint iterations;
    //float4 pixelColor = float4(castRay(header, cameraPos, rayDir, t), 1);
    float3 color = castRay(header, cameraPos, rayDir, t, iterations);
    if (debugMode == 1) {
        color = heatmap(iterations/ 64.0);
    }

    float4 pixelColor = float4(color, 1.0);
    outTexture[pixel] = pixelColor;
}