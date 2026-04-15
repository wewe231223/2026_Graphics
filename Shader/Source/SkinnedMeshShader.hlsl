#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

struct SkinnedVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

VertexOutput VsMain(SkinnedVertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<float4x4> BonePaletteBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];

    VertexOutput Output;
    float4 SkinnedPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 SkinnedNormal = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint BoneWeightIndex = 0; BoneWeightIndex < 4; ++BoneWeightIndex) {
        const float BoneWeight = Input.BoneWeights[BoneWeightIndex];
        if (BoneWeight <= 0.0f) {
            continue;
        }

        const uint BonePaletteIndex = ModelContext.BoneIndexStart + Input.BoneIndices[BoneWeightIndex];
        const float4x4 BoneMatrix = transpose(BonePaletteBuffer[BonePaletteIndex]);
        SkinnedPosition += mul(float4(Input.Position, 1.0f), BoneMatrix) * BoneWeight;
        SkinnedNormal += mul(Input.Normal, (float3x3)BoneMatrix) * BoneWeight;
    }

    float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(SkinnedPosition, World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(normalize(SkinnedNormal), (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;

    float4 SampledColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (DiffuseColorTextureTableIndex >= 0)
    {
        const uint TextureTableIndex = (uint) DiffuseColorTextureTableIndex;
        const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
        
        if (TextureSrvIndex != 0xffffffffu)
        {
            Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[TextureSrvIndex];
            SampledColor = ApplyBaseColor(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
        }
    }

    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(SampledColor, MaterialData);
    float4 LitColor = ApplyMaterialLighting(ScalarAppliedColor, Input.Normal);
    if (RootConstants.ShadowMappingParameterSrvIndex != 0xffffffffu && RootConstants.ShadowMapTextureSrvIndex != 0xffffffffu) {
        StructuredBuffer<ShadowMappingParameterGpu> ShadowMappingParameterBuffer = ResourceDescriptorHeap[RootConstants.ShadowMappingParameterSrvIndex];
        Texture2D<float> ShadowMapTexture = ResourceDescriptorHeap[RootConstants.ShadowMapTextureSrvIndex];
        const ShadowMappingParameterGpu ShadowMappingParameter = ShadowMappingParameterBuffer[0];
        LitColor = ApplyMaterialLightingWithShadow(ScalarAppliedColor, Input.Normal, Input.WorldPosition, ShadowMappingParameter, ShadowMapTexture, ShadowComparisonSampler);
    }
    
    return ResolveFlags(LitColor, Input.Flags);
}
