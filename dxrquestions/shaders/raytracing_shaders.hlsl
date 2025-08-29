RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);

[shader("raygeneration")]
void MyRaygenShader()
{
  float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

  // Render interpolated DispatchRaysIndex outside the stencil window
  RenderTarget[DispatchRaysIndex().xy] = float4(lerpValues, 0, 1);
}