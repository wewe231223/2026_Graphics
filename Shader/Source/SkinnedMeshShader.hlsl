#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);

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

    if (DiffuseColorTextureTableIndex < 0) {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    const uint TextureTableIndex = (uint)DiffuseColorTextureTableIndex;
    const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
    if (TextureSrvIndex == 0xffffffffu) {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[TextureSrvIndex];
    const float4 BaseColor = ApplyBaseColor(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    return ResolveFlags(BaseColor, Input.Flags);
}
