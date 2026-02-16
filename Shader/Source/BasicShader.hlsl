#include "Common.hlsli"

cbuffer TransformConstants : register(b0)
{
    float4x4 WorldViewProjection;
};

cbuffer PixelConstants : register(b1)
{
    float4 TintColor;
};

Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
};

VertexOutput VsMain(VertexInput Input)
{
    VertexOutput Output;
    Output.Position = mul(float4(Input.Position, 1.0f), WorldViewProjection);
    Output.Normal = normalize(Input.Normal);
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    float2 uv = Input.Normal.xy * 0.5f + 0.5f;
    float4 sampledColor = DiffuseTexture.Sample(DiffuseSampler, uv);
    float4 baseColor = float4(abs(Input.Normal), 1.0f);
    return ApplyBaseColor(sampledColor * baseColor * TintColor);
}
