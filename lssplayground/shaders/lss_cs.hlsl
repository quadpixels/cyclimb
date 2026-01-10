RWTexture2D<float4> buffer : register(u0);
StructuredBuffer<float3> LssPositions : register(t0);
StructuredBuffer<float> LssRadii : register(t1);

#include "includes.hlsli"

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    float3 ro;
    int cam_mode; // 0=perspective, 1=orthogonal
    float3 rd;
    int viz_mode;
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
    float3 p0 = LssPositions[pidx * 2];
    float3 p1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    float thit;
    bool hit = IntersectLSS(p0, r0, p1, r1, ro, rd, 0, 1e20, thit);
    
    if (hit)
    {
        float3 hit_p = ro + rd * thit; // Assume no transform
        float ax = length(hit_p - p0);
        float bx = length(hit_p - p1);
        const float EPS = 1e-4;
        float u;
        if (abs(r0 - ax) < EPS)
        {
            u = 0;
        }
        else if (abs(r1 - bx) < EPS)
        {
            u = 1;
        }
        else
        {
            float jx = sqrt(ax * ax - r0 * r0);
            float xk = sqrt(bx * bx - r1 * r1);
            u = (jx / (jx + xk));
        }
        float3 p = lerp(p0, p1, u).xyz;
        float3 n = normalize(hit_p - p);
        
        switch (viz_mode)
        {
            case 0:{  // Hit/miss
                    ret = float4(1, 1, 0, 1);
                    break;
                }
            case 1:{
                    float dnl = dot(n, normalize(float3(1, 1, 1)));
                    dnl = dnl * 0.5 + 0.5;
                    ret = float4(dnl, dnl, dnl, 1);
                    break;
                }
            case 2:{
                    ret = float4(n * 0.5 + 0.5, 1);
                    break;
                }
            case 3:{
                    ret = float4(MapDistToColorRamp(thit), 1);
                    break;
                }
        }
    }
    

    buffer[dispatchThreadID.xy] = ret;

}