#include "defines.hlsli"

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

struct FrameGlobalsGpu
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float Dt;
    uint FrameIndex;
    uint Flags;
    uint Pad0;
};

struct ModelContextGpu
{
    float4x4 World;
    float4x4 PrevWorld;
    float4 BbCenter;
    float4 BbExtents;
    uint Flags;
    uint BoneIndexStart;
    uint ObjectId;
    uint Pad0;
    float4 Custom0;
    float4 Custom1;
};

struct DrawRecordGpu
{
    uint ObjectIndex;
    uint MaterialIndex;
    uint Flags;
    uint Pad0;
};

struct MaterialFieldGpu
{
    uint Type;
    uint Padding0;
    uint Padding1;
    uint Padding2;
    float4 FloatValue;
    int64_t IntValue;
    uint2 Padding3;
};

struct MaterialGpu
{
    MaterialFieldGpu Fields[MATERIAL_FIELD_COUNT];
};

struct MaterialTextureTableItemGpu
{
    uint TextureSrvDescriptorIndex;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

struct RootConstantsB1
{
    uint FrameGlobalsSrvIndex;
    uint ModelContextSrvIndex;
    uint DrawRecordSrvIndex;
    uint DrawRecordBaseIndex;
    uint MaterialSrvIndex;
    uint MaterialTextureTableSrvIndex;
    float4 TintColor;
};

float4 ApplyBaseColor(float4 Color)
{
    return saturate(Color);
}

float4 ResolveFlags(float4 Color, uint Flags)
{
    const uint PickedFlagMask = 0x1u;

    if ((Flags & PickedFlagMask) != 0u) {
        const float3 PickTint = float3(0.7f, 0.03f, 0.03f);
        Color.rgb = saturate(Color.rgb + PickTint);
    }

    return Color;
}
