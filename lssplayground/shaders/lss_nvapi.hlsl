RWTexture2D<float4> RenderTarget : register(u0);

#define NV_SHADER_EXTN_SLOT u999
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../../dxrquestions/nvapi/nvHLSLExtns.h"

struct MyPayload
{
    float t;
};

struct MyAttributes
{
    float2 bary;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 dri = DispatchRaysIndex();
    uint3 drd = DispatchRaysDimensions();
    RenderTarget[dri.xy] = float4(dri.xy * 1.0f / drd.xy, 0, 1);
}

[shader("miss")]
void Miss(inout MyPayload payload)
{
  //
}

[shader("closesthit")]
void ClosestHit(inout MyPayload payload, in MyAttributes attr)
{
    if (NvRtIsLssHit())
    {
        payload.t = RayTCurrent();
    }
}