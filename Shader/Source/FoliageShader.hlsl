#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

struct FoliageVertexInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FoliageVertexOutput {
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

struct DepthVertexOutput {
    float4 Position : SV_POSITION;
};

FoliageVertexOutput VsMain(FoliageVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    const float4x4 World = ModelContext.World;
    const float3x3 WorldRotation = (float3x3)World;
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);

    FoliageVertexOutput Output;
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    Output.Normal = normalize(mul(Input.Normal, WorldRotation));
    Output.Tangent = normalize(mul(Input.Tangent, WorldRotation));
    Output.Bitangent = normalize(mul(Input.Bitangent, WorldRotation));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

DepthVertexOutput VsMainDepth(VertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), ModelContext.World);

    DepthVertexOutput Output;
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    return Output;
}

GBufferOutput PsMain(FoliageVertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const int64_t NormalTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_NORMAL_TEXTURE].IntValue;
    float4 BaseColor = ResolveMaterialColorFallback(MaterialData);
    float3 WorldNormal = Input.Normal;

    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);
    if (DiffuseTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        BaseColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
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

    return BuildGBufferOutput(BaseColor, WorldNormal, Input.WorldPosition, Input.Flags);
}
