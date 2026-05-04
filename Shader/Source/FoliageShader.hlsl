#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

struct DepthVertexOutput
{
    float4 Position : SV_POSITION;
};

VertexOutput VsMain(VertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    VertexOutput Output;

    float4x4 World = ModelContext.World;
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

DepthVertexOutput VsMainDepth(VertexInput Input, uint InstanceId : SV_InstanceID)
{
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

GBufferOutput PsMain(VertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const float4 FallbackColor = ApplyMaterialOpacity(ResolveMaterialColorFallback(MaterialData), MaterialData);

    if (DiffuseColorTextureTableIndex < 0) {
        ApplyMaterialAlphaCut(FallbackColor.a, MaterialData);
        return BuildGBufferOutput(FallbackColor, Input.Normal, Input.WorldPosition, Input.Flags);
    }

    const uint TextureTableIndex = (uint)DiffuseColorTextureTableIndex;
    const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
    if (TextureSrvIndex == 0xffffffffu) {
        ApplyMaterialAlphaCut(FallbackColor.a, MaterialData);
        return BuildGBufferOutput(FallbackColor, Input.Normal, Input.WorldPosition, Input.Flags);
    }

    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
    const float4 SampledColor = ApplyMaterialOpacity(ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0)), MaterialData);
    ApplyMaterialAlphaCut(SampledColor.a, MaterialData);
    return BuildGBufferOutput(SampledColor, Input.Normal, Input.WorldPosition, Input.Flags);
}
