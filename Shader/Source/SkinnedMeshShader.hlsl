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

struct SkinnedDepthVertexOutput
{
    float4 Position : SV_POSITION;
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
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

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
        const float4x4 BoneMatrix = BonePaletteBuffer[BonePaletteIndex];
        SkinnedPosition += mul(float4(Input.Position, 1.0f), BoneMatrix) * BoneWeight;
        SkinnedNormal += mul(Input.Normal, (float3x3)BoneMatrix) * BoneWeight;
    }

    float4x4 World = ModelContext.World;
    const float4 WorldPosition = mul(SkinnedPosition, World);
    const float4 ClipPosition = mul(WorldPosition, FrameGlobals.ViewProj);
    const float4 PreviousWorldPosition = mul(SkinnedPosition, ModelContext.PrevWorld);
    Output.Position = ClipPosition;
    Output.ClipPosition = ClipPosition;
    Output.PreviousClipPosition = mul(PreviousWorldPosition, FrameGlobals.PrevViewProj);
    Output.RenderTargetSize = FrameGlobals.RenderTargetSize.xy;
    Output.Normal = normalize(mul(SkinnedNormal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

SkinnedDepthVertexOutput VsMainDepth(SkinnedVertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<float4x4> BonePaletteBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    float4 SkinnedPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint BoneWeightIndex = 0; BoneWeightIndex < 4; ++BoneWeightIndex) {
        const float BoneWeight = Input.BoneWeights[BoneWeightIndex];
        if (BoneWeight <= 0.0f) {
            continue;
        }

        const uint BonePaletteIndex = ModelContext.BoneIndexStart + Input.BoneIndices[BoneWeightIndex];
        const float4x4 BoneMatrix = BonePaletteBuffer[BonePaletteIndex];
        SkinnedPosition += mul(float4(Input.Position, 1.0f), BoneMatrix) * BoneWeight;
    }

    const float4 WorldPosition = mul(SkinnedPosition, ModelContext.World);

    SkinnedDepthVertexOutput Output;
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    return Output;
}

GBufferOutput PsMain(VertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);

    float4 BaseColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (DiffuseTextureSrvIndex != 0xffffffffu)
    {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        BaseColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    }
    else
    {
        BaseColor = ResolveMaterialColorFallback(MaterialData);
    }

    BaseColor = ApplyMaterialOpacity(BaseColor, MaterialData);
    return BuildGBufferOutput(BaseColor, Input.Normal, Input.WorldPosition, Input.Flags, Input.ClipPosition, Input.PreviousClipPosition, Input.RenderTargetSize);
}
