RWTexture2D<float4> buffer : register(u0);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1: register(t1);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int width;
    int height;
    buffer.GetDimensions(width, height);
    
    float4 t0 = tex0.Load(int3(dispatchThreadID.x, dispatchThreadID.y, 0));
    float4 t1 = tex1.Load(int3(dispatchThreadID.x, dispatchThreadID.y, 0));
    
    float2 uv = dispatchThreadID.xy / float2(width, height);
    
    buffer[dispatchThreadID.xy] = (t0 == t1) ? float4(0.1, 0.1, 0.1, 1) : float4(1, 0.2, 0.2, 1);

}