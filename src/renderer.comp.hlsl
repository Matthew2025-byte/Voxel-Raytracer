RWTexture2D<float4> outTexture : register(u0, space1);

cbuffer CameraBuffer : register(b0, space2) {
    float3 cameraPos;
    float3 cameraForward;
    float3 cameraRight;
    float3 cameraUp;
}
ByteAddressBuffer octreeBuffer : register(t0, space0);

struct Node {
    uint child_pointer;
    uint masks;
    
    uint getChildMask() {
        return masks >> 8;
    }
    uint getLeafMask() {
        return masks & 0xF;
    }
};
    
    

bool castRay(float3 rayOrigin, float3 rayDir, float3 voxelMin, float3 voxelMax) {
    float3 t0 = (voxelMin - rayOrigin) / rayDir;
    float3 t1 = (voxelMax - rayOrigin) / rayDir;

    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);

    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit  = min(min(tFar.x, tFar.y), tFar.z);

    return tEnter <= tExit && tExit >= 0.0;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int3 center = octreeBuffer.Load3(0);
    int half_len = octreeBuffer.Load(12);
    uint root_node = octreeBuffer.Load(16);

    uint nodeIndex = 0;
    uint nodeOffset = 32 + (nodeIndex * 8);

    float3 rayOrigin = cameraPos;
    float3 voxelMin = float3(-0.5, -0.5, -0.5);
    float3 voxelMax = float3(0.5, 0.5, 0.5);

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

    float3 rayDir = normalize((screenX * cameraRight) + (screenY * cameraUp) + cameraForward);

    // Pixel color
    float4 pixelColor = float4(0.08, 0.10, 0.14, 1.0);

    if (castRay(rayOrigin, rayDir, voxelMin, voxelMax)) {
        pixelColor = float4(1.0, 0.45, 0.12, 1.0);
    }

    // Write to texture
    outTexture[pixel] = pixelColor;
}RWTexture2D<float4> outTexture : register(u0, space1);

cbuffer CameraBuffer : register(b0, space2) {
    float3 cameraPos;
    float3 cameraForward;
    float3 cameraRight;
    float3 cameraUp;
}
ByteAddressBuffer octreeBuffer : register(t0, space0);

struct Node {
    uint child_pointer;
    uint masks;
    
    uint getChildMask() {
        return masks >> 8;
    }
    uint getLeafMask() {
        return masks & 0xF;
    }
};
    
    

bool castRay(float3 rayOrigin, float3 rayDir, float3 voxelMin, float3 voxelMax) {
    float3 t0 = (voxelMin - rayOrigin) / rayDir;
    float3 t1 = (voxelMax - rayOrigin) / rayDir;

    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);

    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit  = min(min(tFar.x, tFar.y), tFar.z);

    return tEnter <= tExit && tExit >= 0.0;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int3 center = octreeBuffer.Load3(0);
    int half_len = octreeBuffer.Load(12);
    uint root_node = octreeBuffer.Load(16);

    uint nodeIndex = 0;
    uint nodeOffset = 32 + (nodeIndex * 8);

    float3 rayOrigin = cameraPos;
    float3 voxelMin = float3(-0.5, -0.5, -0.5);
    float3 voxelMax = float3(0.5, 0.5, 0.5);

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

    float3 rayDir = normalize((screenX * cameraRight) + (screenY * cameraUp) + cameraForward);

    // Pixel color
    float4 pixelColor = float4(0.08, 0.10, 0.14, 1.0);

    if (castRay(rayOrigin, rayDir, voxelMin, voxelMax)) {
        pixelColor = float4(1.0, 0.45, 0.12, 1.0);
    }

    // Write to texture
    outTexture[pixel] = pixelColor;
}