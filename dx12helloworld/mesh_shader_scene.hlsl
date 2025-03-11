struct MSVert
{
    float4 pos : SV_POSITION;
    float3 color : COLOR0;
};

[outputtopology("triangle")]
[numthreads(2, 1, 1)]
void MSMain(
    out vertices MSVert outVerts[6],
    out indices uint3 outIndices[2],
    uint3 tid : SV_GroupThreadID,
    uint3 gid : SV_GroupID)
{
    SetMeshOutputCounts(6, 2); // For this threadgroup. Only the input values from the first active thread are used.
    
    int offset = tid.x * 3;
    const float x0 = -0.9 + 0.1 * tid.x;
    const float x1 = x0 + 0.1;
    const float y0 = -0.1 + gid.x * (-0.2);
    const float y1 = y0 + 0.2;
    
    outVerts[offset + 0].pos = float4(x0, y0, 0, 1);
    outVerts[offset + 0].color = float3(1, 0, 0);
    outVerts[offset + 1].pos = float4(x0, y1, 0, 1);
    outVerts[offset + 1].color = float3(0, 1, 0);
    outVerts[offset + 2].pos = float4(x1, y1, 0, 1);
    outVerts[offset + 2].color = float3(0, 0, 1);

    const float idx0 = offset;
    outIndices[tid.x] = uint3(idx0, idx0 + 1, idx0 + 2);
}

float4 PSMain(MSVert pin) : SV_TARGET
{
    return float4(pin.color, 1.0);
}