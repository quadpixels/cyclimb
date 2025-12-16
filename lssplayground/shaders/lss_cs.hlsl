RWTexture2D<float4> buffer : register(u0);
StructuredBuffer<float3> LssPositions : register(t0);
StructuredBuffer<float> LssRadii : register(t1);

#include "includes.hlsli"

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    int cam_mode; // 0=perspective, 1=orthogonal
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{   
    // No DispatchRaysDimensions() equivalent in CS :(
    int width;
    int height;
    buffer.GetDimensions(width, height);
    float2 dim = float2(width * 1.0f, height * 1.0f);
    
    float4 ret = float4(0.1, 0.1, 0.1, 1);
    float2 d = (dispatchThreadID.xy + 0.5f) / dim * 2.f - 1.f;
    d.y *= -1;
 
    float3 rd, ro;
    
    float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 1));
    float3 dir = TransformDirection(inverse_view, normalize(target));
    rd = dir;
    ro = TransformPosition(inverse_view, float3(0, 0, 0));
    
    uint pidx = 0;
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    float thit;
    bool hit = IntersectLSS(c0, r0, c1, r1, ro, rd, 0, 1e20, thit);
    if (hit)
    {
        ret = float4(1, 1, 0, 1);
    }
    buffer[dispatchThreadID.xy] = ret;

}