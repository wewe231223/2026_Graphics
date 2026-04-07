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
    uint Flags;
    uint BoneIndexStart;
    uint ObjectId;
    uint Pad0;
    float4 Custom0;
    float4 Custom1;
};

struct BoundingBoxContextGpu
{
    float4x4 World;
    float4 Center;
    float4 Extents;
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
    uint BonePaletteSrvIndex;
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

float4 ApplyMaterialScalarColor(float4 BaseColor, MaterialGpu MaterialData)
{
    const float4 MaterialBaseColor = MaterialData.Fields[MATERIAL_TYPE_BASE_COLOR].FloatValue;
    const float4 MaterialDiffuseColor = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR].FloatValue;
    const float Opacity = MaterialData.Fields[MATERIAL_TYPE_OPACITY].FloatValue.x;

    float4 ScalarColor = MaterialBaseColor;
    if (dot(ScalarColor, ScalarColor) <= 0.0f) {
        ScalarColor = MaterialDiffuseColor;
    }

    if (dot(ScalarColor, ScalarColor) <= 0.0f) {
        ScalarColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float EffectiveOpacity = (Opacity > 0.0f) ? Opacity : ScalarColor.a;
    float4 ResultColor = BaseColor * float4(ScalarColor.rgb, 1.0f);
    ResultColor.a *= saturate(EffectiveOpacity);
    return saturate(ResultColor);
}

float3 ApplyDirectionalLight(float3 BaseRgb, float3 WorldNormal)
{
    const float3 NormalizedNormal = normalize(WorldNormal);
    const float3 LightDirection = normalize(float3(0.4f, -1.0f, 0.35f));
    const float3 LightColor = float3(1.0f, 0.97f, 0.92f);
    const float AmbientIntensity = 0.6f;
    const float DiffuseIntensity = saturate(dot(NormalizedNormal, -LightDirection));

    const float3 LitColor = BaseRgb * (AmbientIntensity + (DiffuseIntensity * LightColor));
    return saturate(LitColor);
}

float4 ApplyMaterialLighting(float4 BaseColor, float3 WorldNormal)
{
    float4 ResultColor = BaseColor;
    ResultColor.rgb = ApplyDirectionalLight(ResultColor.rgb, WorldNormal);
    return ResultColor;
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
