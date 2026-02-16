#include "Common.hlsli"

struct VertexInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

VertexOutput VsMain(VertexInput Input)
{
    VertexOutput Output;
    Output.Position = float4(Input.Position, 1.0f);
    Output.Color = ApplyBaseColor(Input.Color);
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    return ApplyBaseColor(Input.Color);
}
