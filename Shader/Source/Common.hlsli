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
