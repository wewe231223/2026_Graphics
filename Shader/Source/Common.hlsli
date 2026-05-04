#include "defines.hlsli"

static const uint SHADOW_CASCADE_MAX_COUNT = 4u;
static const bool ENABLE_CASCADE_MAP_SHADOW_COLOR = false;
static const float3 GlobalLightColor = float3(1.0f, 0.97f, 0.92f);
static const float GlobalLightIntensity = 1.2f;
static const float AmbientLightIntensity = 0.25f;
static const float3 ShadowColor = float3(0.34f, 0.32f, 0.30f);

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
    float3 WorldPosition : WORLD_POSITION;
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

struct CameraParameterGpu
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float4 Position;
    float NearPlane;
    float FarPlane;
    float AspectRatio;
    float FovRadians;
};

struct ShadowMappingParameterGpu
{
    CameraParameterGpu ShadowCameras[SHADOW_CASCADE_MAX_COUNT];
    float4 LightDirection;
    float4 CascadeSplitDistances;
    float ShadowBiases[SHADOW_CASCADE_MAX_COUNT];
    float ShadowStrengths[SHADOW_CASCADE_MAX_COUNT];
    float ShadowMapSizes[SHADOW_CASCADE_MAX_COUNT];
    float RasterDepthBiases[SHADOW_CASCADE_MAX_COUNT];
    float RasterSlopeScaledDepthBiases[SHADOW_CASCADE_MAX_COUNT];
    uint CascadeCount;
    float Padding0;
    float Padding1;
    float Padding2;
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
    float4 Center;
    float4 Extents;
    float4 Orientation;
};

struct DebugGeometryContextGpu
{
    float4 Parameter0;
    float4 Parameter1;
    float4 Parameter2;
    float4 Color;
    uint Type;
    float LineThickness;
    uint Padding0;
    uint Padding1;
};

struct DrawRecordGpu
{
    uint ObjectIndex;
    uint MaterialIndex;
    uint Flags;
    uint TerrainPatchContextIndex;
};

struct TerrainPatchContextGpu
{
    float4 OuterTessFactors;
    float4 InsideTessFactors;
    float4 TileGrid;
    float4 HeightFieldParameters;
    float4 TerrainParameters;
    float4 TerrainUvParameters;
    uint HeightFieldSrvDescriptorIndex;
    uint SplatMapSrvDescriptorIndex;
    uint SplatMapWidth;
    uint SplatMapHeight;
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
    uint ShadowMappingParameterSrvIndex;
    uint ShadowMapTextureBaseSrvIndex;
    uint FrameGlobalsElementIndex;
    uint TerrainPatchContextSrvIndex;
    uint Reserved1;
};

struct GBufferOutput {
    float4 Albedo : SV_TARGET0;
    float4 NormalFlags : SV_TARGET1;
    float4 WorldPosition : SV_TARGET2;
};

GBufferOutput BuildGBufferOutput(float4 Albedo, float3 WorldNormal, float3 WorldPosition, uint Flags) {
    GBufferOutput Output;
    const float3 EncodedNormal = (normalize(WorldNormal) * 0.5f) + 0.5f;
    const float PickedFlag = ((Flags & 0x1u) != 0u) ? 1.0f : 0.0f;
    Output.Albedo = saturate(Albedo);
    Output.NormalFlags = float4(EncodedNormal, PickedFlag);
    Output.WorldPosition = float4(WorldPosition, 1.0f);
    return Output;
}

uint ResolveMaterialTextureSrvDescriptorIndex(StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, int64_t TextureTableIndex) {
    if (TextureTableIndex < 0) {
        return 0xffffffffu;
    }

    const uint MaterialTextureTableIndex = (uint)TextureTableIndex;
    return MaterialTextureTableBuffer[MaterialTextureTableIndex].TextureSrvDescriptorIndex;
}

float ResolveMaterialNormalScale(MaterialGpu MaterialData) {
    const float NormalScaleRaw = MaterialData.Fields[MATERIAL_TYPE_NORMAL_SCALE].FloatValue.x;
    return (NormalScaleRaw <= 0.0f) ? 1.0f : NormalScaleRaw;
}

float3 DecodeNormalMapColor(float4 NormalColor, float NormalScale) {
    float3 NormalTangent = NormalColor.xyz * 2.0f - 1.0f;
    NormalTangent.xy *= NormalScale;
    return normalize(NormalTangent);
}

float3 ResolveTbnNormalMappedWorldNormal(float3 WorldNormal, float3 WorldTangent, float3 WorldBitangent, float3 NormalTangent) {
    const float3 Normal = normalize(WorldNormal);
    const float TangentLengthSquared = dot(WorldTangent, WorldTangent);
    const float BitangentLengthSquared = dot(WorldBitangent, WorldBitangent);
    const float MaxLengthSquared = max(TangentLengthSquared, BitangentLengthSquared);

    if (MaxLengthSquared <= 0.000001f) {
        return Normal;
    }

    const float3 Tangent = WorldTangent * rsqrt(max(TangentLengthSquared, 0.000001f));
    const float3 Bitangent = WorldBitangent * rsqrt(max(BitangentLengthSquared, 0.000001f));
    return normalize((Tangent * NormalTangent.x) + (Bitangent * NormalTangent.y) + (Normal * NormalTangent.z));
}

float3 DecodeSrgbColor(float3 Color)
{
    return pow(saturate(Color), 2.2f);
}

float4 ApplyBaseColor(float4 Color)
{
    return saturate(Color);
}

float4 ApplyBaseColorToLinear(float4 Color)
{
    return float4(DecodeSrgbColor(Color.rgb), saturate(Color.a));
}

float4 ApplyMaterialOpacity(float4 BaseColor, MaterialGpu MaterialData)
{
    const float Opacity = MaterialData.Fields[MATERIAL_TYPE_OPACITY].FloatValue.x;
    float4 ResultColor = saturate(BaseColor);

    if (Opacity > 0.0f)
    {
        ResultColor.a *= saturate(Opacity);
    }

    return saturate(ResultColor);
}

float4 ResolveMaterialColorFallbackWithDefault(MaterialGpu MaterialData, float4 DefaultColor)
{
    const float4 MaterialBaseColor = MaterialData.Fields[MATERIAL_TYPE_BASE_COLOR].FloatValue;
    const float4 MaterialDiffuseColor = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR].FloatValue;

    float4 Color = MaterialBaseColor;
    if (dot(Color, Color) <= 0.0f)
    {
        Color = MaterialDiffuseColor;
    }

    if (dot(Color, Color) <= 0.0f)
    {
        Color = DefaultColor;
    }

    return ApplyBaseColorToLinear(Color);
}

float4 ResolveMaterialColorFallback(MaterialGpu MaterialData)
{
    return ResolveMaterialColorFallbackWithDefault(MaterialData, float4(1.0f, 1.0f, 1.0f, 1.0f));
}

float4 ApplyMaterialScalarColor(float4 BaseColor, MaterialGpu MaterialData)
{
    const float4 MaterialBaseColor = MaterialData.Fields[MATERIAL_TYPE_BASE_COLOR].FloatValue;
    const float4 MaterialDiffuseColor = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR].FloatValue;
    const float Opacity = MaterialData.Fields[MATERIAL_TYPE_OPACITY].FloatValue.x;

    float4 ScalarColor = MaterialBaseColor;
    if (dot(ScalarColor, ScalarColor) <= 0.0f)
    {
        ScalarColor = MaterialDiffuseColor;
    }

    if (dot(ScalarColor, ScalarColor) <= 0.0f)
    {
        ScalarColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float EffectiveOpacity = (Opacity > 0.0f) ? Opacity : ScalarColor.a;
    const float3 ScalarRgb = all(ScalarColor.rgb == 1.0f) ? float3(1.0f, 1.0f, 1.0f) : DecodeSrgbColor(ScalarColor.rgb);
    float4 ResultColor = BaseColor * float4(ScalarRgb, 1.0f);
    ResultColor.a *= saturate(EffectiveOpacity);
    return saturate(ResultColor);
}

float3 ApplyDirectionalLight(float3 BaseRgb, float3 WorldNormal)
{
    const float3 NormalizedNormal = normalize(WorldNormal);
    const float3 LightDirection = normalize(float3(0.4f, -1.0f, 0.35f));
    const float3 LightColor = GlobalLightColor * GlobalLightIntensity;
    const float AmbientIntensity = AmbientLightIntensity;
    const float DiffuseIntensity = saturate(dot(NormalizedNormal, -LightDirection));

    const float3 LitColor = BaseRgb * (AmbientIntensity + (DiffuseIntensity * LightColor));
    return LitColor;
}

float ResolveCascadeSplitDistance(ShadowMappingParameterGpu ShadowMappingParameter, uint CascadeIndex)
{
    if (CascadeIndex == 0u)
    {
        return ShadowMappingParameter.CascadeSplitDistances.x;
    }

    if (CascadeIndex == 1u)
    {
        return ShadowMappingParameter.CascadeSplitDistances.y;
    }

    if (CascadeIndex == 2u)
    {
        return ShadowMappingParameter.CascadeSplitDistances.z;
    }

    return ShadowMappingParameter.CascadeSplitDistances.w;
}

float ResolveCascadeShadowMapSize(ShadowMappingParameterGpu ShadowMappingParameter, uint CascadeIndex)
{
    return max(ShadowMappingParameter.ShadowMapSizes[CascadeIndex], 1.0f);
}

float ResolveCascadeShadowBias(ShadowMappingParameterGpu ShadowMappingParameter, uint CascadeIndex)
{
    return max(ShadowMappingParameter.ShadowBiases[CascadeIndex], 0.0f);
}

float ResolveCascadeShadowStrength(ShadowMappingParameterGpu ShadowMappingParameter, uint CascadeIndex)
{
    return saturate(ShadowMappingParameter.ShadowStrengths[CascadeIndex]);
}

uint ResolveCascadeIndex(ShadowMappingParameterGpu ShadowMappingParameter, FrameGlobalsGpu FrameGlobals, float3 WorldPosition)
{
    const uint EffectiveCascadeCount = clamp(ShadowMappingParameter.CascadeCount, 1u, SHADOW_CASCADE_MAX_COUNT);
    const float4 ViewPosition = mul(float4(WorldPosition, 1.0f), FrameGlobals.View);
    const float ViewDepth = max(ViewPosition.z, 0.0f);
    uint CascadeIndex = EffectiveCascadeCount - 1u;

    [unroll]
    for (uint CandidateCascadeIndex = 0u; CandidateCascadeIndex < SHADOW_CASCADE_MAX_COUNT; CandidateCascadeIndex += 1u)
    {
        if (CandidateCascadeIndex >= EffectiveCascadeCount)
        {
            break;
        }

        const float CascadeSplitDistance = ResolveCascadeSplitDistance(ShadowMappingParameter, CandidateCascadeIndex);
        if (ViewDepth <= CascadeSplitDistance)
        {
            CascadeIndex = CandidateCascadeIndex;
            break;
        }
    }

    return CascadeIndex;
}

float ComputeShadowVisibility(Texture2D<float> ShadowMapTexture, SamplerComparisonState ShadowComparisonSampler, CameraParameterGpu ShadowCamera, float ShadowBias, float ShadowMapSize, float3 WorldPosition)
{
    float4x4 ShadowViewProj = ShadowCamera.ViewProj;
    float4 ShadowClipPosition = mul(float4(WorldPosition, 1.0f), ShadowViewProj);
    float3 ShadowNdcPosition = ShadowClipPosition.xyz / max(ShadowClipPosition.w, 1.0e-5f);
    float2 ShadowUv = ShadowNdcPosition.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    if (any(ShadowUv < 0.0f) || any(ShadowUv > 1.0f) || ShadowNdcPosition.z < 0.0f || ShadowNdcPosition.z > 1.0f)
    {
        return 1.0f;
    }

    const float ReceiverDepth = ShadowNdcPosition.z - ShadowBias;
    const float TexelSize = ShadowMapSize > 0.0f ? (1.0f / ShadowMapSize) : 0.0f;

    float Visibility = 0.0f;
    [unroll]
    for (int SampleY = -1; SampleY <= 1; ++SampleY)
    {
        [unroll]
        for (int SampleX = -1; SampleX <= 1; ++SampleX)
        {
            float2 SampleOffset = float2((float) SampleX, (float) SampleY) * TexelSize;
            Visibility += ShadowMapTexture.SampleCmpLevelZero(ShadowComparisonSampler, ShadowUv + SampleOffset, ReceiverDepth);
        }
    }

    return saturate(Visibility / 9.0f);
}

float3 ResolveCascadeShadowColor(uint CascadeIndex)
{
    if (CascadeIndex == 0u)
    {
        return float3(1.0f, 0.52f, 0.52f);
    }

    if (CascadeIndex == 1u)
    {
        return float3(0.52f, 1.0f, 0.52f);
    }

    if (CascadeIndex == 2u)
    {
        return float3(0.52f, 0.68f, 1.0f);
    }

    return float3(1.0f, 0.92f, 0.52f);
}

float3 ApplyDirectionalLightWithShadow(float3 BaseRgb, float3 WorldNormal, float3 LightDirection, float ShadowVisibility, float ShadowStrength, uint CascadeIndex)
{
    const float3 NormalizedNormal = normalize(WorldNormal);
    const float3 NormalizedLightDirection = normalize(LightDirection);
    const float3 LightColor = GlobalLightColor * GlobalLightIntensity;
    const float AmbientIntensity = AmbientLightIntensity;
    const float DiffuseIntensity = saturate(dot(NormalizedNormal, -NormalizedLightDirection));
    if (ENABLE_CASCADE_MAP_SHADOW_COLOR)
    {
        const float3 FullyLitColor = BaseRgb * (AmbientIntensity + (DiffuseIntensity * LightColor));
        const float ShadowColorBlendFactor = saturate((1.0f - ShadowVisibility) * saturate(ShadowStrength));
        const float3 CascadeShadowColor = ResolveCascadeShadowColor(CascadeIndex);
        const float ShadowMask = (ShadowColorBlendFactor > 0.0f) ? 1.0f : 0.0f;
        const float3 LitColor = lerp(FullyLitColor, CascadeShadowColor, ShadowMask);
        return LitColor;
    }

    const float ShadowBlendFactor = saturate((1.0f - ShadowVisibility) * saturate(ShadowStrength));
    const float DiffuseShadowFactor = lerp(1.0f - saturate(ShadowStrength), 1.0f, ShadowVisibility);
    const float3 LitColor = BaseRgb * (AmbientIntensity + (DiffuseIntensity * DiffuseShadowFactor * LightColor));
    return lerp(LitColor, LitColor * ShadowColor, ShadowBlendFactor);
}

float4 ApplyMaterialLighting(float4 BaseColor, float3 WorldNormal)
{
    float4 ResultColor = BaseColor;
    ResultColor.rgb = ApplyDirectionalLight(ResultColor.rgb, WorldNormal);
    return ResultColor;
}

float4 ApplyMaterialLightingWithShadow(float4 BaseColor, float3 WorldNormal, float3 WorldPosition, ShadowMappingParameterGpu ShadowMappingParameter, FrameGlobalsGpu FrameGlobals, uint ShadowMapTextureBaseSrvIndex, SamplerComparisonState ShadowComparisonSampler)
{
    const uint CascadeIndex = ResolveCascadeIndex(ShadowMappingParameter, FrameGlobals, WorldPosition);
    Texture2D<float> ShadowMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(ShadowMapTextureBaseSrvIndex + CascadeIndex)];
    const CameraParameterGpu ShadowCamera = ShadowMappingParameter.ShadowCameras[CascadeIndex];
    const float CascadeShadowMapSize = ResolveCascadeShadowMapSize(ShadowMappingParameter, CascadeIndex);
    const float CascadeShadowBias = ResolveCascadeShadowBias(ShadowMappingParameter, CascadeIndex);
    const float CascadeShadowStrength = ResolveCascadeShadowStrength(ShadowMappingParameter, CascadeIndex);
    const float ShadowVisibility = ComputeShadowVisibility(ShadowMapTexture, ShadowComparisonSampler, ShadowCamera, CascadeShadowBias, CascadeShadowMapSize, WorldPosition);
    float4 ResultColor = BaseColor;
    ResultColor.rgb = ApplyDirectionalLightWithShadow(ResultColor.rgb, WorldNormal, ShadowMappingParameter.LightDirection.xyz, ShadowVisibility, CascadeShadowStrength, CascadeIndex);
    return ResultColor;
}

float4 ResolveFlags(float4 Color, uint Flags)
{
    const uint PickedFlagMask = 0x1u;
    if ((Flags & PickedFlagMask) != 0u)
    {
        const float3 PickTint = float3(0.7f, 0.03f, 0.03f);
        Color.rgb = saturate(Color.rgb + PickTint);
    }
    return Color;
}
