//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_MORETRIANGLES2_HLSL
#define RAYTRACING_MORETRIANGLES2_HLSL

struct RayGenConstantBuffer
{
    int the_ray_flag;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0);
typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

[shader("raygeneration")]
void MyRaygenShader()
{
    float2 lerpValues = (float2) DispatchRaysIndex() / (float2) DispatchRaysDimensions();
    lerpValues = lerpValues * 2 - float2(1.0, 1.0);
    lerpValues.y *= -1;
    
    float3 dir = float3(0, 0, 1);
    float3 origin = float3(lerpValues, -1);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 100000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    
    TraceRay(
        Scene,
        g_rayGenCB.the_ray_flag,
        ~0, 0, 0, 0, ray, payload
    );

    // Render interpolated DispatchRaysIndex outside the stencil window
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(1, 1, 0, 1);
}

[shader("anyhit")]
void MyAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
    
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
  // Don't do anything
  //payload.color = float4(0, 0, 0, 1);
}

#endif // RAYTRACING_MORETRIANGLES2_HLSL