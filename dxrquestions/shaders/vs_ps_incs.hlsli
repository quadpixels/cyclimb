struct VSInput {
    float3 aPos   : POSITION;
    float3 aColor : COLOR0;
    float2 uv : TEXCOORD;
    uint mat_idx : DATA;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

struct PSOutput {
    float4 color : SV_Target;
};
