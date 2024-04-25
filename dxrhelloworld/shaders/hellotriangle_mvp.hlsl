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

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer Transform : register(b1) {
    float4x4 M;
    float4x4 V;
    float4x4 P;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
    PSInput result;

    result.position = mul(P, mul(V, mul(M, position)));
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET { return input.color; }
