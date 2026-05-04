#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);

struct NormalMappedVertexInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct SkinnedNormalMappedVertexInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct NormalMappedVertexOutput {
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

NormalMappedVertexOutput VsMain(NormalMappedVertexInput Input, uint InstanceId : SV_InstanceID) {
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

    NormalMappedVertexOutput Output;
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

NormalMappedVertexOutput SkinnedVsMain(SkinnedNormalMappedVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<float4x4> BonePaletteBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    float4 SkinnedPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 SkinnedNormal = float3(0.0f, 0.0f, 0.0f);
    float3 SkinnedTangent = float3(0.0f, 0.0f, 0.0f);
    float3 SkinnedBitangent = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint BoneWeightIndex = 0; BoneWeightIndex < 4; ++BoneWeightIndex) {
        const float BoneWeight = Input.BoneWeights[BoneWeightIndex];
        if (BoneWeight <= 0.0f) {
            continue;
        }

        const uint BonePaletteIndex = ModelContext.BoneIndexStart + Input.BoneIndices[BoneWeightIndex];
        const float4x4 BoneMatrix = BonePaletteBuffer[BonePaletteIndex];
        const float3x3 BoneRotation = (float3x3)BoneMatrix;
        SkinnedPosition += mul(float4(Input.Position, 1.0f), BoneMatrix) * BoneWeight;
        SkinnedNormal += mul(Input.Normal, BoneRotation) * BoneWeight;
        SkinnedTangent += mul(Input.Tangent, BoneRotation) * BoneWeight;
        SkinnedBitangent += mul(Input.Bitangent, BoneRotation) * BoneWeight;
    }

    const float4x4 World = ModelContext.World;
    const float3x3 WorldRotation = (float3x3)World;
    const float4 WorldPosition = mul(SkinnedPosition, World);

    NormalMappedVertexOutput Output;
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    Output.Normal = normalize(mul(normalize(SkinnedNormal), WorldRotation));
    Output.Tangent = normalize(mul(normalize(SkinnedTangent), WorldRotation));
    Output.Bitangent = normalize(mul(normalize(SkinnedBitangent), WorldRotation));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

GBufferOutput PsMain(NormalMappedVertexOutput Input) {
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

    const uint NormalTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, NormalTextureTableIndex);
    if (NormalTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> NormalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(NormalTextureSrvIndex)];
        const float NormalScale = ResolveMaterialNormalScale(MaterialData);
        const float3 NormalTangent = DecodeNormalMapColor(NormalTexture.Sample(LinearWrapSampler, Input.TexCoord0), NormalScale);
        WorldNormal = ResolveTbnNormalMappedWorldNormal(Input.Normal, Input.Tangent, Input.Bitangent, NormalTangent);        
    }

    BaseColor = ApplyMaterialOpacity(BaseColor, MaterialData);
    return BuildGBufferOutput(BaseColor, WorldNormal, Input.WorldPosition, Input.Flags);
}
