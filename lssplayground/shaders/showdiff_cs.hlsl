RWTexture2D<float4> buffer : register(u0);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1: register(t1);

cbuffer PerSceneCb : register(b0)
{
    float thresh;  // out of 255
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int width;
    int height;
    buffer.GetDimensions(width, height);
    
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;
    
    float4 t0 = tex0.Load(int3(dispatchThreadID.x, dispatchThreadID.y, 0));
    float4 t1 = tex1.Load(int3(dispatchThreadID.x, dispatchThreadID.y, 0));
    
    float2 uv = dispatchThreadID.xy / float2(width, height);
    
    float th = 0;
    float4 ret;
    float dif = (float) (abs(t0 - t1));
    if (dif <= th)
    {
        ret = float4(0.1, 0.1, 0.1, 1);
    }
    else
    {
        ret = float4(1, 0.2, 0.1, 1);
    }
    buffer[dispatchThreadID.xy] = ret;

}