struct MSVert
{
    float4 pos : SV_POSITION;
    float3 color : COLOR0;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void MSMain(
    out vertices MSVert outVerts[3],
    out indices uint3 outIndices[1])
{
    SetMeshOutputCounts(3, 1);
    outVerts[0].pos = float4(0.25, -0.25, 0, 1);
    outVerts[0].color = float3(1, 0, 0);
    outVerts[1].pos = float4(-0.25, -0.25, 0, 1);
    outVerts[1].color = float3(0, 1, 0);
    outVerts[2].pos = float4(0, 0.25, 0, 1);
    outVerts[2].color = float3(0, 0, 2);

    outIndices[0] = uint3(0, 1, 2);
}

float4 PSMain(MSVert pin) : SV_TARGET
{
    return float4(pin.color, 1.0);
}