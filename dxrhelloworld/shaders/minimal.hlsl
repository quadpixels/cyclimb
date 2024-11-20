RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct Attributes
{
    float2 bary;
};

struct HitInfo
{
    float4 colorAndDistance;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;
    result.position = position;
    result.color = color;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}

[shader("raygeneration")]
void RayGen()
{
    RenderTarget[DispatchRaysIndex().xy] = float4(1, 1, 0.2, 1);
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    // NOP
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    // NOP
}