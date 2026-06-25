#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

struct EnvironmentVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct EnvironmentDepthVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
};

struct EnvironmentVertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

struct EnvironmentDepthVertexOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
};

void ResolveEnvironmentYawSinCos(float YawRadians, out float SinValue, out float CosValue) {
    SinValue = 0.0f;
    CosValue = 1.0f;
    sincos(YawRadians, SinValue, CosValue);
}

float3 RotateEnvironmentYaw(float3 Value, float SinValue, float CosValue) {
    return float3((Value.x * CosValue) + (Value.z * SinValue), Value.y, (-Value.x * SinValue) + (Value.z * CosValue));
}

float3 BuildEnvironmentWorldPosition(float3 LocalPosition, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext, float SinYaw, float CosYaw) {
    const float4 SegmentPosition = mul(float4(LocalPosition, 1.0f), SegmentContext.LocalTransform);
    const float3 ScaledPosition = SegmentPosition.xyz * InstanceContext.PositionScale.w;
    return RotateEnvironmentYaw(ScaledPosition, SinYaw, CosYaw) + InstanceContext.PositionScale.xyz;
}

float3 BuildEnvironmentWorldDirection(float3 LocalDirection, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext, float SinYaw, float CosYaw) {
    const float3 SegmentDirection = mul(LocalDirection, (float3x3)SegmentContext.LocalTransform);
    return normalize(RotateEnvironmentYaw(SegmentDirection, SinYaw, CosYaw));
}

uint ResolveEnvironmentInstanceIndex(EnvironmentDrawRecordGpu DrawRecord, uint InstanceId) {
    if ((DrawRecord.GpuDrivenFlags & EnvironmentDrawRecordFlagGpuDriven) != 0u && RootConstants.Reserved1 != 0xffffffffu) {
        StructuredBuffer<uint> VisibleInstanceIndexBuffer = ResourceDescriptorHeap[RootConstants.Reserved1];
        return VisibleInstanceIndexBuffer[DrawRecord.VisibleInstanceOffset + InstanceId];
    }

    return DrawRecord.InstanceOffset + InstanceId;
}

EnvironmentVertexOutput VsMain(EnvironmentVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<EnvironmentSegmentContextGpu> SegmentContextBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[RootConstants.DrawRecordBaseIndex];
    const EnvironmentInstanceContextGpu InstanceContext = InstanceContextBuffer[ResolveEnvironmentInstanceIndex(DrawRecord, InstanceId)];
    const EnvironmentSegmentContextGpu SegmentContext = SegmentContextBuffer[DrawRecord.SegmentContextIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    float SinYaw = 0.0f;
    float CosYaw = 1.0f;
    ResolveEnvironmentYawSinCos(InstanceContext.RotationVariation.x, SinYaw, CosYaw);
    const float3 WorldPosition = BuildEnvironmentWorldPosition(Input.Position, InstanceContext, SegmentContext, SinYaw, CosYaw);

    EnvironmentVertexOutput Output;
    Output.Position = mul(float4(WorldPosition, 1.0f), FrameGlobals.ViewProj);
    Output.Normal = BuildEnvironmentWorldDirection(Input.Normal, InstanceContext, SegmentContext, SinYaw, CosYaw);
    Output.Tangent = BuildEnvironmentWorldDirection(Input.Tangent, InstanceContext, SegmentContext, SinYaw, CosYaw);
    Output.Bitangent = BuildEnvironmentWorldDirection(Input.Bitangent, InstanceContext, SegmentContext, SinYaw, CosYaw);
    Output.WorldPosition = WorldPosition;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

EnvironmentDepthVertexOutput VsMainDepth(EnvironmentDepthVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<EnvironmentSegmentContextGpu> SegmentContextBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[RootConstants.DrawRecordBaseIndex];
    const EnvironmentInstanceContextGpu InstanceContext = InstanceContextBuffer[ResolveEnvironmentInstanceIndex(DrawRecord, InstanceId)];
    const EnvironmentSegmentContextGpu SegmentContext = SegmentContextBuffer[DrawRecord.SegmentContextIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    float SinYaw = 0.0f;
    float CosYaw = 1.0f;
    ResolveEnvironmentYawSinCos(InstanceContext.RotationVariation.x, SinYaw, CosYaw);
    const float3 WorldPosition = BuildEnvironmentWorldPosition(Input.Position, InstanceContext, SegmentContext, SinYaw, CosYaw);

    EnvironmentDepthVertexOutput Output;
    Output.Position = mul(float4(WorldPosition, 1.0f), FrameGlobals.ViewProj);
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    return Output;
}

GBufferOutput PsMain(EnvironmentVertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const int64_t NormalTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_NORMAL_TEXTURE].IntValue;
    float4 BaseColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 WorldNormal = Input.Normal;

    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);
    if (DiffuseTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        BaseColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    }
    else {
        BaseColor = ResolveMaterialColorFallback(MaterialData);
    }

    BaseColor = ApplyMaterialOpacity(BaseColor, MaterialData);
    ApplyMaterialAlphaCut(BaseColor.a, MaterialData);

    const uint NormalTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, NormalTextureTableIndex);
    if (NormalTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> NormalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(NormalTextureSrvIndex)];
        const float NormalScale = ResolveMaterialNormalScale(MaterialData);
        const float3 NormalTangent = DecodeNormalMapColor(NormalTexture.Sample(LinearWrapSampler, Input.TexCoord0), NormalScale);
        WorldNormal = ResolveTbnNormalMappedWorldNormal(Input.Normal, Input.Tangent, Input.Bitangent, NormalTangent);
    }

    GBufferOutput Output = BuildGBufferOutput(BaseColor, WorldNormal, Input.WorldPosition, Input.Flags);
    Output.WorldPosition.w = FoliageGBufferSurfaceMarker;
    return Output;
}

void PsMainDepth(EnvironmentDepthVertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);
    float Alpha = 0.0f;
    if (DiffuseTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        Alpha = ApplyMaterialOpacityToAlpha(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0).a, MaterialData);
    }
    else {
        Alpha = ApplyMaterialOpacityToAlpha(ResolveMaterialColorFallback(MaterialData).a, MaterialData);
    }

    ApplyMaterialAlphaCut(Alpha, MaterialData);
}
