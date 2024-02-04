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

struct PSInput
{
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD;
};

cbuffer PerSceneCB : register(b1)
{
  float4x4 view;
  float4x4 projection;
  float win_w, win_h;
}

// Add this: NULL error
cbuffer PerObjectCB : register(b0)
{
  float x, y, z;
  float w, h;
  float4x4 orientation;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
  PSInput result;
  result.position.z = 0.0f;
  float min_wh = min(win_h, win_w);
  float half_h = h / 100;
  float half_w = w / 100;
  result.position = mul(orientation, position)  // Unit square
    * float4(half_w, half_h, 1.0f, 1.0f) // Aspect ratio correction
    + float4(x / 100, y / 100, 0, 0); // Translation

  result.position = mul(projection, mul(view, result.position));
  result.position.z /= 100.0f;

  result.uv = uv.xy;

  return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
  float4 rgba = g_texture.Sample(g_sampler, input.uv);
  if (rgba.a <= 0) {
    discard;
    return rgba; // Still need to return
  }
  else {
	return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
}
